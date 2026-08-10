#include "pch.h"
#include "PlayerCharacter.h"
#include "StatsComponent.h"
#include "HitboxComponent.h"
#include "SpringArmComponent.h"
#include "CameraComponent.h"
#include "InputManager.h"
#include "SkeletalMeshComponent.h"
#include "CharacterMovementComponent.h"
#include "ParticleSystemComponent.h"
#include "World.h"
#include "GameModeBase.h"
#include "GameState.h"
#include"BossEnemy.h"
#include "Source/Runtime/Engine/Animation/AnimMontage.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"
#include "Source/Runtime/Engine/Animation/AnimSequence.h"
#include "Source/Runtime/AssetManagement/ResourceManager.h"
#include "Source/Runtime/Engine/GameFramework/PlayerCameraManager.h"


APlayerCharacter::APlayerCharacter()
{
    // 스탯 컴포넌트
    StatsComponent = CreateDefaultSubobject<UStatsComponent>("StatsComponent");

    // 히트박스 컴포넌트 (무기에 붙일 수도 있음)
    HitboxComponent = CreateDefaultSubobject<UHitboxComponent>("HitboxComponent");
    HitboxComponent->SetBoxExtent(FVector(50.f, 50.f, 50.f));

    // 스프링암 + 카메라 (필요시)
    // SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
    // Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
}

void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    // PIE 모드일 때만 StatsComponent 초기화
    if (GWorld && GWorld->bPie)
    {
        // GetComponent로 StatsComponent 찾기
        UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
        if (Stats)
        {
            // 시작 시 풀 HP/스태미나 설정
            Stats->RestoreFullHealth();
            Stats->RestoreFullStamina();
            UE_LOG("[PlayerCharacter] StatsComponent initialized - HP: %.0f, Stamina: %.0f",
                   Stats->GetCurrentHealth(), Stats->GetCurrentStamina());
        }
        else
        {
            UE_LOG("[PlayerCharacter] WARNING: StatsComponent not found!");
        }
    }

    // 히트박스 소유자 설정
    if (HitboxComponent)
    {
        HitboxComponent->SetOwnerActor(this);
    }

    // 무기 충돌 시 데미지 처리 등록
    OnWeaponHit.Add([this](AActor* HitActor, const FDamageInfo& DamageInfo) {
        if (IDamageable* Target = GetDamageable(HitActor))
        {
            if (Target->CanBeHit())
            {
                Target->TakeDamage(DamageInfo);
            }
        }
    });

    // 공격 몽타주 초기화
    auto InitAttackMontage = [](UAnimMontage*& OutMontage, const FString& AnimPath, const char* Name)
    {
        if (!AnimPath.empty())
        {
            UAnimSequence* Anim = UResourceManager::GetInstance().Get<UAnimSequence>(AnimPath);
            if (Anim)
            {
                OutMontage = NewObject<UAnimMontage>();
                OutMontage->SetSourceSequence(Anim);
                UE_LOG("[PlayerCharacter] %s initialized: %s", Name, AnimPath.c_str());
            }
            else
            {
                UE_LOG("[PlayerCharacter] Failed to find animation: %s", AnimPath.c_str());
            }
        }
    };

    InitAttackMontage(LightAttackMontage, LightAttackAnimPath, "LightAttackMontage");
    InitAttackMontage(HeavyAttackMontage, HeavyAttackAnimPath, "HeavyAttackMontage");
    InitAttackMontage(DashAttackMontage, DashAttackAnimPath, "DashAttackMontage");
    InitAttackMontage(UltimateAttackMontage, UltimateAttackAnimPath, "UltimateAttackMontage");

    // 피격 몽타주 초기화 (4방향: F, B, R, L)
    FString* HitPaths[4] = { &HitAnimPath_F, &HitAnimPath_B, &HitAnimPath_R, &HitAnimPath_L };
    const char* HitNames[4] = { "F", "B", "R", "L" };

    for (int32 i = 0; i < 4; ++i)
    {
        if (!HitPaths[i]->empty())
        {
            UAnimSequence* HitAnim = UResourceManager::GetInstance().Get<UAnimSequence>(*HitPaths[i]);
            if (HitAnim)
            {
                HitMontages[i] = NewObject<UAnimMontage>();
                HitMontages[i]->SetSourceSequence(HitAnim);
                UE_LOG("[PlayerCharacter] HitMontage[%s] initialized: %s", HitNames[i], HitPaths[i]->c_str());
            }
            else
            {
                UE_LOG("[PlayerCharacter] Failed to find hit animation: %s", HitPaths[i]->c_str());
            }
        }
    }

    // 사망 몽타주 초기화
    if (!DeathAnimPath.empty())
    {
        UAnimSequence* DeathAnim = UResourceManager::GetInstance().Get<UAnimSequence>(DeathAnimPath);
        if (DeathAnim)
        {
            DeathMontage = NewObject<UAnimMontage>();
            DeathMontage->SetSourceSequence(DeathAnim);
            UE_LOG("[PlayerCharacter] DeathMontage initialized: %s", DeathAnimPath.c_str());
        }
        else
        {
            UE_LOG("[PlayerCharacter] Failed to find death animation: %s", DeathAnimPath.c_str());
        }
    }

    // 구르기 몽타주 초기화 (8방향)
    FString* DodgePaths[8] = {
        &DodgeAnimPath_F, &DodgeAnimPath_FR, &DodgeAnimPath_R, &DodgeAnimPath_BR,
        &DodgeAnimPath_B, &DodgeAnimPath_BL, &DodgeAnimPath_L, &DodgeAnimPath_FL
    };
    const char* DodgeNames[8] = { "F", "FR", "R", "BR", "B", "BL", "L", "FL" };

    for (int32 i = 0; i < 8; ++i)
    {
        if (!DodgePaths[i]->empty())
        {
            UAnimSequence* DodgeAnim = UResourceManager::GetInstance().Get<UAnimSequence>(*DodgePaths[i]);
            if (DodgeAnim)
            {
                DodgeMontages[i] = NewObject<UAnimMontage>();
                DodgeMontages[i]->SetSourceSequence(DodgeAnim);
                UE_LOG("[PlayerCharacter] DodgeMontage[%s] initialized: %s", DodgeNames[i], DodgePaths[i]->c_str());
            }
            else
            {
                UE_LOG("[PlayerCharacter] Failed to find dodge animation: %s", DodgePaths[i]->c_str());
            }
        }
    }

    // 가드 몽타주 초기화 (루프)
    if (!BlockAnimPath.empty())
    {
        UAnimSequence* BlockAnim = UResourceManager::GetInstance().Get<UAnimSequence>(BlockAnimPath);
        if (BlockAnim)
        {
            BlockMontage = NewObject<UAnimMontage>();
            BlockMontage->SetSourceSequence(BlockAnim);
            BlockMontage->bLoop = true;  // 루프 설정
            UE_LOG("[PlayerCharacter] BlockMontage initialized (Loop): %s", BlockAnimPath.c_str());
        }
        else
        {
            UE_LOG("[PlayerCharacter] Failed to find block animation: %s", BlockAnimPath.c_str());
        }
    }

    // 가드 브레이크 몽타주 초기화 (넘어지는 모션)
    if (!GuardBreakAnimPath.empty())
    {
        UAnimSequence* GuardBreakAnim = UResourceManager::GetInstance().Get<UAnimSequence>(GuardBreakAnimPath);
        if (GuardBreakAnim)
        {
            GuardBreakMontage = NewObject<UAnimMontage>();
            GuardBreakMontage->SetSourceSequence(GuardBreakAnim);
            UE_LOG("[PlayerCharacter] GuardBreakMontage initialized: %s", GuardBreakAnimPath.c_str());
        }
        else
        {
            UE_LOG("[PlayerCharacter] Failed to find guard break animation: %s", GuardBreakAnimPath.c_str());
        }
    }

    // 차징 몽타주 초기화 (루프)
    if (!ChargingAnimPath.empty())
    {
        UAnimSequence* ChargingAnim = UResourceManager::GetInstance().Get<UAnimSequence>(ChargingAnimPath);
        if (ChargingAnim)
        {
            ChargingMontage = NewObject<UAnimMontage>();
            ChargingMontage->SetSourceSequence(ChargingAnim);
            ChargingMontage->bLoop = true;  // 루프 설정
            UE_LOG("[PlayerCharacter] ChargingMontage initialized (Loop): %s", ChargingAnimPath.c_str());
        }
        else
        {
            UE_LOG("[PlayerCharacter] Failed to find charging animation: %s", ChargingAnimPath.c_str());
        }
    }

    // 포션 몽타주 초기화
    if (!PotionAnimPath.empty())
    {
        UAnimSequence* PotionAnim = UResourceManager::GetInstance().Get<UAnimSequence>(PotionAnimPath);
        if (PotionAnim)
        {
            PotionMontage = NewObject<UAnimMontage>();
            PotionMontage->SetSourceSequence(PotionAnim);
            PotionMontage->bLoop = false;
            UE_LOG("[PlayerCharacter] PotionMontage initialized: %s", PotionAnimPath.c_str());
        }
        else
        {
            UE_LOG("[PlayerCharacter] Failed to find potion animation: %s", PotionAnimPath.c_str());
        }
    }

    GatherParticles();

    AGameModeBase* GM = GWorld->GetGameMode();
    if (!GM)
    {
        return;
    }

    GS = Cast<AGameState>(GM->GetGameState());
    if (!GS)
    {
        return;
    }
}

void APlayerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // SubWeapon 트랜스폼 업데이트 (PlayerCharacter만 사용)
    UpdateSubWeaponTransform();

    // PIE 모드일 때만 StatsComponent 틱 및 GameState 업데이트
    if (GWorld && GWorld->bPie)
    {
        // 매번 GetComponent로 StatsComponent 찾기 (포인터 손상 방지)
        UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
        if (Stats)
        {
            // StatsComponent 틱 (스태미나 회복 등)
            Stats->TickComponent(DeltaSeconds);

            // 차징 중이면 포커스 충전
            if (bIsCharging)
            {
                Stats->ChargeFocus(Stats->FocusChargeRate * DeltaSeconds);
            }

            // 막기 중이면 스태미나 강제 소모
            if (bIsBlocking)
            {
                Stats->DrainStamina(Stats->BlockDrainRate * DeltaSeconds);

                // 스태미나가 0이 되면 막기 강제 해제
                if (Stats->GetCurrentStamina() <= 0.f)
                {
                    UE_LOG("[PlayerCharacter] Block forced stop - stamina depleted");
                    StopBlock();
                }
            }

            // GameState에 스태미나/체력/포커스 실시간 업데이트
            if (AGameModeBase* GM = GWorld->GetGameMode())
            {
                if (AGameState* GS = Cast<AGameState>(GM->GetGameState()))
                {
                    GS->OnPlayerStaminaChanged(Stats->GetCurrentStamina(), Stats->GetMaxStamina());
                    GS->OnPlayerHealthChanged(Stats->GetCurrentHealth(), Stats->GetMaxHealth());
                    GS->OnPlayerFocusChanged(Stats->GetCurrentFocus(), Stats->GetMaxFocus());
                }
            }
        }
    }

    // Only process gameplay when in Fighting state
    if (AGameModeBase* GM = GWorld ? GWorld->GetGameMode() : nullptr)
    {
        if (AGameState* GS = Cast<AGameState>(GM->GetGameState()))
        {
            if (GS->GetGameFlowState() != EGameFlowState::Fighting)
            {
                return;
            }
        }
    }

    // 사망 상태면 입력 무시
    if (CombatState == ECombatState::Dead)
    {
        return;
    }

    // 공격 상태일 때 몽타주 종료 체크
    UpdateAttackState(DeltaSeconds);

    // 구르기 상태 업데이트
    UpdateDodgeState(DeltaSeconds);

    // 점프 딜레이 업데이트
    if (bJumpPending)
    {
        JumpDelayTimer += DeltaSeconds;
        if (JumpDelayTimer >= JumpDelayTime)
        {
            bJumpPending = false;
            JumpDelayTimer = 0.f;
            // 실제 점프 실행
            Super::Jump();
            UE_LOG("[PlayerCharacter] Jump executed after %.2fs delay", JumpDelayTime);
        }
    }

    // 착지 쿨다운 업데이트
    if (bLandingCooldown)
    {
        LandingCooldownTimer += DeltaSeconds;
        if (LandingCooldownTimer >= LandingCooldownTime)
        {
            bLandingCooldown = false;
            LandingCooldownTimer = 0.f;
            UE_LOG("[PlayerCharacter] Landing cooldown finished");
        }
    }

    // Idle 상태 타이머 업데이트
    if (CombatState == ECombatState::Idle)
    {
        IdleStateTimer += DeltaSeconds;
    }

    // 경직 상태 업데이트
    UpdateStagger(DeltaSeconds);

    // 패리 윈도우 업데이트
    UpdateParryWindow(DeltaSeconds);

    // 스킬 차징 업데이트
    UpdateSkillCharging(DeltaSeconds);

    // 이동 상태 업데이트 (Walking/Running/Jumping/Idle 전환)
    UpdateMovementState(DeltaSeconds);

    // 경직 중이 아니면 입력 처리
    if (CombatState != ECombatState::Staggered)
    {
        ProcessInput(DeltaSeconds);
    }

    UpdateEffect(DeltaSeconds);

    // 포션 마시기 완료 체크 (상체 분리 모드가 해제되면 HP 회복)
    if (bIsDrinkingPotion)
    {
        USkeletalMeshComponent* Mesh = GetMesh();
        if (Mesh)
        {
            UAnimInstance* AnimInst = Mesh->GetAnimInstance();
            if (AnimInst && !AnimInst->IsUpperBodySplitEnabled())
            {
                // 포션 몽타주가 끝나서 상체 분리가 해제됨 -> HP 회복
                bIsDrinkingPotion = false;

                UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
                if (Stats)
                {
                    Stats->Heal(PotionHealAmount);
                    UE_LOG("[PlayerCharacter] Potion consumed! Healed %.0f HP (Current: %.0f/%.0f)",
                           PotionHealAmount, Stats->GetCurrentHealth(), Stats->GetMaxHealth());
                }
            }
        }
    }
}

// ============================================================================
// 입력 처리
// ============================================================================

void APlayerCharacter::ProcessInput(float DeltaTime)
{
  //  ProcessMovementInput(DeltaTime);
    ProcessCombatInput();
}

void APlayerCharacter::ProcessMovementInput(float DeltaTime)
{
    // 공격/회피 중에는 이동 불가
    if (CombatState == ECombatState::Attacking || CombatState == ECombatState::Dodging)
    {
        return;
    }

    FVector MoveDirection = FVector();

    if (INPUT.IsKeyDown('W')) MoveDirection.Y += 1.f;
    if (INPUT.IsKeyDown('S')) MoveDirection.Y -= 1.f;
    if (INPUT.IsKeyDown('A')) MoveDirection.X -= 1.f;
    if (INPUT.IsKeyDown('D')) MoveDirection.X += 1.f;

    if (MoveDirection.SizeSquared() > 0.f)
    {
        MoveDirection.Normalize();
        FVector NewLocation = GetActorLocation() + MoveDirection * MoveSpeed * DeltaTime;
        SetActorLocation(NewLocation);

        // 이동 방향으로 회전
        // FQuat TargetRotation = FQuat::LookAt(MoveDirection, FVector::UpVector);
        // SetActorRotation(TargetRotation);
    }
}

