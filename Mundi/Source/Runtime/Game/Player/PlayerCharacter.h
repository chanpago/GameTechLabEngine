#pragma once

#include "Character.h"
#include "IDamageable.h"
#include "CombatTypes.h"
#include "Source/Runtime/Core/Containers/UEContainer.h"
#include "APlayerCharacter.generated.h"
class UStatsComponent;
class UHitboxComponent;
class USpringArmComponent;
class UCameraComponent;
class UAnimMontage;
class UParticleSystem;
class UParticleSystemComponent;
class UCamMod_Shake;
class AGameState;

// ============================================================================
// APlayerCharacter - 플레이어 캐릭터 예시
// ============================================================================

UCLASS(DisplayName = "플레이어 캐릭터", Description = "플레이어가 조작하는 캐릭터")
class APlayerCharacter : public ACharacter, public IDamageable
{
public:
    GENERATED_REFLECTION_BODY()

    APlayerCharacter();
    virtual ~APlayerCharacter() = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void Jump() override;

    // ========================================================================
    // IDamageable 구현
    // ========================================================================
    virtual float TakeDamage(const FDamageInfo& DamageInfo) override;
    virtual bool IsAlive() const override;
    virtual bool CanBeHit() const override;
    virtual bool IsBlocking() const override;
    virtual bool IsParrying() const override;
    virtual void OnHitReaction(EHitReaction Reaction, const FDamageInfo& DamageInfo) override;
    virtual void OnDeath() override;

    // ========================================================================
    // 전투 액션
    // ========================================================================

    /** 약공격 */
    void LightAttack();

    /** 강공격 */
    void HeavyAttack();

    /** 대시공격 (포커스 50 소모) */
    void DashAttack();

    /** 궁극기 (포커스 100 소모) */
    void UltimateAttack();

    /** 회피 (구르기) */
    void Dodge();

    /** 가드 시작 */
    void StartBlock();

    /** 가드 종료 */
    void StopBlock();

    /** 차징 시작 */
    void StartCharging();

    /** 차징 종료 */
    void StopCharging();

    /** 포션 마시기 (상체 몽타주) */
    void DrinkPotion();

    /** 콤보 윈도우 활성화 (AnimNotify에서 호출) */
    void EnableComboWindow();

    /** 콤보 윈도우 비활성화 (AnimNotify에서 호출) */
    void DisableComboWindow();

    // ========================================================================
    // 상태 확인
    // ========================================================================
    ECombatState GetCombatState() const { return CombatState; }
    bool IsInvincible() const { return bIsInvincible; }
    bool IsJumpPending() const { return bJumpPending; }

    // ========================================================================
    // 컴포넌트 접근
    // ========================================================================
    UStatsComponent* GetStatsComponent() const { return StatsComponent; }

    TMap<FString, TArray<UParticleSystemComponent*>> PlayerParticles;
protected:
    // ========================================================================
    // 입력 처리
    // ========================================================================
    void ProcessInput(float DeltaTime);
    void ProcessMovementInput(float DeltaTime);
    void ProcessCombatInput();

    // ========================================================================
    // 콜백
    // ========================================================================
    void HandleHealthChanged(float CurrentHealth, float MaxHealth);
    void HandleStaminaChanged(float CurrentStamina, float MaxStamina);
    void HandleDeath();

    // ========================================================================
    // 내부 함수
    // ========================================================================
    void SetCombatState(ECombatState NewState);
    void UpdateAttackState(float DeltaTime);
    void UpdateParryWindow(float DeltaTime);
    void UpdateStagger(float DeltaTime);
    void UpdateDodgeState(float DeltaTime);
    void UpdateMovementState(float DeltaTime);
    void UpdateEffect(float DeltaTime);

    /** 스킬 차징 시작 (1=DashAttack, 2=UltimateAttack) */
    void StartSkillCharging(int32 SkillType);

    /** 스킬 차징 상태 업데이트 */
    void UpdateSkillCharging(float DeltaTime);

    /** 대기 중인 스킬 실행 */
    void ExecutePendingSkill();

    /** 입력 방향을 기반으로 8방향 인덱스 반환 (0=F, 1=FR, 2=R, 3=BR, 4=B, 5=BL, 6=L, 7=FL) */
    int32 GetDodgeDirectionIndex() const;

protected:
    // ========== 컴포넌트 ==========
    UStatsComponent* StatsComponent = nullptr;
    UHitboxComponent* HitboxComponent = nullptr;
    USpringArmComponent* SpringArm = nullptr;

    // ========== 전투 상태 ==========
    AGameState* GS = nullptr;
    ECombatState CombatState = ECombatState::Idle;

    // ========== 상태 플래그 ==========
    UPROPERTY(EditAnywhere, Category = "Combat")
    bool bIsInvincible = false;         // 무적 (회피 중)

    UPROPERTY(EditAnywhere, Category = "Combat")
    bool bIsBlocking = false;           // 가드 중

    UPROPERTY(EditAnywhere, Category = "Combat")
    bool bIsParrying = false;           // 패리 윈도우

