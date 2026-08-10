#include "pch.h"
#include "BossEnemy.h"
#include "StatsComponent.h"
#include "HitboxComponent.h"
#include "World.h"
#include "SkeletalMeshComponent.h"
#include "AnimInstance.h"
#include "AnimMontage.h"
#include "AnimSequence.h"
#include "ResourceManager.h"
#include "GameModeBase.h"
#include "GameState.h"
#include "BillboardComponent.h"

ABossEnemy::ABossEnemy()
{
    // 보스 기본 스탯 설정
    if (StatsComponent)
    {
        StatsComponent->MaxHealth = 30.f;
        StatsComponent->CurrentHealth = 30.f;
        StatsComponent->MaxStamina = 200.f;
        StatsComponent->CurrentStamina = 200.f;
    }

    // 보스 AI 설정
    DetectionRange = 2000.f;
    AttackRange = 300.f;
    LoseTargetRange = 3000.f;
    MoveSpeed = 250.f;
    AttackCooldown = 1.5f;
    MaxAttackPatterns = 4;
}

void ABossEnemy::BeginPlay()
{
    // Super::BeginPlay 전에 StatsComponent 값 확인
    UE_LOG("[BossEnemy] BEFORE Super::BeginPlay - StatsComponent: %p, MaxHealth: %.1f, CurrentHealth: %.1f",
           StatsComponent,
           StatsComponent ? StatsComponent->MaxHealth : -1.f,
           StatsComponent ? StatsComponent->CurrentHealth : -1.f);

    Super::BeginPlay();

    // 보스 스탯 강제 설정 (프리팹에서 로드 시 값이 0일 수 있음)
    if (StatsComponent)
    {
        StatsComponent->MaxHealth = 500.f;
        StatsComponent->CurrentHealth = 500.f;
        StatsComponent->MaxStamina = 200.f;
        StatsComponent->CurrentStamina = 200.f;
        UE_LOG("[BossEnemy] Stats initialized - MaxHealth: %.1f, CurrentHealth: %.1f",
               StatsComponent->MaxHealth, StatsComponent->CurrentHealth);
    }

    // 페이즈 1 시작
    CurrentPhase = 1;

    // GameState에 보스 등록
    if (UWorld* World = GetWorld())
    {
        if (AGameModeBase* GM = World->GetGameMode())
        {
            if (AGameState* GS = Cast<AGameState>(GM->GetGameState()))
            {
                FString BossDisplayName = "흉조의 왕 모르고트";  // 보스 이름
                // 항상 500으로 등록 (StatsComponent 값이 잘못될 수 있음)
                const float BossMaxHP = 500.f;
                GS->RegisterBoss(BossDisplayName, BossMaxHP);
                UE_LOG("[BossEnemy] Registered to GameState with MaxHP: %.0f", BossMaxHP);
            }
        }
    }

    // ========================================================================
    // 애니메이션 몽타주 초기화
    // ========================================================================
    auto InitMontage = [](UAnimMontage*& OutMontage, const FString& AnimPath, const char* Name)
    {
        if (!AnimPath.empty())
        {
            UAnimSequence* Anim = UResourceManager::GetInstance().Get<UAnimSequence>(AnimPath);
            if (Anim)
            {
                OutMontage = NewObject<UAnimMontage>();
                OutMontage->SetSourceSequence(Anim);
                UE_LOG("[BossEnemy] %s montage initialized: %s", Name, AnimPath.c_str());
            }
            else
            {
                UE_LOG("[BossEnemy] Failed to find animation: %s", AnimPath.c_str());
            }
        }
    };

    InitMontage(LightComboMontage, LightComboAnimPath, "LightCombo");
    InitMontage(HeavySlamMontage, HeavySlamAnimPath, "HeavySlam");
    InitMontage(ChargeStartMontage, ChargeStartAnimPath, "ChargeStart");
    InitMontage(ChargeAttackMontage, ChargeAttackAnimPath, "ChargeAttack");
    InitMontage(SpinAttackMontage, SpinAttackAnimPath, "SpinAttack");
    InitMontage(PunishAttackMontage, PunishAttackAnimPath, "PunishAttack");
    // 콤보 애니메이션 몽타주 초기화
    InitMontage(Slash_1_Montage, Slash_1_AnimPath, "Slash_1");
    InitMontage(Slash_2_Montage, Slash_2_AnimPath, "Slash_2");
    InitMontage(Slash_3_Montage, Slash_3_AnimPath, "Slash_3");
    InitMontage(Spin_1_Montage, Spin_1_AnimPath, "Spin_1");
    InitMontage(Spin_2_Montage, Spin_2_AnimPath, "Spin_2");
    InitMontage(Smash_1_Montage, Smash_1_AnimPath, "Smash_1");
    InitMontage(Smash_2_Montage, Smash_2_AnimPath, "Smash_2");

    // 죽음 애니메이션 몽타주 초기화
    UE_LOG("[BossEnemy] Initializing Death montage with path: %s", DeathAnimPath.c_str());
    InitMontage(DeathMontage, DeathAnimPath, "Death");
    if (DeathMontage)
    {
        UE_LOG("[BossEnemy] Death montage successfully initialized");
    }
    else
    {
        UE_LOG("[BossEnemy] WARNING: Death montage is NULL after initialization!");
    }

    // 궁극기 애니메이션 몽타주 초기화
    InitMontage(UltimateMontage, UltimateAnimPath, "Ultimate");

    // 포효 애니메이션 몽타주 초기화
    InitMontage(RoarMontage, RoarAnimPath, "Roar");

    // ========================================================================
    // LockOnIndicator를 spine_01 본에 붙이기 위한 준비
    // ========================================================================
    // LockOnIndicator 컴포넌트 찾기
    const TSet<UActorComponent*>& Components = GetOwnedComponents();
    for (UActorComponent* Component : Components)
    {
        if (Component && Component->GetName() == "LockOnIndicator")
        {
            LockOnIndicator = Cast<UBillboardComponent>(Component);
            if (LockOnIndicator && GetMesh())
            {
                // spine_01 본의 인덱스 찾기
                LockOnBoneIndex = GetMesh()->GetBoneIndexByName(FName("spine_01"));
                if (LockOnBoneIndex != INDEX_NONE)
                {
                    UE_LOG("[BossEnemy] LockOnIndicator found, will follow spine_01 bone (index: %d)", LockOnBoneIndex);
                }
                else
                {
                    UE_LOG("[BossEnemy] WARNING: spine_01 bone not found!");
                }
            }
            break;
        }
    }
}