void APlayerCharacter::ProcessCombatInput()
{
    // 입력은 PlayerController에서 InputComponent를 통해 처리됨
    // - 마우스 좌클릭: Attack -> OnAttack -> LightAttack
    // - 마우스 우클릭: Block -> OnStartBlock/OnStopBlock -> StartBlock/StopBlock
    // - 스페이스: Dodge -> OnDodge -> Dodge
    // - Y키: Charging -> OnStartCharging/OnStopCharging -> StartCharging/StopCharging

    // I키: 무적 모드 토글 (디버그용)
    static bool bIKeyWasPressed = false;
    bool bIKeyIsPressed = INPUT.IsKeyDown('I');

    if (bIKeyIsPressed && !bIKeyWasPressed)
    {
        // I키가 눌린 순간 (토글)
        bIsInvincible = !bIsInvincible;
        UE_LOG("[PlayerCharacter] Invincible mode %s", bIsInvincible ? "ENABLED" : "DISABLED");
    }
    bIKeyWasPressed = bIKeyIsPressed;

    // N키: 보스 체력 250 깎기 (디버그용)
    static bool bNKeyWasPressed = false;
    bool bNKeyIsPressed = INPUT.IsKeyDown('N');

    if (bNKeyIsPressed && !bNKeyWasPressed)
    {
        // 보스 찾기
        if (GWorld)
        {
            for (AActor* Actor : GWorld->GetActors())
            {
                ABossEnemy* Boss = Cast<ABossEnemy>(Actor);
                if (Boss && Boss->IsAlive())
                {
                    if (UStatsComponent* Stats = Boss->GetStatsComponent())
                    {
                        float OldHP = Stats->CurrentHealth;
                        Stats->CurrentHealth = FMath::Max(0.0f, Stats->CurrentHealth - 250.0f);
                        UE_LOG("[PlayerCharacter Debug] N key - Boss HP: %.1f -> %.1f", OldHP, Stats->CurrentHealth);
                    }
                    break;
                }
            }
        }
    }
    bNKeyWasPressed = bNKeyIsPressed;

    // U키: 커서 토글 (디버그용, PIE에서만)
    static bool bUKeyWasPressed = false;
    bool bUKeyIsPressed = INPUT.IsKeyDown('U');

    if (bUKeyIsPressed && !bUKeyWasPressed)
    {
        if (GWorld && GWorld->bPie)
        {
            static bool bCursorIsVisible = false;
            bCursorIsVisible = !bCursorIsVisible;
            UInputManager::GetInstance().SetCursorVisible(bCursorIsVisible);
            UInputManager::GetInstance().ReleaseCursor();
        }
    }

    bUKeyWasPressed = bUKeyIsPressed;
}

// ============================================================================
// 전투 액션
// ============================================================================

void APlayerCharacter::LightAttack()
{
    UE_LOG("[PlayerCharacter] LightAttack() called - CombatState: %d, bCanCombo: %s",
           static_cast<int>(CombatState), bCanCombo ? "true" : "false");

    // 공격 중 + 콤보 가능 상태에서 호출되면 HeavyAttack 버퍼링
    if (CombatState == ECombatState::Attacking && bCanCombo)
    {
        bBufferedHeavyAttack = true;
        UE_LOG("[PlayerCharacter] HeavyAttack buffered for combo");
        return;
    }

    // 상태 체크: 공격 중이고 콤보 불가능하면 리턴
    if (CombatState == ECombatState::Attacking && !bCanCombo)
    {
        UE_LOG("[PlayerCharacter] LightAttack() blocked - already attacking, no combo");
        return;
    }
    if (CombatState == ECombatState::Staggered ||
        CombatState == ECombatState::Dead ||
        CombatState == ECombatState::Dodging)
    {
        UE_LOG("[PlayerCharacter] LightAttack() blocked - staggered, dead or dodging");
        return;
    }

    // StatsComponent 찾기
    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats)
    {
        UE_LOG("[PlayerCharacter] LightAttack() blocked - no StatsComponent");
        return;
    }

    // 스태미나 체크
    if (!Stats->ConsumeStamina(Stats->LightAttackCost))
    {
        UE_LOG("[PlayerCharacter] LightAttack() blocked - not enough stamina");
        return; // 스태미나 부족
    }

    UE_LOG("[PlayerCharacter] LightAttack() executing - playing montage");

    // 가드 해제
    StopBlock();

    SetCombatState(ECombatState::Attacking);

    // 콤보 타이머 초기화
    LightAttackTimer = 0.f;

    // 콤보 카운트 증가
    if (bCanCombo)
    {
        ComboCount = (ComboCount + 1) % MaxComboCount;
    }
    else
    {
        ComboCount = 0;
    }

    // 데미지 정보 설정 (노티파이에서 StartWeaponTrace 호출)
    FDamageInfo DamageInfo(this, 10.f + ComboCount * 5.f, EDamageType::Light);
    DamageInfo.StaggerDuration = 0.2f + ComboCount * 0.1f;  // 콤보가 높을수록 경직 증가
    SetWeaponDamageInfo(DamageInfo);

    // 공격 애니메이션 재생 (몽타주)
    if (LightAttackMontage)
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                // 루트 모션 활성화 및 애니메이션 끝 자르기 시간 설정
                AnimInst->SetRootMotionEnabled(bEnableLightAttackRootMotion);
                AnimInst->SetAnimationCutEndTime(LightAttackCutEndTime);

                AnimInst->Montage_Play(LightAttackMontage, 0.1f, 0.2f, 1.0f);  // BlendOut 0.2초
                UE_LOG("[PlayerCharacter] Playing LightAttack montage (RootMotion: %s, CutEndTime: %.3fs)",
                    bEnableLightAttackRootMotion ? "ON" : "OFF", LightAttackCutEndTime);
            }
        }
    }

    bCanCombo = false;
}

void APlayerCharacter::HeavyAttack()
{
    if (CombatState == ECombatState::Attacking ||
        CombatState == ECombatState::Staggered ||
        CombatState == ECombatState::Dead ||
        CombatState == ECombatState::Dodging)
    {
        return;
    }

    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats || !Stats->ConsumeStamina(Stats->HeavyAttackCost))
    {
        return;
    }

    StopBlock();
    SetCombatState(ECombatState::Attacking);
    ComboCount = 0;

    // 데미지 정보 설정 (노티파이에서 StartWeaponTrace 호출)
    FDamageInfo DamageInfo(this, 30.f, EDamageType::Heavy);
    DamageInfo.HitReaction = EHitReaction::Stagger;
    DamageInfo.StaggerDuration = 0.5f;
    DamageInfo.KnockbackForce = 200.f;
    SetWeaponDamageInfo(DamageInfo);

    // 강공격 애니메이션 재생 (몽타주)
    if (HeavyAttackMontage)
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                AnimInst->SetRootMotionEnabled(bEnableHeavyAttackRootMotion);
                AnimInst->SetAnimationCutEndTime(HeavyAttackCutEndTime);

                AnimInst->Montage_Play(HeavyAttackMontage, 0.1f, 0.1f, 1.0f);
                UE_LOG("[PlayerCharacter] Playing HeavyAttack montage (RootMotion: %s, CutEndTime: %.3fs)",
                    bEnableHeavyAttackRootMotion ? "ON" : "OFF", HeavyAttackCutEndTime);
            }
        }
    }
}