    bool bCanCombo = false;             // 콤보 입력 가능
    bool bBufferedHeavyAttack = false;  // HeavyAttack 버퍼링 여부
    float LightAttackTimer = 0.f;       // LightAttack 경과 시간

    /** 콤보 전환 시간 (이 시간에 HeavyAttack으로 전환) */
    UPROPERTY(EditAnywhere, Category = "Combat")
    float ComboTransitionTime = 0.35f;

    // ========== 타이머 ==========
    float ParryWindowTimer = 0.f;

    UPROPERTY(EditAnywhere, Category = "Combat", Tooltip = "패리 판정 시간")
    float ParryWindowDuration = 0.2f;

    float StaggerTimer = 0.f;

    // ========== 점프 딜레이 ==========
    bool bJumpPending = false;          // 점프 대기 중
    float JumpDelayTimer = 0.f;         // 점프 딜레이 타이머

    /** 점프 딜레이 시간 (이 시간 후에 실제 점프) */
    UPROPERTY(EditAnywhere, Category = "Movement")
    float JumpDelayTime = 0.21f;

    /** 착지 후 점프 쿨다운 (착지 직후 바로 점프 방지) */
    bool bLandingCooldown = false;
    float LandingCooldownTimer = 0.f;

    /** 착지 쿨다운 시간 */
    UPROPERTY(EditAnywhere, Category = "Movement")
    float LandingCooldownTime = 0.1f;

    /** Idle 상태 유지 시간 (점프 가능 조건 체크용) */
    float IdleStateTimer = 0.f;

    /** Idle 상태에서 점프 가능해지기까지 필요한 시간 */
    UPROPERTY(EditAnywhere, Category = "Movement")
    float IdleJumpDelayTime = 0.5f;

    // ========== 콤보 ==========
    UPROPERTY(EditAnywhere, Category = "Combat")
    int32 ComboCount = 0;

    UPROPERTY(EditAnywhere, Category = "Combat")
    int32 MaxComboCount = 3;

    // ========== 공격 몽타주 ==========
    /** 약공격 애니메이션 경로 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    FString LightAttackAnimPath;

    /** 약공격 몽타주 */
    UAnimMontage* LightAttackMontage = nullptr;

    /** 약공격 루트 모션 활성화 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    bool bEnableLightAttackRootMotion = true;

    /** 약공격 끝에서 자를 시간 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    float LightAttackCutEndTime = 0.0f;

    /** 강공격 애니메이션 경로 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    FString HeavyAttackAnimPath;

    /** 강공격 몽타주 */
    UAnimMontage* HeavyAttackMontage = nullptr;

    /** 강공격 루트 모션 활성화 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    bool bEnableHeavyAttackRootMotion = true;

    /** 강공격 끝에서 자를 시간 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    float HeavyAttackCutEndTime = 0.0f;

    /** 대시공격 애니메이션 경로 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    FString DashAttackAnimPath;

    /** 대시공격 몽타주 */
    UAnimMontage* DashAttackMontage = nullptr;

    /** 대시공격 루트 모션 활성화 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    bool bEnableDashAttackRootMotion = true;

    /** 대시공격 끝에서 자를 시간 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    float DashAttackCutEndTime = 0.0f;

    /** 궁극기 애니메이션 경로 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    FString UltimateAttackAnimPath;

    /** 궁극기 몽타주 */
    UAnimMontage* UltimateAttackMontage = nullptr;

    /** 궁극기 루트 모션 활성화 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    bool bEnableUltimateAttackRootMotion = true;

    /** 궁극기 끝에서 자를 시간 */
    UPROPERTY(EditAnywhere, Category = "Animation|Attack")
    float UltimateAttackCutEndTime = 0.0f;

    // ========== 피격 몽타주 (4방향) ==========
    /** 피격 애니메이션 경로 - Forward */
    UPROPERTY(EditAnywhere, Category = "Animation|Hit")
    FString HitAnimPath_F;

    /** 피격 애니메이션 경로 - Backward */
    UPROPERTY(EditAnywhere, Category = "Animation|Hit")
    FString HitAnimPath_B;

    /** 피격 애니메이션 경로 - Right */
    UPROPERTY(EditAnywhere, Category = "Animation|Hit")
    FString HitAnimPath_R;

    /** 피격 애니메이션 경로 - Left */
    UPROPERTY(EditAnywhere, Category = "Animation|Hit")
    FString HitAnimPath_L;

    /** 피격 몽타주 배열 (4방향) - 인덱스: 0=F, 1=B, 2=R, 3=L */
    UAnimMontage* HitMontages[4] = { nullptr };

    /** 피격 시 루트 모션 활성화 여부 */
    UPROPERTY(EditAnywhere, Category = "Animation|Hit")
    bool bEnableHitRootMotion = false;

    /** 피격 애니메이션 끝에서 자를 시간 */
    UPROPERTY(EditAnywhere, Category = "Animation|Hit")
    float HitCutEndTime = 0.0f;

    // ========== 사망 몽타주 ==========
    /** 사망 애니메이션 경로 */
    UPROPERTY(EditAnywhere, Category = "Animation|Death")
    FString DeathAnimPath;

