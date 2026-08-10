#pragma once

#include "EnemyBase.h"
#include "ABossEnemy.generated.h"

class UAnimMontage;

// ============================================================================
// ABossEnemy - 보스 적 클래스
// ============================================================================

UCLASS(DisplayName = "보스 에너미", Description = "보스 적 캐릭터")
class ABossEnemy : public AEnemyBase
{
public:
    GENERATED_REFLECTION_BODY()

    ABossEnemy();
    virtual ~ABossEnemy() = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnDeath() override;

    // ========================================================================
    // 보스 전용 공격 패턴
    // ========================================================================
    UFUNCTION(LuaBind,DisplayName='Attack',Tooltip="Play AttackPattern")
    void ExecuteAttackPattern(int PatternIndex);

    /** 몽타주 이름으로 재생 (Lua용) */
    bool PlayMontageByName(const FString& MontageName, float BlendIn = 0.1f, float BlendOut = 0.1f, float PlayRate = 1.0f);

    /** 현재 재생 중인 몽타주의 재생 속도 변경 (Lua용) */
    void SetMontagePlayRate(float NewPlayRate);

    /** 페이즈 변경 */
    void SetPhase(int32 NewPhase);
    int32 GetPhase() const { return CurrentPhase; }

    /** 현재 공격 패턴 이름 */
    const FString& GetCurrentPatternName() const { return CurrentPatternName; }
    void SetCurrentPatternName(const FString& Name);

    /** AI 상태 (디버그용) */
    const FString& GetAIState() const { return AIState; }
    void SetAIState(const FString& State) { AIState = State; }

    /** 이동 타입 (디버그용) */
    const FString& GetMovementType() const { return MovementType; }
    void SetMovementType(const FString& Type) { MovementType = Type; }

    /** 플레이어까지 거리 */
    float GetDistanceToPlayer() const { return DistanceToPlayer; }
    void SetDistanceToPlayer(float Dist) { DistanceToPlayer = Dist; }

protected:
    // ========================================================================
    // 보스 공격 패턴들
    // ========================================================================
    void Attack_LightCombo();       // 약공격 콤보
    void Attack_HeavySlam();        // 강공격 내려찍기
    void Attack_ChargeAttack();     // 돌진 공격
    void Attack_SpinAttack();       // 회전 공격

    // ========================================================================
    // 페이즈 전환
    // ========================================================================
    void CheckPhaseTransition();
    void OnPhaseChanged(int32 OldPhase, int32 NewPhase);

protected:
    // ========== 보스 설정 ==========
    UPROPERTY(EditAnywhere, Category = "Boss")
    int32 CurrentPhase = 1;

    UPROPERTY(EditAnywhere, Category = "Boss")
    int32 MaxPhase = 2;

    UPROPERTY(EditAnywhere, Category = "Boss", Tooltip = "페이즈 2 진입 HP 비율")
    float Phase2HealthThreshold = 0.5f;

    /** 현재 공격 패턴 이름 (디버그용) */
    FString CurrentPatternName = "None";

    /** AI 상태 (디버그용): Idle, Strafing, Approaching, Attacking, Retreating, Chasing */
    FString AIState = "Idle";

    /** 이동 타입 (디버그용): None, Walk, Run, Strafe_L, Strafe_R, Retreat, Approach */
    FString MovementType = "None";

    /** 플레이어까지 거리 (디버그용) */
    float DistanceToPlayer = 0.f;

    // ========== 공격 애니메이션 경로 ==========
    UPROPERTY(EditAnywhere, Category = "Animation")
    FString LightComboAnimPath;

    UPROPERTY(EditAnywhere, Category = "Animation")
    FString HeavySlamAnimPath;

    UPROPERTY(EditAnywhere, Category = "Animation")
    FString ChargeStartAnimPath;

    UPROPERTY(EditAnywhere, Category = "Animation")
    FString ChargeAttackAnimPath;

    UPROPERTY(EditAnywhere, Category = "Animation")
    FString SpinAttackAnimPath;

    // ========== 콤보 애니메이션 경로 ==========
    // 베기 콤보 (3타)
    UPROPERTY(EditAnywhere, Category = "Animation|Combo")
    FString Slash_1_AnimPath;
    UPROPERTY(EditAnywhere, Category = "Animation|Combo")
    FString Slash_2_AnimPath;
    UPROPERTY(EditAnywhere, Category = "Animation|Combo")
    FString Slash_3_AnimPath;

    // 회전 콤보 (2타)
    UPROPERTY(EditAnywhere, Category = "Animation|Combo")
    FString Spin_1_AnimPath;
    UPROPERTY(EditAnywhere, Category = "Animation|Combo")
    FString Spin_2_AnimPath;

    // 내려찍기 콤보 (2타)
    UPROPERTY(EditAnywhere, Category = "Animation|Combo")
    FString Smash_1_AnimPath;
    UPROPERTY(EditAnywhere, Category = "Animation|Combo")
    FString Smash_2_AnimPath;

    // ========== 죽음 애니메이션 ==========
    UPROPERTY(EditAnywhere, Category = "Animation")
    FString DeathAnimPath;

    // ========== 징벌 공격 애니메이션 ==========
    UPROPERTY(EditAnywhere, Category = "Animation")
    FString PunishAttackAnimPath;

    // ========== 궁극기 애니메이션 ==========
    UPROPERTY(EditAnywhere, Category = "Animation")
    FString UltimateAnimPath;

    // ========== 포효 애니메이션 ==========
    UPROPERTY(EditAnywhere, Category = "Animation")
    FString RoarAnimPath;

    // ========== 공격 애니메이션 몽타주 ==========
    UAnimMontage* LightComboMontage = nullptr;
    UAnimMontage* HeavySlamMontage = nullptr;
    UAnimMontage* ChargeStartMontage = nullptr;
    UAnimMontage* ChargeAttackMontage = nullptr;
    UAnimMontage* SpinAttackMontage = nullptr;

    // ========== 콤보 애니메이션 몽타주 ==========
    UAnimMontage* Slash_1_Montage = nullptr;
    UAnimMontage* Slash_2_Montage = nullptr;
    UAnimMontage* Slash_3_Montage = nullptr;
    UAnimMontage* Spin_1_Montage = nullptr;
    UAnimMontage* Spin_2_Montage = nullptr;
    UAnimMontage* Smash_1_Montage = nullptr;
    UAnimMontage* Smash_2_Montage = nullptr;

    // ========== 죽음 애니메이션 몽타주 ==========
    UAnimMontage* DeathMontage = nullptr;

    UAnimMontage* PunishAttackMontage = nullptr;

    // ========== 궁극기 애니메이션 몽타주 ==========
    UAnimMontage* UltimateMontage = nullptr;

    // ========== 포효 애니메이션 몽타주 ==========
    UAnimMontage* RoarMontage = nullptr;

    // ========== 애니메이션 설정 ==========
    UPROPERTY(EditAnywhere, Category = "Animation")
    bool bEnableAttackRootMotion = true;

    UPROPERTY(EditAnywhere, Category = "Animation")
    float AnimationCutEndTime = 0.0f;

    // ========== LockOnIndicator ==========
    class UBillboardComponent* LockOnIndicator = nullptr;
    int32 LockOnBoneIndex = INDEX_NONE;
};