void APlayerCharacter::DashAttack()
{
    UE_LOG("[PlayerCharacter] DashAttack() called - CombatState: %d", static_cast<int>(CombatState));

    // 이미 차징 중이면 무시
    if (PendingSkillType != 0)
    {
        UE_LOG("[PlayerCharacter] DashAttack() blocked - already charging");
        return;
    }

    // 상태 체크
    if (CombatState == ECombatState::Attacking ||
        CombatState == ECombatState::Staggered ||
        CombatState == ECombatState::Dead ||
        CombatState == ECombatState::Dodging)
    {
        UE_LOG("[PlayerCharacter] DashAttack() blocked - invalid state");
        return;
    }

    // StatsComponent 찾기
    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats)
    {
        UE_LOG("[PlayerCharacter] DashAttack() blocked - no StatsComponent");
        return;
    }

    // 포커스 50 이상 필요
    if (Stats->GetCurrentFocus() < 50.f)
    {
        UE_LOG("[PlayerCharacter] DashAttack() blocked - not enough focus (need 50, have %.0f)",
               Stats->GetCurrentFocus());
        return;
    }

    // 차징 시작 (포커스는 실제 공격 시 소모)
    StartSkillCharging(1);  // 1 = DashAttack
}

void APlayerCharacter::UltimateAttack()
{
    UE_LOG("[PlayerCharacter] UltimateAttack() called - CombatState: %d", static_cast<int>(CombatState));

    // 이미 차징 중이면 무시
    if (PendingSkillType != 0)
    {
        UE_LOG("[PlayerCharacter] UltimateAttack() blocked - already charging");
        return;
    }

    // 상태 체크
    if (CombatState == ECombatState::Attacking ||
        CombatState == ECombatState::Staggered ||
        CombatState == ECombatState::Dead ||
        CombatState == ECombatState::Dodging)
    {
        UE_LOG("[PlayerCharacter] UltimateAttack() blocked - invalid state");
        return;
    }

    // StatsComponent 찾기
    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats)
    {
        UE_LOG("[PlayerCharacter] UltimateAttack() blocked - no StatsComponent");
        return;
    }

    // 포커스 100 이상 필요 (최대치)
    if (Stats->GetCurrentFocus() < 100.f)
    {
        UE_LOG("[PlayerCharacter] UltimateAttack() blocked - not enough focus (need 100, have %.0f)",
               Stats->GetCurrentFocus());
        return;
    }

    // 차징 시작 (포커스는 실제 공격 시 소모)
    StartSkillCharging(2);  // 2 = UltimateAttack
}

void APlayerCharacter::Dodge()
{
    if (CombatState == ECombatState::Dodging ||
        CombatState == ECombatState::Staggered ||
        CombatState == ECombatState::Dead)
    {
        return;
    }

    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats || !Stats->ConsumeStamina(Stats->DodgeCost))
    {
        return;
    }

    StopBlock();
    SetCombatState(ECombatState::Dodging);
    // Dodge는 피격 모션만 무시, 데미지는 받음 (bIsInvincible 설정 안 함)

    // 입력 방향에 따라 8방향 구르기 몽타주 선택
    int32 DodgeIndex = GetDodgeDirectionIndex();
    UAnimMontage* SelectedMontage = DodgeMontages[DodgeIndex];

    if (SelectedMontage)
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                // 루트 모션 활성화 및 애니메이션 끝 자르기 시간 설정
                AnimInst->SetRootMotionEnabled(bEnableDodgeRootMotion);
                AnimInst->SetAnimationCutEndTime(DodgeAnimationCutEndTime);

                AnimInst->Montage_Play(SelectedMontage, 0.1f, 0.1f, 1.0f);
                const char* DirNames[8] = { "F", "FR", "R", "BR", "B", "BL", "L", "FL" };
                UE_LOG("[PlayerCharacter] Playing Dodge montage: %s (RootMotion: %s, CutEndTime: %.3fs)",
                    DirNames[DodgeIndex], bEnableDodgeRootMotion ? "ON" : "OFF", DodgeAnimationCutEndTime);
            }
        }
    }
    else
    {
        // 몽타주가 없으면 기본 Forward로 폴백
        if (DodgeMontages[0])
        {
            if (USkeletalMeshComponent* Mesh = GetMesh())
            {
                if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                {
                    // 루트 모션 활성화 및 애니메이션 끝 자르기 시간 설정
                    AnimInst->SetRootMotionEnabled(bEnableDodgeRootMotion);
                    AnimInst->SetAnimationCutEndTime(DodgeAnimationCutEndTime);

                    AnimInst->Montage_Play(DodgeMontages[0], 0.1f, 0.1f, 1.0f);
                    UE_LOG("[PlayerCharacter] Playing Dodge montage: F (fallback, RootMotion: %s, CutEndTime: %.3fs)",
                        bEnableDodgeRootMotion ? "ON" : "OFF", DodgeAnimationCutEndTime);
                }
            }
        }
    }
}

void APlayerCharacter::StartBlock()
{
    if (CombatState == ECombatState::Attacking ||
        CombatState == ECombatState::Dodging ||
        CombatState == ECombatState::Dead)
    {
        return;
    }

    // 이미 가드 중이면 무시
    if (bIsBlocking)
    {
        return;
    }

    // 스태미나 최소 요구량 체크
    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats || Stats->GetCurrentStamina() < Stats->BlockMinRequired)
    {
        UE_LOG("[PlayerCharacter] Cannot block - not enough stamina (need %.0f, have %.0f)",
               Stats ? Stats->BlockMinRequired : 0.f, Stats ? Stats->GetCurrentStamina() : 0.f);
        return;
    }

    bIsBlocking = true;
    SetCombatState(ECombatState::Blocking);

    // 스태미나 회복 중지
    Stats->PauseStaminaRegen();

    // 패리 윈도우 시작
    bIsParrying = true;
    ParryWindowTimer = ParryWindowDuration;

    // 가드 몽타주 재생 (루프)
    if (BlockMontage)
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                AnimInst->Montage_Play(BlockMontage, 0.1f, 0.1f, 1.0f, true);  // bLoop = true
                UE_LOG("[PlayerCharacter] Playing Block montage (Loop)");
            }
        }
    }
}

void APlayerCharacter::StopBlock()
{
    if (!bIsBlocking)
    {
        return;
    }

    bIsBlocking = false;
    bIsParrying = false;
    ParryWindowTimer = 0.f;

    // 스태미나 회복 재개
    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (Stats)
    {
        Stats->ResumeStaminaRegen();
    }

    // 가드 몽타주 정지
    if (USkeletalMeshComponent* Mesh = GetMesh())
    {
        if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
        {
            AnimInst->Montage_Stop(0.1f);  // 블렌드 아웃
            UE_LOG("[PlayerCharacter] Stopping Block montage");
        }
    }

    if (CombatState == ECombatState::Blocking)
    {
        SetCombatState(ECombatState::Idle);
    }
}

void APlayerCharacter::StartCharging()
{
    if (CombatState == ECombatState::Attacking ||
        CombatState == ECombatState::Dodging ||
        CombatState == ECombatState::Dead)
    {
        return;
    }

    // 이미 차징 중이면 무시
    if (bIsCharging)
    {
        return;
    }

    // 가드 중이면 해제
    if (bIsBlocking)
    {
        StopBlock();
    }

    bIsCharging = true;

    // 차징 몽타주 재생 (루프)
    if (ChargingMontage)
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                AnimInst->Montage_Play(ChargingMontage, 0.1f, 0.1f, 1.0f, true);  // bLoop = true
                UE_LOG("[PlayerCharacter] Playing Charging montage (Loop)");
            }
        }
    }
}