    /** 사망 몽타주 */
    UAnimMontage* DeathMontage = nullptr;

    /** 사망 시 루트 모션 활성화 여부 */
    UPROPERTY(EditAnywhere, Category = "Animation|Death")
    bool bEnableDeathRootMotion = false;

    /** 사망 애니메이션 끝에서 자를 시간 */
    UPROPERTY(EditAnywhere, Category = "Animation|Death")
    float DeathCutEndTime = 0.0f;

    // ========== 구르기 몽타주 (8방향) ==========
    /** 구르기 애니메이션 경로 - Forward */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_F;

    /** 구르기 애니메이션 경로 - Forward-Right */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_FR;

    /** 구르기 애니메이션 경로 - Right */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_R;

    /** 구르기 애니메이션 경로 - Backward-Right */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_BR;

    /** 구르기 애니메이션 경로 - Backward */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_B;

    /** 구르기 애니메이션 경로 - Backward-Left */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_BL;

    /** 구르기 애니메이션 경로 - Left */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_L;

    /** 구르기 애니메이션 경로 - Forward-Left */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    FString DodgeAnimPath_FL;

    /** 구르기 몽타주 배열 (8방향) - 인덱스: 0=F, 1=FR, 2=R, 3=BR, 4=B, 5=BL, 6=L, 7=FL */
    UAnimMontage* DodgeMontages[8] = { nullptr };

    /** 구르기 시 루트 모션 활성화 여부 */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    bool bEnableDodgeRootMotion = true;

    // ========== 가드 몽타주 ==========
    /** 가드 애니메이션 경로 (루프) */
    UPROPERTY(EditAnywhere, Category = "Animation|Block")
    FString BlockAnimPath;

    /** 가드 몽타주 */
    UAnimMontage* BlockMontage = nullptr;

    // ========== 가드 브레이크 몽타주 ==========
    /** 가드 브레이크 애니메이션 경로 (넘어지는 모션) */
    UPROPERTY(EditAnywhere, Category = "Animation|GuardBreak")
    FString GuardBreakAnimPath;

    /** 가드 브레이크 몽타주 */
    UAnimMontage* GuardBreakMontage = nullptr;

    /** 가드 브레이크 시 루트 모션 활성화 여부 */
    UPROPERTY(EditAnywhere, Category = "Animation|GuardBreak")
    bool bEnableGuardBreakRootMotion = true;

    /** 가드 브레이크 애니메이션 끝에서 자를 시간 */
    UPROPERTY(EditAnywhere, Category = "Animation|GuardBreak")
    float GuardBreakCutEndTime = 0.0f;

    // ========== 차징 몽타주 ==========
    /** 차징 애니메이션 경로 (루프) */
    UPROPERTY(EditAnywhere, Category = "Animation|Charging")
    FString ChargingAnimPath;

    /** 차징 몽타주 */
    UAnimMontage* ChargingMontage = nullptr;

    // ========== 포션 ==========
    /** 포션 몽타주 애니메이션 경로 */
    UPROPERTY(EditAnywhere, Category = "Animation|Potion")
    FString PotionAnimPath;

    /** 포션 몽타주 */
    UAnimMontage* PotionMontage = nullptr;

    /** 상체 분리 기준 본 이름 */
    UPROPERTY(EditAnywhere, Category = "Animation|Potion")
    FString UpperBodyRootBoneName = "Spine1";

    /** 포션 회복량 */
    UPROPERTY(EditAnywhere, Category = "Animation|Potion")
    float PotionHealAmount = 30.f;

    /** 포션 마시는 중 여부 */
    bool bIsDrinkingPotion = false;

    /** 차징 중 여부 */
    bool bIsCharging = false;

    // ========== 스킬 차징 준비 ==========
    /** 준비 중인 스킬 타입 (0=없음, 1=DashAttack, 2=UltimateAttack) */
    int32 PendingSkillType = 0;

    /** 차징 루프 횟수 */
    int32 ChargingLoopCount = 0;

    /** 차징 루프 목표 횟수 */
    UPROPERTY(EditAnywhere, Category = "Animation|Charging")
    int32 ChargingLoopTarget = 2;

    /** 차징 애니메이션 길이 추적용 타이머 */
    float ChargingLoopTimer = 0.f;

    /** 차징 애니메이션 1회 길이 (초) */
    UPROPERTY(EditAnywhere, Category = "Animation|Charging")
    float ChargingLoopDuration = 0.5f;

    /** 구르기 애니메이션 끝에서 자를 시간 (초 단위) */
    UPROPERTY(EditAnywhere, Category = "Animation|Dodge")
    float DodgeAnimationCutEndTime = 0.0f;

    // ========== 이동 ==========
    UPROPERTY(EditAnywhere, Category = "Movement")
    float MoveSpeed = 500.f;

    UPROPERTY(EditAnywhere, Category = "Movement")
    float RotationSpeed = 10.f;

    // Effect
    // Charging Effect
    void GatherParticles();
    bool bWasCharging = false;
};
