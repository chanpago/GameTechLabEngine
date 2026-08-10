#include "pch.h"
#include "BasicEnemy.h"
#include "StatsComponent.h"
#include "HitboxComponent.h"
#include "SkeletalMeshComponent.h"
#include "AnimInstance.h"
#include "AnimMontage.h"
#include "AnimSequence.h"
#include "ResourceManager.h"
#include "CharacterMovementComponent.h"
#include "Source/Runtime/Engine/Physics/BodyInstance.h"

ABasicEnemy::ABasicEnemy()
{
    // 기본 스탯 설정
    BasicEnemyMaxHealth = 10.f;  // 체력 10으로 설정
    if (StatsComponent)
    {
        StatsComponent->MaxHealth = BasicEnemyMaxHealth;
        StatsComponent->CurrentHealth = BasicEnemyMaxHealth;
    }

    // 기본 AI 설정
    DetectionRange = 1500.f;
    AttackRange = 200.f;
    LoseTargetRange = 2000.f;
    MoveSpeed = 200.f;
    RotationSpeed = 5.f;
    AttackCooldown = 2.0f;
    MaxAttackPatterns = 1;
}

void ABasicEnemy::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG("[BasicEnemy] BeginPlay - Name: %s", GetName().c_str());

    // StatsComponent 재검색 (프리팹 로드 후)
    if (!StatsComponent)
    {
        StatsComponent = Cast<UStatsComponent>(GetComponent(UStatsComponent::StaticClass()));
    }

    // 스탯 설정
    if (StatsComponent)
    {
        StatsComponent->MaxHealth = BasicEnemyMaxHealth;
        StatsComponent->CurrentHealth = BasicEnemyMaxHealth;
        UE_LOG("[BasicEnemy] Stats initialized - MaxHP: %.1f, CurrentHP: %.1f",
               StatsComponent->MaxHealth, StatsComponent->CurrentHealth);
    }
    else
    {
        UE_LOG("[BasicEnemy] WARNING: StatsComponent is nullptr!");
    }

    // HitboxComponent 재검색 (프리팹 로드 후)
    if (!HitboxComponent)
    {
        HitboxComponent = Cast<UHitboxComponent>(GetComponent(UHitboxComponent::StaticClass()));
    }

    // CapsuleComponent 체크
    if (GetCapsuleComponent())
    {
     //   UE_LOG("[BasicEnemy] CapsuleComponent found - Radius: %.2f, Height: %.2f, GenerateOverlap: %d",
          //     GetCapsuleComponent()->CapsuleRadius, GetCapsuleComponent()->CapsuleHalfHeight,
           //    GetCapsuleComponent()->GetGenerateOverlapEvents());
    }
    else
    {
        UE_LOG("[BasicEnemy] WARNING: CapsuleComponent is nullptr!");
    }

    // 공격 애니메이션 몽타주 초기화
    if (!AttackAnimPath.empty())
    {
        UAnimSequence* AttackAnim = UResourceManager::GetInstance().Get<UAnimSequence>(AttackAnimPath);
        if (AttackAnim)
        {
            AttackMontage = NewObject<UAnimMontage>();
            AttackMontage->SetSourceSequence(AttackAnim);
            UE_LOG("[BasicEnemy] AttackMontage initialized: %s", AttackAnimPath.c_str());
        }
    }
}

void ABasicEnemy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 체력이 0 이하면 OnDeath 호출
    if (StatsComponent && StatsComponent->GetCurrentHealth() <= 0.f)
    {
        if (GetAIState() != EEnemyAIState::Dead)
        {
            OnDeath();
        }
    }
}

bool ABasicEnemy::PlayMontageByName(const FString& MontageName, float BlendIn, float BlendOut, float PlayRate)
{
    UAnimMontage* Montage = nullptr;

    // 몽타주 이름으로 찾기
    if (MontageName == "Attack")
    {
        Montage = AttackMontage;
    }

    if (!Montage || !GetMesh())
    {
        UE_LOG("[BasicEnemy] PlayMontageByName failed: %s not found", MontageName.c_str());
        return false;
    }

    if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
    {
        AnimInst->SetRootMotionEnabled(false);
        AnimInst->Montage_Play(Montage, BlendIn, BlendOut, PlayRate);
        UE_LOG("[BasicEnemy] Playing montage: %s", MontageName.c_str());
        return true;
    }

    return false;
}

void ABasicEnemy::SetMontagePlayRate(float NewPlayRate)
{
    if (GetMesh())
    {
        if (UAnimInstance* AnimInst = GetMesh()->GetAnimInstance())
        {
            AnimInst->Montage_SetPlayRate(NewPlayRate);
        }
    }
}

void ABasicEnemy::OnDeath()
{
    Super::OnDeath();

    UE_LOG("[BasicEnemy] OnDeath - Activating ragdoll");

    // 렉돌 활성화
    if (USkeletalMeshComponent* Mesh = GetMesh())
    {
        Mesh->SetPhysicsAnimationState(EPhysicsAnimationState::PhysicsDriven, 0.2f);

        // 날아가는 힘 추가 (마지막 피격 방향으로)
        FVector ForceDirection = GetActorForward() * -1.0f; // 반대 방향
        ForceDirection.Z = 0.5f; // 약간 위로
        ForceDirection = ForceDirection.GetNormalized();

        float ForceStrength = 50000.f; // 날아가는 힘 (AddForce는 continuous이므로 큰 값 사용)
        FVector Force = ForceDirection * ForceStrength;

        // 루트 본에 힘 적용
        if (Mesh->GetNumBodies() > 0)
        {
            const TArray<FBodyInstance*>& Bodies = Mesh->GetBodies();
            if (Bodies.Num() > 0 && Bodies[0])
            {
                Bodies[0]->AddForce(Force);
            }
        }

        UE_LOG("[BasicEnemy] Ragdoll activated with force: (%.2f, %.2f, %.2f)",
               Force.X, Force.Y, Force.Z);
    }

    // 일정 시간 후 제거 (옵션)
    // TODO: 5초 후 Destroy 하도록 타이머 추가 가능
}

void ABasicEnemy::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // 로드 후 필요한 초기화 수행
        // 애니메이션 몽타주는 BeginPlay에서 로드됨
    }
}