void APlayerCharacter::StopCharging()
{
    if (!bIsCharging)
    {
        return;
    }

    bIsCharging = false;

    // 차징 몽타주 정지
    if (USkeletalMeshComponent* Mesh = GetMesh())
    {
        if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
        {
            AnimInst->Montage_Stop(0.1f);  // 블렌드 아웃
            UE_LOG("[PlayerCharacter] Stopping Charging montage");
        }
    }
}

void APlayerCharacter::DrinkPotion()
{
    // Idle 또는 Walk 상태에서만 가능
    if (CombatState != ECombatState::Idle && CombatState != ECombatState::Walking)
    {
        UE_LOG("[PlayerCharacter] DrinkPotion: Invalid state (current: %d)", static_cast<int32>(CombatState));
        return;
    }

    // 이미 몽타주 재생 중이면 무시
    USkeletalMeshComponent* Mesh = GetMesh();
    if (!Mesh) return;

    UAnimInstance* AnimInst = Mesh->GetAnimInstance();
    if (!AnimInst) return;

    if (AnimInst->Montage_IsPlaying())
    {
        UE_LOG("[PlayerCharacter] DrinkPotion: Montage already playing");
        return;
    }

    if (!PotionMontage)
    {
        UE_LOG("[PlayerCharacter] DrinkPotion: PotionMontage is null");
        return;
    }

    // 상체 분리 활성화
    FName BoneName(UpperBodyRootBoneName.c_str());
    AnimInst->EnableUpperBodySplit(BoneName);

    // 포션 마시는 중 플래그 설정
    bIsDrinkingPotion = true;

    // 포션 몽타주 재생 (상체만 적용됨)
    AnimInst->Montage_Play(PotionMontage, 0.1f, 0.2f, 1.0f, false);
    UE_LOG("[PlayerCharacter] DrinkPotion: Playing potion montage (upper body only)");
}

void APlayerCharacter::EnableComboWindow()
{
    bCanCombo = true;
    UE_LOG("[PlayerCharacter] Combo window enabled");
}

void APlayerCharacter::DisableComboWindow()
{
    bCanCombo = false;
    UE_LOG("[PlayerCharacter] Combo window disabled");
}

// ============================================================================
// IDamageable 구현
// ============================================================================

float APlayerCharacter::TakeDamage(const FDamageInfo& DamageInfo)
{
    if (!CanBeHit())
    {
        return 0.f;
    }

    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats)
    {
        return 0.f;
    }

    float ActualDamage = DamageInfo.Damage;

    // 가드 브레이크 처리 - 가드 중에 GuardBreak 공격이 들어오면 가드 무력화
    if (bIsBlocking && DamageInfo.DamageType == EDamageType::GuardBreak)
    {
        UE_LOG("[PlayerCharacter] Guard Break! Block broken.");

        // 가드 해제
        StopBlock();

        // 상태 변경
        SetCombatState(ECombatState::Staggered);
        StaggerTimer = DamageInfo.StaggerDuration;

        // 가드 브레이크 몽타주 재생 (넘어지는 모션)
        if (GuardBreakMontage)
        {
            if (USkeletalMeshComponent* Mesh = GetMesh())
            {
                if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                {
                    AnimInst->SetRootMotionEnabled(bEnableGuardBreakRootMotion);
                    AnimInst->SetAnimationCutEndTime(GuardBreakCutEndTime);
                    AnimInst->Montage_Play(GuardBreakMontage, 0.1f, 0.2f, 1.0f);
                    UE_LOG("[PlayerCharacter] Playing GuardBreak montage");
                }
            }
        }

        // 데미지 적용
        Stats->ApplyDamage(ActualDamage);

        // 사망 체크
        if (!Stats->IsAlive())
        {
            OnDeath();
        }

        return ActualDamage;
    }

    // 일반 가드 중이면 데미지 완전 막기
    if (bIsBlocking && DamageInfo.bCanBeBlocked)
    {
        Stats->ConsumeStamina(Stats->BlockCostPerHit);
        UE_LOG("[PlayerCharacter] Attack blocked! Damage negated.");
        return 0.f;  // 데미지 0
    }

    Stats->ApplyDamage(ActualDamage);

    // 사망 체크
    if (!Stats->IsAlive())
    {
        OnDeath();
        return ActualDamage;
    }

    // 피격 반응 (가드 중이 아니고, Dodging 중이 아닐 때만)
    if (!bIsBlocking && CombatState != ECombatState::Dodging)
    {
        OnHitReaction(DamageInfo.HitReaction, DamageInfo);
    }

    return ActualDamage;
}

bool APlayerCharacter::IsAlive() const
{
    UStatsComponent* Stats = Cast<UStatsComponent>(const_cast<APlayerCharacter*>(this)->GetComponent(UStatsComponent::StaticClass()));
    return Stats && Stats->IsAlive();
}

bool APlayerCharacter::CanBeHit() const
{
    return IsAlive() && !bIsInvincible;
}

bool APlayerCharacter::IsBlocking() const
{
    return bIsBlocking;
}

bool APlayerCharacter::IsParrying() const
{
    return bIsParrying;
}

void APlayerCharacter::OnHitReaction(EHitReaction Reaction, const FDamageInfo& DamageInfo)
{
    if (Reaction == EHitReaction::None)
    {
        return;
    }

    // 대쉬 공격/궁극기 사용 중에는 피격 몽타주 스킵 (슈퍼아머) - 실제 공격 중일 때만
    if (CombatState == ECombatState::Attacking)
    {
        EDamageType CurrentAttackType = GetWeaponDamageInfo().DamageType;
        if (CurrentAttackType == EDamageType::DashAttack || CurrentAttackType == EDamageType::UltimateAttack)
        {
            UE_LOG("[PlayerCharacter] Hit reaction skipped - %s has super armor",
                CurrentAttackType == EDamageType::DashAttack ? "DashAttack" : "UltimateAttack");
            return;
        }
    }

    // 현재 공격/회피 중단
    EndWeaponTrace();
    bIsInvincible = false;

    SetCombatState(ECombatState::Staggered);
    StaggerTimer = DamageInfo.StaggerDuration;

    // 피격 방향 계산 (0=F, 1=B, 2=R, 3=L)
    int32 HitDirIndex = 0;  // 기본값: Forward
    if (!DamageInfo.HitDirection.IsZero())
    {
        // HitDirection을 캐릭터 로컬 좌표계로 변환
        FQuat ActorRot = GetActorRotation();
        FVector LocalHitDir = ActorRot.Inverse().RotateVector(DamageInfo.HitDirection);
        LocalHitDir.Z = 0.f;
        LocalHitDir.Normalize();

        // 방향 결정
        float ForwardDot = LocalHitDir.X;  // Forward = +X
        float RightDot = LocalHitDir.Y;    // Right = +Y

        if (FMath::Abs(ForwardDot) > FMath::Abs(RightDot))
        {
            // 앞/뒤
            HitDirIndex = (ForwardDot > 0.f) ? 0 : 1;  // F or B
        }
        else
        {
            // 좌/우
            HitDirIndex = (RightDot > 0.f) ? 2 : 3;  // R or L
        }
    }

    // 피격 애니메이션 재생
    if (HitMontages[HitDirIndex])
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                AnimInst->SetRootMotionEnabled(bEnableHitRootMotion);
                AnimInst->SetAnimationCutEndTime(HitCutEndTime);
                AnimInst->Montage_Play(HitMontages[HitDirIndex], 0.05f, 0.1f, 1.0f);
                const char* DirNames[4] = { "F", "B", "R", "L" };
                UE_LOG("[PlayerCharacter] Playing Hit montage: %s", DirNames[HitDirIndex]);
            }
        }
    }

    // 넉백 적용
    if (Reaction == EHitReaction::Knockback && DamageInfo.KnockbackForce > 0.f)
    {
        FVector KnockbackDir = DamageInfo.HitDirection * DamageInfo.KnockbackForce;
        AddActorWorldLocation(KnockbackDir * 0.1f); // 간단한 넉백
    }
}