void ABossEnemy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 페이즈 전환 체크
    CheckPhaseTransition();

    // GameState에 보스 체력 업데이트
    if (StatsComponent)
    {
        if (UWorld* World = GetWorld())
        {
            if (AGameModeBase* GM = World->GetGameMode())
            {
                if (AGameState* GS = Cast<AGameState>(GM->GetGameState()))
                {
                    float CurrentHP = StatsComponent->GetCurrentHealth();
                    GS->OnBossHealthChanged(CurrentHP);

                    // 체력이 0 이하면 Victory
                    if (CurrentHP <= 0.0f)
                    {
                        GS->EnterVictory();
                    }
                }
            }
        }
    }

    // LockOnIndicator를 spine_01 본 위치로 업데이트
    if (LockOnIndicator && GetMesh() && LockOnBoneIndex != INDEX_NONE)
    {
        FTransform BoneTransform = GetMesh()->GetBoneWorldTransform(LockOnBoneIndex);
        LockOnIndicator->SetWorldLocation(BoneTransform.Translation);
    }
}

void ABossEnemy::OnDeath()
{
    Super::OnDeath();

    // 죽음 애니메이션은 Lua에서 처리

    // GameState에 승리 알림
    if (UWorld* World = GetWorld())
    {
        if (AGameModeBase* GM = World->GetGameMode())
        {
            if (AGameState* GS = Cast<AGameState>(GM->GetGameState()))
            {
                GS->EnterVictory();
            }
        }
    }
}

// ============================================================================
// 공격 패턴
// ============================================================================

void ABossEnemy::ExecuteAttackPattern(int PatternIndex)
{
    // 페이즈에 따라 공격 패턴 변경
    if (CurrentPhase >= 2)
    {
        // 페이즈 2에서는 더 강력한 패턴
        PatternIndex = (PatternIndex + 2) % MaxAttackPatterns;
    }

    switch (PatternIndex)
    {
    case 0:
        Attack_LightCombo();
        break;
    case 1:
        Attack_HeavySlam();
        break;
    case 2:
        Attack_ChargeAttack();
        break;
    case 3:
        Attack_SpinAttack();
        break;
    default:
        Attack_LightCombo();
        break;
    }
}