void APlayerCharacter::OnDeath()
{
    SetCombatState(ECombatState::Dead);
    EndWeaponTrace();

    // 사망 애니메이션 재생
    if (DeathMontage)
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                AnimInst->SetRootMotionEnabled(bEnableDeathRootMotion);
                AnimInst->SetAnimationCutEndTime(DeathCutEndTime);
                AnimInst->Montage_Play(DeathMontage, 0.1f, 0.0f, 1.0f);  // BlendOut 없음
                UE_LOG("[PlayerCharacter] Playing Death montage");
            }
        }
    }
}

// ============================================================================
// 내부 함수
// ============================================================================

void APlayerCharacter::SetCombatState(ECombatState NewState)
{
    ECombatState OldState = CombatState;
    CombatState = NewState;

    // Idle 상태 진입 시 타이머 초기화
    if (NewState == ECombatState::Idle && OldState != ECombatState::Idle)
    {
        IdleStateTimer = 0.f;
    }

    // 상태 변경 시 처리
    if (OldState == ECombatState::Attacking && NewState != ECombatState::Attacking)
    {
        // 무기 Sweep 종료
        EndWeaponTrace();
        // 무적 해제
        bIsInvincible = false;
        // 콤보 관련 초기화
        bCanCombo = false;
        bBufferedHeavyAttack = false;
    }

    // 닷지 종료 시 무적 해제
    if (OldState == ECombatState::Dodging && NewState != ECombatState::Dodging)
    {
        bIsInvincible = false;
    }

    // ── 엘든링 스타일 카메라: SpringArm에 구르기 상태 전달 ──
    bool bIsNowDodging = (NewState == ECombatState::Dodging);
    bool bWasDodging = (OldState == ECombatState::Dodging);
    if (bIsNowDodging != bWasDodging)
    {
        if (UActorComponent* C = GetComponent(USpringArmComponent::StaticClass()))
        {
            if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(C))
            {
                SpringArm->SetRollingState(bIsNowDodging);
            }
        }
    }
}

void APlayerCharacter::UpdateParryWindow(float DeltaTime)
{
    if (bIsParrying && ParryWindowTimer > 0.f)
    {
        ParryWindowTimer -= DeltaTime;
        if (ParryWindowTimer <= 0.f)
        {
            bIsParrying = false;
        }
    }
}

void APlayerCharacter::UpdateAttackState(float DeltaTime)
{
    // 공격 상태일 때만 체크
    if (CombatState != ECombatState::Attacking)
    {
        return;
    }

    // LightAttack 타이머 증가
    LightAttackTimer += DeltaTime;

    // 버퍼된 HeavyAttack이 있고, ComboTransitionTime에 도달하면 HeavyAttack으로 전환
    if (bBufferedHeavyAttack && LightAttackTimer >= ComboTransitionTime)
    {
        bBufferedHeavyAttack = false;
        bCanCombo = false;
        UE_LOG("[PlayerCharacter] Combo transition at %.2fs -> HeavyAttack", LightAttackTimer);

        // 스태미나 체크
        UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
        if (!Stats || !Stats->ConsumeStamina(Stats->HeavyAttackCost))
        {
            UE_LOG("[PlayerCharacter] Combo HeavyAttack blocked - not enough stamina");
            return;
        }

        // CombatState는 Attacking 유지, 데미지 타입만 Heavy로 변경
        ComboCount = 0;

        FDamageInfo DamageInfo(this, 30.f, EDamageType::Heavy);
        DamageInfo.HitReaction = EHitReaction::Stagger;
        DamageInfo.StaggerDuration = 0.5f;
        DamageInfo.KnockbackForce = 200.f;
        SetWeaponDamageInfo(DamageInfo);

        // HeavyAttack 몽타주 바로 재생
        if (HeavyAttackMontage)
        {
            if (USkeletalMeshComponent* Mesh = GetMesh())
            {
                if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                {
                    AnimInst->SetRootMotionEnabled(bEnableHeavyAttackRootMotion);
                    AnimInst->SetAnimationCutEndTime(HeavyAttackCutEndTime);
                    AnimInst->Montage_Play(HeavyAttackMontage, 0.0f, 0.3f, 1.0f);
                    UE_LOG("[PlayerCharacter] Combo -> HeavyAttack montage (BlendIn: 0.2s)");
                }
            }
        }
        return;
    }

    // 몽타주가 끝났는지 확인
    if (USkeletalMeshComponent* Mesh = GetMesh())
    {
        if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
        {
            // 몽타주가 재생 중이 아니면 공격 종료
            if (!AnimInst->Montage_IsPlaying())
            {
                SetCombatState(ECombatState::Idle);
                UE_LOG("[PlayerCharacter] Attack finished, returning to Idle");
            }
        }
    }
}

void APlayerCharacter::UpdateStagger(float DeltaTime)
{
    if (CombatState == ECombatState::Staggered && StaggerTimer > 0.f)
    {
        StaggerTimer -= DeltaTime;
        if (StaggerTimer <= 0.f)
        {
            SetCombatState(ECombatState::Idle);
        }
    }
}

// ============================================================================
// 콜백
// ============================================================================

void APlayerCharacter::HandleHealthChanged(float CurrentHealth, float MaxHealth)
{
    // UI 업데이트는 UI 클래스에서 델리게이트 바인딩해서 처리
}

void APlayerCharacter::HandleStaminaChanged(float CurrentStamina, float MaxStamina)
{
    // UI 업데이트는 UI 클래스에서 델리게이트 바인딩해서 처리
}

void APlayerCharacter::HandleDeath()
{
    OnDeath();
}

int32 APlayerCharacter::GetDodgeDirectionIndex() const
{
    // 캐릭터의 실제 이동 속도(Local Velocity) 기준으로 방향 결정
    FVector LocalVel = FVector::Zero();

    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        // 월드 Velocity를 캐릭터 로컬 좌표계로 변환
        FVector WorldVel = Movement->GetVelocity();
        WorldVel.Z = 0.0f;  // 수평 방향만 고려

        // 캐릭터 회전의 역방향으로 회전시켜 로컬 좌표계로 변환
        FQuat ActorRot = GetActorRotation();
        LocalVel = ActorRot.Inverse().RotateVector(WorldVel);
    }

    // 속도가 없으면 Forward (0)
    if (LocalVel.IsZero() || LocalVel.Size() < 0.1f)
    {
        return 0;
    }

    // 각도 계산 (atan2: Y, X 순서) - 결과는 -PI ~ PI
    // Forward(+X) = 0도, Right(+Y) = 90도, Backward(-X) = 180도, Left(-Y) = -90도
    float AngleRad = std::atan2(LocalVel.Y, LocalVel.X);
    float AngleDeg = AngleRad * (180.0f / PI);

    // 8방향 인덱스 계산 (각 방향은 45도 간격)
    // 0=F(0°), 1=FR(45°), 2=R(90°), 3=BR(135°), 4=B(180°/-180°), 5=BL(-135°), 6=L(-90°), 7=FL(-45°)

    // 각도를 0~360 범위로 변환
    if (AngleDeg < 0) AngleDeg += 360.0f;

    // 22.5도 오프셋을 더해서 각 방향의 중심이 경계가 되도록 함
    AngleDeg += 22.5f;
    if (AngleDeg >= 360.0f) AngleDeg -= 360.0f;

    // 45도 단위로 나눠서 인덱스 계산
    int32 Index = static_cast<int32>(AngleDeg / 45.0f);
    Index = FMath::Clamp(Index, 0, 7);

    return Index;
}

void APlayerCharacter::Jump()
{
    // Idle, Walking, Running 상태에서만 점프 가능
    if (CombatState != ECombatState::Idle &&
        CombatState != ECombatState::Walking &&
        CombatState != ECombatState::Running)
    {
        return;
    }

    // Idle 상태에서는 0.5초 경과 후에만 점프 가능
    if (CombatState == ECombatState::Idle && IdleStateTimer < IdleJumpDelayTime)
    {
        return;
    }

    // 이미 점프 대기 중이면 무시
    if (bJumpPending)
    {
        return;
    }

    // 착지 쿨다운 중이면 무시
    if (bLandingCooldown)
    {
        UE_LOG("[PlayerCharacter] Jump blocked - landing cooldown active");
        return;
    }

    // 공중에 있으면 무시
    UCharacterMovementComponent* MovementComp = GetCharacterMovement();
    if (MovementComp && MovementComp->IsFalling())
    {
        return;
    }

    // 점프 딜레이 시작
    bJumpPending = true;
    JumpDelayTimer = 0.f;
    UE_LOG("[PlayerCharacter] Jump requested, waiting %.2fs", JumpDelayTime);
}

void APlayerCharacter::UpdateDodgeState(float DeltaTime)
{
    // 구르기 상태일 때만 체크
    if (CombatState != ECombatState::Dodging)
    {
        return;
    }

    // 몽타주가 끝났는지 확인
    if (USkeletalMeshComponent* Mesh = GetMesh())
    {
        if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
        {
            // 몽타주가 재생 중이 아니면 구르기 종료
            if (!AnimInst->Montage_IsPlaying())
            {
                bIsInvincible = false;
                SetCombatState(ECombatState::Idle);
                UE_LOG("[PlayerCharacter] Dodge finished, returning to Idle");
            }
        }
    }
}

// ============================================================================
// 스킬 차징 시스템
// ============================================================================

void APlayerCharacter::StartSkillCharging(int32 SkillType)
{
    UE_LOG("[PlayerCharacter] StartSkillCharging - SkillType: %d", SkillType);

    // 가드 해제
    StopBlock();

    // 차징 상태 초기화
    PendingSkillType = SkillType;
    ChargingLoopCount = 0;
    ChargingLoopTimer = 0.f;

    // 차징 몽타주 재생
    if (ChargingMontage)
    {
        if (USkeletalMeshComponent* Mesh = GetMesh())
        {
            if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
            {
                AnimInst->Montage_Play(ChargingMontage, 0.1f, 0.1f, 1.0f, true);  // bLoop = true
                UE_LOG("[PlayerCharacter] Playing Charging montage for skill (Loop target: %d)", ChargingLoopTarget);
            }
        }
    }
}

void APlayerCharacter::UpdateSkillCharging(float DeltaTime)
{
    // 스킬 대기 중이 아니면 스킵
    if (PendingSkillType == 0)
    {
        return;
    }

    // 타이머 증가
    ChargingLoopTimer += DeltaTime;

    // 루프 1회 완료 체크
    if (ChargingLoopTimer >= ChargingLoopDuration)
    {
        ChargingLoopCount++;
        ChargingLoopTimer -= ChargingLoopDuration;
        UE_LOG("[PlayerCharacter] Charging loop %d/%d completed", ChargingLoopCount, ChargingLoopTarget);

        // 목표 루프 횟수 도달
        if (ChargingLoopCount >= ChargingLoopTarget)
        {
            // 차징 몽타주 정지
            if (USkeletalMeshComponent* Mesh = GetMesh())
            {
                if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                {
                    AnimInst->Montage_Stop(0.1f);
                }
            }

            // 스킬 실행
            ExecutePendingSkill();
        }
    }
}

void APlayerCharacter::ExecutePendingSkill()
{
    int32 SkillType = PendingSkillType;
    PendingSkillType = 0;  // 초기화

    UE_LOG("[PlayerCharacter] ExecutePendingSkill - SkillType: %d", SkillType);

    // StatsComponent 찾기
    UStatsComponent* Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    if (!Stats)
    {
        UE_LOG("[PlayerCharacter] ExecutePendingSkill blocked - no StatsComponent");
        return;
    }

    if (SkillType == 1)  // DashAttack
    {
        // 포커스 50 소모
        if (!Stats->ConsumeFocus(50.f))
        {
            UE_LOG("[PlayerCharacter] ExecutePendingSkill (DashAttack) blocked - ConsumeFocus failed");
            return;
        }

        UE_LOG("[PlayerCharacter] Executing DashAttack - Focus remaining: %.0f", Stats->GetCurrentFocus());

        SetCombatState(ECombatState::Attacking);
        ComboCount = 0;
        bIsInvincible = true;  // 대쉬 공격 중 무적

        // 데미지 정보 설정 (노티파이에서 StartWeaponTrace 호출)
        FDamageInfo DamageInfo(this, 40.f, EDamageType::DashAttack);
        DamageInfo.HitReaction = EHitReaction::Stagger;
        DamageInfo.StaggerDuration = 0.6f;
        DamageInfo.KnockbackForce = 300.f;
        SetWeaponDamageInfo(DamageInfo);

        // 대시공격 애니메이션 재생
        if (DashAttackMontage)
        {
            if (USkeletalMeshComponent* Mesh = GetMesh())
            {
                if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                {
                    AnimInst->SetRootMotionEnabled(bEnableDashAttackRootMotion);
                    AnimInst->SetAnimationCutEndTime(DashAttackCutEndTime);
                    AnimInst->Montage_Play(DashAttackMontage, 0.1f, 0.1f, 1.0f);
                    UE_LOG("[PlayerCharacter] Playing DashAttack montage");
                }
            }
        }
    }
    else if (SkillType == 2)  // UltimateAttack
    {
        // 포커스 100 소모
        if (!Stats->ConsumeFocus(100.f))
        {
            UE_LOG("[PlayerCharacter] ExecutePendingSkill (UltimateAttack) blocked - ConsumeFocus failed");
            return;
        }

        UE_LOG("[PlayerCharacter] Executing UltimateAttack - Focus remaining: %.0f", Stats->GetCurrentFocus());

        SetCombatState(ECombatState::Attacking);
        ComboCount = 0;
        bIsInvincible = true;  // 궁극기 사용 중 무적

        // 데미지 정보 설정 (노티파이에서 StartWeaponTrace 호출)
        FDamageInfo DamageInfo(this, 80.f, EDamageType::UltimateAttack);
        DamageInfo.HitReaction = EHitReaction::Knockback;
        DamageInfo.StaggerDuration = 1.0f;
        DamageInfo.KnockbackForce = 500.f;
        DamageInfo.bCanBeBlocked = false;
        SetWeaponDamageInfo(DamageInfo);

        // 궁극기 애니메이션 재생
        if (UltimateAttackMontage)
        {
            if (USkeletalMeshComponent* Mesh = GetMesh())
            {
                if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
                {
                    AnimInst->SetRootMotionEnabled(bEnableUltimateAttackRootMotion);
                    AnimInst->SetAnimationCutEndTime(UltimateAttackCutEndTime);
                    AnimInst->Montage_Play(UltimateAttackMontage, 0.1f, 0.1f, 1.0f);
                    UE_LOG("[PlayerCharacter] Playing UltimateAttack montage");
                }
            }
        }
    }
}