void ABossEnemy::Attack_LightCombo()
{
    CurrentPatternName = "LightCombo";
    FDamageInfo DamageInfo;
    DamageInfo.Instigator = this;
    DamageInfo.Damage = 20.f;
    DamageInfo.DamageType = EDamageType::Light;
    DamageInfo.HitReaction = EHitReaction::Flinch;
    DamageInfo.StaggerDuration = 0.3f;

    // WeaponCollider 기반 충돌 (노티파이에서 StartWeaponTrace 호출)
    SetWeaponDamageInfo(DamageInfo);

    // 콤보 애니메이션 재생
    if (LightComboMontage && GetMesh())
    {
        if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
        {
            AnimInst->SetRootMotionEnabled(bEnableAttackRootMotion);
            AnimInst->SetAnimationCutEndTime(AnimationCutEndTime);
            AnimInst->Montage_Play(LightComboMontage, 0.1f, 0.1f, 1.0f);
        }
    }
}

void ABossEnemy::Attack_HeavySlam()
{
    CurrentPatternName = "HeavySlam";
    FDamageInfo DamageInfo;
    DamageInfo.Instigator = this;
    DamageInfo.Damage = 40.f;
    DamageInfo.DamageType = EDamageType::Heavy;
    DamageInfo.HitReaction = EHitReaction::Knockback;
    DamageInfo.StaggerDuration = 0.8f;
    DamageInfo.KnockbackForce = 500.f;

    bHasSuperArmor = true;  // 강공격 중 슈퍼아머
    SetWeaponDamageInfo(DamageInfo);

    // 내려찍기 애니메이션 재생
    if (HeavySlamMontage && GetMesh())
    {
        if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
        {
            AnimInst->SetRootMotionEnabled(bEnableAttackRootMotion);
            AnimInst->SetAnimationCutEndTime(AnimationCutEndTime);
            AnimInst->Montage_Play(HeavySlamMontage, 0.1f, 0.1f, 1.0f);
        }
    }
}

void ABossEnemy::Attack_ChargeAttack()
{
    CurrentPatternName = "ChargeAttack";
    FDamageInfo DamageInfo;
    DamageInfo.Instigator = this;
    DamageInfo.Damage = 35.f;
    DamageInfo.DamageType = EDamageType::Heavy;
    DamageInfo.HitReaction = EHitReaction::Knockback;
    DamageInfo.StaggerDuration = 0.6f;
    DamageInfo.KnockbackForce = 800.f;

    bHasSuperArmor = true;
    SetWeaponDamageInfo(DamageInfo);

    // 돌진 이동
    if (TargetActor)
    {
        FVector Direction = (TargetActor->GetActorLocation() - GetActorLocation()).GetNormalized();
        Direction.Z = 0.f;
        SetActorLocation(GetActorLocation() + Direction * 500.f);
    }

    // 돌진 애니메이션 재생
    if (ChargeAttackMontage && GetMesh())
    {
        if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
        {
            AnimInst->SetRootMotionEnabled(bEnableAttackRootMotion);
            AnimInst->SetAnimationCutEndTime(AnimationCutEndTime);
            AnimInst->Montage_Play(ChargeAttackMontage, 0.05f, 0.1f, 1.2f);
        }
    }
}

void ABossEnemy::Attack_SpinAttack()
{
    CurrentPatternName = "SpinAttack";
    FDamageInfo DamageInfo;
    DamageInfo.Instigator = this;
    DamageInfo.Damage = 25.f;
    DamageInfo.DamageType = EDamageType::Special;
    DamageInfo.HitReaction = EHitReaction::Stagger;
    DamageInfo.StaggerDuration = 0.5f;
    DamageInfo.bCanBeBlocked = false;  // 가드 불가

    bHasSuperArmor = true;
    SetWeaponDamageInfo(DamageInfo);

    // 회전 공격 애니메이션 재생
    if (SpinAttackMontage && GetMesh())
    {
        if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
        {
            AnimInst->SetRootMotionEnabled(bEnableAttackRootMotion);
            AnimInst->SetAnimationCutEndTime(AnimationCutEndTime);
            AnimInst->Montage_Play(SpinAttackMontage, 0.1f, 0.1f, 1.0f);
        }
    }
}

// ============================================================================
// 패턴 이름 설정 (Lua용)
// ============================================================================