void APlayerCharacter::UpdateEffect(float DeltaTime)
{
    UStatsComponent* Stats = StatsComponent;
    if (!Stats)
    {
        Stats = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    }

    UWorld* WorldPtr = GetWorld();
    APlayerCameraManager* CameraManager = WorldPtr ? WorldPtr->GetPlayerCameraManager() : nullptr;

    const bool bShouldEvaluateCharging = bIsCharging && Stats && CameraManager;

    int32 DesiredStage = 0;
    
    if (!GS) return;

    const float Focus = GS->GetPlayerFocus().GetFocus();
    if (bShouldEvaluateCharging)
    {
        if (PlayerParticles["Charging"].size() < 3 || PlayerParticles["Charged"].size() < 3
            || !PlayerParticles["Charged"][0] || !PlayerParticles["Charged"][1] || !PlayerParticles["Charged"][2]
            || !PlayerParticles["Charging"][0] || !PlayerParticles["Charging"][1] || !PlayerParticles["Charging"][2])
            return;

        if (!bWasCharging) bWasCharging = true;

        UE_LOG("%f", Focus);
        if (Focus < 50.0f)
        {
            CameraManager->StartCameraShake(5, 0.0005, 0.0005, 20, 2);
            PlayerParticles["Charging"][0]->ResumeSpawning();
            PlayerParticles["Charging"][1]->PauseSpawning();
            PlayerParticles["Charging"][2]->PauseSpawning();
        }
        else if (Focus < 99.0f)
        {
            CameraManager->StopCameraShake();
            CameraManager->StartCameraShake(5, 0.00055, 0.00055, 20, 1);
            PlayerParticles["Charging"][0]->ResumeSpawning();
            PlayerParticles["Charging"][1]->ResumeSpawning();
            PlayerParticles["Charging"][2]->PauseSpawning();
        }
        else
        {
            CameraManager->StopCameraShake();
            CameraManager->StartCameraShake(5, 0.0006, 0.0006, 20, 0);
            PlayerParticles["Charging"][0]->ResumeSpawning();
            PlayerParticles["Charging"][1]->ResumeSpawning();
            PlayerParticles["Charging"][2]->ResumeSpawning();
        }
    }
    else if (bWasCharging)
    {
        bWasCharging = false;
        CameraManager->StopCameraShake();
        for (auto& Charge : PlayerParticles["Charging"])
        {
            if (Charge)
                Charge->PauseSpawning();
        }
    }
    else
    {
        // TODO : Update말고 Event로 주면 얼마나 좋을까..
        if (Focus < 50.0f) 
        {
            if (PlayerParticles["Charged"][0] && PlayerParticles["Charged"][0]->IsSpawning())PlayerParticles["Charged"][0]->PauseSpawning();
            if (PlayerParticles["Charged"][1] && PlayerParticles["Charged"][1]->IsSpawning())PlayerParticles["Charged"][1]->PauseSpawning();
            if (PlayerParticles["Charged"][2] && PlayerParticles["Charged"][2]->IsSpawning())PlayerParticles["Charged"][2]->PauseSpawning();
        }
        else if (Focus < 99.0f)
        {
            if (PlayerParticles["Charged"][0] && PlayerParticles["Charged"][0]->IsSpawning())PlayerParticles["Charged"][0]->PauseSpawning();
            if (PlayerParticles["Charged"][1] && !PlayerParticles["Charged"][1]->IsSpawning())PlayerParticles["Charged"][1]->ResumeSpawning();
        }
        else
        {
            if (PlayerParticles["Charged"][0] && PlayerParticles["Charged"][0]->IsSpawning())PlayerParticles["Charged"][0]->PauseSpawning();
            if (PlayerParticles["Charged"][1] && !PlayerParticles["Charged"][1]->IsSpawning())PlayerParticles["Charged"][1]->ResumeSpawning();
            if (PlayerParticles["Charged"][2] && !PlayerParticles["Charged"][2]->IsSpawning())PlayerParticles["Charged"][2]->ResumeSpawning();
        }
    }
}

// ============================================================================
// 이동 상태 업데이트 (Walking/Running/Jumping/Idle 전환)
// ============================================================================

void APlayerCharacter::UpdateMovementState(float DeltaTime)
{
    // 전투 액션 중이면 이동 상태 변경 안 함
    // (Attacking, Dodging, Blocking, Parrying, Staggered, Charging, Knockback, Dead)
    if (CombatState == ECombatState::Attacking ||
        CombatState == ECombatState::Dodging ||
        CombatState == ECombatState::Blocking ||
        CombatState == ECombatState::Parrying ||
        CombatState == ECombatState::Staggered ||
        CombatState == ECombatState::Charging ||
        CombatState == ECombatState::Knockback ||
        CombatState == ECombatState::Dead)
    {
        return;
    }

    UCharacterMovementComponent* MovementComp = GetCharacterMovement();
    if (!MovementComp)
    {
        return;
    }

    // 점프/낙하 중인지 확인
    bool bIsFalling = MovementComp->IsFalling();

    // 수평 속도 확인 (이동 중인지)
    FVector Velocity = MovementComp->GetVelocity();
    Velocity.Z = 0.0f;
    float HorizontalSpeed = Velocity.Size();

    // 달리기 중인지 확인 (TargetWalkSpeed >= SprintWalkSpeed)
    bool bIsSprinting = (MovementComp->TargetWalkSpeed >= MovementComp->SprintWalkSpeed);

    // 이동 중인지 확인 (최소 속도 임계값)
    const float MinMovementSpeed = 0.5f;
    bool bIsMoving = (HorizontalSpeed > MinMovementSpeed);

    ECombatState NewState = CombatState;

    if (bIsFalling)
    {
        // 공중에 있으면 Jumping
        NewState = ECombatState::Jumping;
    }
    else if (bIsMoving)
    {
        // 이동 중
        if (bIsSprinting)
        {
            NewState = ECombatState::Running;
        }
        else
        {
            NewState = ECombatState::Walking;
        }
    }
    else
    {
        // 정지 상태
        NewState = ECombatState::Idle;
    }

    // 상태 변경이 있을 때만 SetCombatState 호출
    if (NewState != CombatState)
    {
        // Jumping에서 다른 상태로 전환 시 (착지) -> 착지 쿨다운 시작
        if (CombatState == ECombatState::Jumping && NewState != ECombatState::Jumping)
        {
            bLandingCooldown = true;
            LandingCooldownTimer = 0.f;
            UE_LOG("[PlayerCharacter] Landed - starting cooldown %.2fs", LandingCooldownTime);
        }

        SetCombatState(NewState);
    }
}

void APlayerCharacter::GatherParticles()
{
    PlayerParticles.clear();

    const TSet<UActorComponent*>& OwnedComponents = GetOwnedComponents();
    if (OwnedComponents.IsEmpty())
    {
        return;
    }

    PlayerParticles["Charging"].SetNum(5);
    PlayerParticles["WeaponRibbon"].SetNum(5);
    PlayerParticles["Charged"].SetNum(5);
    for (UActorComponent* Component : OwnedComponents)
    {
        if (!Component)
        {
            continue;
        }

        UParticleSystemComponent* ParticleComp = Cast<UParticleSystemComponent>(Component);
        if (!ParticleComp)
        {
            continue;
        }

        FString ParticleName = ParticleComp->GetParticleName();
        if (ParticleName.empty())
        {
            ParticleName = ParticleComp->GetName();
        }

        if (ParticleName.empty())
        {
            continue;
        }

        if (ParticleName.find("Charging") != FString::npos)
        {
            PlayerParticles["Charging"][ParticleComp->GetParticleIndex()] =ParticleComp;
            continue;
        }
        else if (ParticleName.find("WeaponRibbon") != FString::npos)
        {
            PlayerParticles["WeaponRibbon"][ParticleComp->GetParticleIndex()] = ParticleComp;
            continue;
        }
        else if (ParticleName.find("Charged") != FString::npos)
        {
            PlayerParticles["Charged"][ParticleComp->GetParticleIndex()] = ParticleComp;
            continue;
        }
        PlayerParticles[ParticleName].Add(ParticleComp);
    }
}