void ABossEnemy::SetCurrentPatternName(const FString& Name)
{
    CurrentPatternName = Name;

    // GuardBreak 패턴일 때 데미지 정보 설정
    if (Name == "GuardBreak")
    {
        FDamageInfo DamageInfo;
        DamageInfo.Instigator = this;
        DamageInfo.Damage = 30.f;
        DamageInfo.DamageType = EDamageType::GuardBreak;
        DamageInfo.HitReaction = EHitReaction::Knockback;
        DamageInfo.StaggerDuration = 1.0f;
        DamageInfo.KnockbackForce = 300.f;
        DamageInfo.bCanBeBlocked = false;

        bHasSuperArmor = true;
        SetWeaponDamageInfo(DamageInfo);
        UE_LOG("[BossEnemy] GuardBreak damage info set");
    }
}

// ============================================================================
// 몽타주 재생 (Lua용)
// ============================================================================

bool ABossEnemy::PlayMontageByName(const FString& MontageName, float BlendIn, float BlendOut, float PlayRate)
{
    // 현재 패턴 이름 저장
    CurrentPatternName = MontageName;

    UAnimMontage* Montage = nullptr;

    // 몽타주 이름으로 찾기
    if (MontageName == "LightCombo")
        Montage = LightComboMontage;
    else if (MontageName == "HeavySlam")
        Montage = HeavySlamMontage;
    else if (MontageName == "ChargeStart")
        Montage = ChargeStartMontage;
    else if (MontageName == "ChargeAttack")
        Montage = ChargeAttackMontage;
    else if (MontageName == "SpinAttack")
        Montage = SpinAttackMontage;
    else if (MontageName == "Death")
        Montage = DeathMontage;
    else if (MontageName == "PunishAttack")
        Montage = PunishAttackMontage;
    // 콤보 애니메이션
    else if (MontageName == "Slash_1")
        Montage = Slash_1_Montage;
    else if (MontageName == "Slash_2")
        Montage = Slash_2_Montage;
    else if (MontageName == "Slash_3")
        Montage = Slash_3_Montage;
    else if (MontageName == "Spin_1")
        Montage = Spin_1_Montage;
    else if (MontageName == "Spin_2")
        Montage = Spin_2_Montage;
    else if (MontageName == "Smash_1")
        Montage = Smash_1_Montage;
    else if (MontageName == "Smash_2")
        Montage = Smash_2_Montage;
    // 궁극기 애니메이션
    else if (MontageName == "Ultimate")
        Montage = UltimateMontage;
    // 포효 애니메이션
    else if (MontageName == "Roar")
        Montage = RoarMontage;

    if (!Montage || !GetMesh())
    {
        UE_LOG("[BossEnemy] PlayMontageByName failed: %s not found", MontageName.c_str());
        return false;
    }

    if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
    {
        AnimInst->SetRootMotionEnabled(bEnableAttackRootMotion);
        AnimInst->SetAnimationCutEndTime(AnimationCutEndTime);
        AnimInst->Montage_Play(Montage, BlendIn, BlendOut, PlayRate);
        UE_LOG("[BossEnemy] Playing montage: %s", MontageName.c_str());
        return true;
    }

    return false;
}

void ABossEnemy::SetMontagePlayRate(float NewPlayRate)
{
    if (GetMesh())
    {
        if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
        {
            AnimInst->Montage_SetPlayRate(NewPlayRate);
        }
    }
}

// ============================================================================
// 페이즈 시스템
// ============================================================================

void ABossEnemy::SetPhase(int32 NewPhase)
{
    if (NewPhase == CurrentPhase || NewPhase < 1 || NewPhase > MaxPhase)
    {
        return;
    }

    int32 OldPhase = CurrentPhase;
    CurrentPhase = NewPhase;
    OnPhaseChanged(OldPhase, NewPhase);
}

void ABossEnemy::CheckPhaseTransition()
{
    if (!StatsComponent || CurrentPhase >= MaxPhase)
    {
        return;
    }

    float HealthPercent = StatsComponent->GetHealthPercent();

    // 페이즈 2 진입 조건
    if (CurrentPhase == 1 && HealthPercent <= Phase2HealthThreshold)
    {
        SetPhase(2);
    }
}

void ABossEnemy::OnPhaseChanged(int32 OldPhase, int32 NewPhase)
{
    // 페이즈 변경 시 처리
    if (NewPhase == 2)
    {
        // 페이즈 2: 더 공격적으로
        AttackCooldown = 1.0f;
        MoveSpeed = 350.f;

        // TODO: 페이즈 전환 연출 (포효 애니메이션 등)
    }
}
