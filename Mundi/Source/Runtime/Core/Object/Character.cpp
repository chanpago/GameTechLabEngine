#include "pch.h"
#include "Character.h"
#include "CapsuleComponent.h"
#include "SkeletalMeshComponent.h"
#include "CharacterMovementComponent.h"
#include "StaticMeshComponent.h"
#include "AnimNotifyParticleComponent.h"
#include "ParticleSystemComponent.h"
#include "ObjectMacros.h"
#include "StaticMeshActor.h"
#include "World.h"
#include "Source/Runtime/Engine/GameFramework/DecalActor.h"
#include "Source/Runtime/Engine/Components/DecalComponent.h"
#include "Source/Runtime/Engine/Physics/PhysScene.h"
#include "Source/Runtime/Engine/GameFramework/PlayerCameraManager.h"
#include "Source/Runtime/Game/Player/PlayerCharacter.h"
#include "Source/Runtime/Game/Enemy/BossEnemy.h"
#include "Source/Runtime/AssetManagement/ResourceManager.h"
#include "Collision.h" 
ACharacter::ACharacter()
{
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	//CapsuleComponent->SetSize();
    WeaponMeshComp = CreateDefaultSubobject<UStaticMeshComponent>("WeaponMeshComponent");
	SetRootComponent(CapsuleComponent);

	SubWeaponMeshComp = CreateDefaultSubobject<UStaticMeshComponent>("SubWeaponMeshComponent");

	if (SkeletalMeshComp)
	{
		SkeletalMeshComp->SetupAttachment(CapsuleComponent);

		//SkeletalMeshComp->SetRelativeLocation(FVector());
		//SkeletalMeshComp->SetRelativeScale(FVector());
	}
    if (WeaponMeshComp)
    {
        WeaponMeshComp->SetupAttachment(SkeletalMeshComp);
    }
	 

	if (SubWeaponMeshComp)
	{
		SubWeaponMeshComp->SetupAttachment(SkeletalMeshComp);
	}
	 
	CharacterMovement = CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovement");
	if (CharacterMovement)
	{
		CharacterMovement->SetUpdatedComponent(CapsuleComponent);
	} 
}

ACharacter::~ACharacter()
{
	// 델리게이트 정리 - 람다에서 this를 캡처했으므로 소멸 전에 제거해야 함
	OnComponentBeginOverlap.Clear();
}

void ACharacter::Tick(float DeltaSecond)
{
	Super::Tick(DeltaSecond);
	UpdateBloodDecalPool(DeltaSecond);

    if (InitFrameCounter < 3) {
        InitFrameCounter++;
        return;
    }
	// 무기 트랜스폼 업데이트 (본 따라가기)
	UpdateWeaponTransform();

	// 무기 Sweep 판정 (활성화된 경우)
	if (bWeaponTraceActive)
	{
		PerformWeaponTrace();
	}
}

void ACharacter::BeginPlay()
{
    Super::BeginPlay();

	InitializeBloodDecalPool();

    // 파티클 시스템 로드
    FleshImpactParticle = RESOURCE.Load<UParticleSystem>("Data/Particle/G_Blood.particle");
    BlockImpactParticle = RESOURCE.Load<UParticleSystem>("Data/Particle/G_Spark2.particle");

    if (!FleshImpactParticle)
    {
        UE_LOG("[Character] WARNING: Failed to load FleshImpactParticle (Data/Particle/G_Blood.particle)");
    }
    if (!BlockImpactParticle)
    {
        UE_LOG("[Character] WARNING: Failed to load BlockImpactParticle (Data/Particle/G_Spark2.particle)");
    }

    // 무기 관련 컴포넌트 상태 로그
    UE_LOG("[Character] BeginPlay - WeaponMeshComp: %p, WeaponCollider: %p, SkeletalMeshComp: %p",
           WeaponMeshComp, WeaponCollider, SkeletalMeshComp);

    if (!WeaponMeshComp && !WeaponCollider)
    {
        UE_LOG("[Character] WARNING: No weapon collision component found! Weapon attacks will not work.");
    }

    // WeaponCollider가 있으면 그 크기를 Sweep 파라미터로 사용
    // 에디터에서 보이는 디버그 캡슐 = 실제 충돌 범위
    if (WeaponCollider)
    {
        WeaponTraceRadius = WeaponCollider->CapsuleRadius;
        WeaponTraceLength = WeaponCollider->CapsuleHalfHeight * 2.0f;  // HalfHeight → 전체 길이
        UE_LOG("[Character] WeaponCollider size applied - Radius: %.2f, Length: %.2f",
               WeaponTraceRadius, WeaponTraceLength);
    }

    // OnComponentBeginOverlap 델리게이트 바인딩 (Overlap 방식 충돌 감지)
    OnComponentBeginOverlap.Add([this](UPrimitiveComponent* OverlappedComp, UPrimitiveComponent* OtherComp, const FTriggerHit* TriggerHit) {
        // WeaponCollider와의 오버랩만 처리
        if (!this || !WeaponCollider || !OverlappedComp || OverlappedComp != static_cast<UPrimitiveComponent*>(WeaponCollider))
        {
            return;
        }

        if (!OtherComp || !OtherComp->GetOwner())
        {
            return;
        }

        AActor* OtherActor = OtherComp->GetOwner();

        UE_LOG("[Character] WeaponCollider overlap detected: %s (my:%s) -> %s (other:%s)",
               GetName().c_str(), OverlappedComp->GetName().c_str(),
               OtherActor->GetName().c_str(), OtherComp->GetName().c_str());

        // 보스(EnemyBase)는 플레이어만 충돌 처리
        // 플레이어는 모두 다 충돌 처리
        FString MyName = GetName();
        FString OtherName = OtherActor->GetName();
        if (MyName.find("Enemy") != FString::npos ||
            MyName.find("Boss") != FString::npos ||
            MyName.find("에너미") != FString::npos ||
            MyName.find("보스") != FString::npos ||
            MyName.find("적") != FString::npos)
        {
            // 적인 경우: 상대방이 플레이어가 아니면 무시
            if (OtherName.find("Player") == FString::npos && OtherName.find("Shinobi") == FString::npos)
            {
                UE_LOG("[Character] Ignoring overlap - enemy attacking non-player");
                return;
            }
        }
        // 충돌 위치 계산 (상대방이 맞은 위치 = 상대방 캡슐 표면)
        FVector Direction = (OtherComp->GetWorldLocation() - OverlappedComp->GetWorldLocation()).GetNormalized();

        // 상대방 캡슐의 스케일 적용된 반지름 계산
        float OtherRadius = 0.5f;  // 기본값
        if (UCapsuleComponent* OtherCapsule = Cast<UCapsuleComponent>(OtherComp))
        {
            FVector OtherScale = OtherCapsule->GetWorldScale();
            OtherRadius = OtherCapsule->CapsuleRadius * FMath::Max(OtherScale.X, FMath::Max(OtherScale.Y, OtherScale.Z));
        }

        FVector HitLocation = OtherComp->GetWorldLocation() - Direction * OtherRadius;
        FVector HitNormal = -Direction;  // 상대방 표면의 법선 (검 방향)

        OnWeaponColliderOverlap(OtherActor, HitLocation, HitNormal);
    });
}

void ACharacter::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        // Rebind important component pointers after load (prefab/scene)
        CapsuleComponent = nullptr;
        CharacterMovement = nullptr;
        WeaponMeshComp = nullptr;
        SubWeaponMeshComp = nullptr;
        SkeletalMeshComp = nullptr;
        WeaponCollider = nullptr;

        // 1차 패스: 기본 컴포넌트들 찾기 (CapsuleComponent, CharacterMovement, SkeletalMeshComp)
        for (UActorComponent* Comp : GetOwnedComponents())
        {
            if (auto* Move = Cast<UCharacterMovementComponent>(Comp))
            {
                CharacterMovement = Move;
            }
            else if (auto* Skel = Cast<USkeletalMeshComponent>(Comp))
            {
                SkeletalMeshComp = Skel;
            }
            else if (auto* Cap = Cast<UCapsuleComponent>(Comp))
            {
                // 이름으로 WeaponCollider 구분
                FName CompName = Comp->GetName();
                if (CompName == FName("WeaponCollider"))
                {
                    WeaponCollider = Cap;
                }
                // 부모가 없는 캡슐 = 루트 캡슐
                else if (!Cap->GetAttachParent())
                {
                    CapsuleComponent = Cap;
                }
            }
        }

        // 2차 패스: 이름으로 무기 관련 컴포넌트 찾기
        for (UActorComponent* Comp : GetOwnedComponents())
        {
            if (auto* StaticMesh = Cast<UStaticMeshComponent>(Comp))
            {
                // 이름으로 구분
                FName CompName = Comp->GetName();
                if (CompName == FName("WeaponMeshComponent"))
                {
                    WeaponMeshComp = StaticMesh;
                }
                else if (CompName == FName("SubWeaponMeshComponent"))
                {
                    SubWeaponMeshComp = StaticMesh;
                }
            }
        }

        if (CharacterMovement)
        {
            USceneComponent* Updated = CapsuleComponent ? reinterpret_cast<USceneComponent*>(CapsuleComponent)
                                                        : GetRootComponent();
            CharacterMovement->SetUpdatedComponent(Updated);
        }
    }
}

void ACharacter::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    CapsuleComponent = nullptr;
    CharacterMovement = nullptr;
    WeaponMeshComp = nullptr;
    SubWeaponMeshComp = nullptr;
    SkeletalMeshComp = nullptr;
    WeaponCollider = nullptr;

    // 1차 패스: 기본 컴포넌트들 찾기
    for (UActorComponent* Comp : GetOwnedComponents())
    {
        if (auto* Move = Cast<UCharacterMovementComponent>(Comp))
        {
            CharacterMovement = Move;
        }
        else if (auto* Skel = Cast<USkeletalMeshComponent>(Comp))
        {
            SkeletalMeshComp = Skel;
        }
        else if (auto* Cap = Cast<UCapsuleComponent>(Comp))
        {
            // 이름으로 WeaponCollider 구분
            FName CompName = Comp->GetName();
            if (CompName == FName("WeaponCollider"))
            {
                WeaponCollider = Cap;
            }
            // 부모가 없는 캡슐 = 루트 캡슐
            else if (!Cap->GetAttachParent())
            {
                CapsuleComponent = Cap;
            }
        }
    }

    // 2차 패스: 부모-자식 관계로 무기 관련 컴포넌트 찾기
    for (UActorComponent* Comp : GetOwnedComponents())
    {
        if (auto* StaticMesh = Cast<UStaticMeshComponent>(Comp))
        {
            // SkeletalMeshComp의 자식 StaticMeshComponent = 무기
            USceneComponent* Parent = StaticMesh->GetAttachParent();
            if (Parent == SkeletalMeshComp)
            {
                if (!WeaponMeshComp)
                {
                    WeaponMeshComp = StaticMesh;
                }
                else if (!SubWeaponMeshComp)
                {
                    SubWeaponMeshComp = StaticMesh;
                }
            }
        }
    }

    // Ensure movement component tracks the correct updated component
    if (CharacterMovement)
    {
        USceneComponent* Updated = CapsuleComponent ? reinterpret_cast<USceneComponent*>(CapsuleComponent)
                                                    : GetRootComponent();
        CharacterMovement->SetUpdatedComponent(Updated);
    }
}

void ACharacter::Jump()
{
	if (CharacterMovement)
	{
		CharacterMovement->DoJump();
	}
}

void ACharacter::StopJumping()
{
	if (CharacterMovement)
	{
		// 점프 scale을 조절할 때 사용,
		// 지금은 비어있음
		CharacterMovement->StopJump();
	}
}

// ============================================================================
// 무기 어태치 시스템
// ============================================================================

void ACharacter::UpdateWeaponTransform()
{
	if (!WeaponMeshComp || !SkeletalMeshComp)
	{
		return;
	}

	// 스켈레탈 메시가 로드되었는지 확인
	if (!SkeletalMeshComp->GetSkeletalMesh())
	{
		return;
	}

	// 본 인덱스 찾기
	int32 BoneIndex = SkeletalMeshComp->GetBoneIndexByName(FName(WeaponBoneName));
	if (BoneIndex < 0)
	{
		return;
	}

	// 본의 월드 트랜스폼 가져오기
	FTransform BoneTransform = SkeletalMeshComp->GetBoneWorldTransform(BoneIndex);

	// 오프셋 적용
	FVector FinalLocation = BoneTransform.Translation +
		BoneTransform.Rotation.RotateVector(WeaponOffset);

	// 회전 오프셋 적용 (오일러 → 쿼터니언)
	FQuat RotOffset = FQuat::MakeFromEulerZYX(WeaponRotationOffset);
	FQuat FinalRotation = BoneTransform.Rotation * RotOffset;

	// 무기 트랜스폼 설정
	WeaponMeshComp->SetWorldLocation(FinalLocation);
	WeaponMeshComp->SetWorldRotation(FinalRotation);
}

// ============================================================================
// 무기 충돌 시스템 (Sweep 기반)
// ============================================================================

void ACharacter::StartWeaponTrace()
{
	bWeaponTraceActive = true;
	HitActorsThisSwing.Empty();

	 //이미 설정된 CurrentWeaponDamageInfo 사용, 없으면 기본값
	if (!CurrentWeaponDamageInfo.Instigator)
	{
		CurrentWeaponDamageInfo = FDamageInfo(this, 10.0f, EDamageType::Light);
	}

	 //WeaponCollider 오버랩 활성화 (PhysX 기반)
	if (WeaponCollider)
	{
		WeaponCollider->SetGenerateOverlapEvents(true);
		UE_LOG("[Character] WeaponCollider overlap enabled");
	}

	 //WeaponMeshComp가 있으면 Sweep 방식도 병행
	if (WeaponMeshComp)
	{
		// 현재 무기 위치를 이전 위치로 초기화
		FVector WeaponPos = WeaponMeshComp->GetWorldLocation();
		FQuat WeaponRot = WeaponMeshComp->GetWorldRotation();

		// 무기의 로컬 Z축 방향으로 베이스와 팁 위치 계산
		FVector WeaponUp = WeaponRot.RotateVector(FVector(0, 0, 1));
		PrevWeaponBasePos = WeaponPos;
		PrevWeaponTipPos = WeaponPos + WeaponUp * WeaponTraceLength;
		UE_LOG("[Character] Weapon trace started with WeaponMeshComp (Sweep enabled)");
	}
	else
	{
		UE_LOG("[Character] Weapon trace started WITHOUT WeaponMeshComp (Sweep DISABLED)");
	}
}

void ACharacter::EndWeaponTrace()
{
	bWeaponTraceActive = false;
	HitActorsThisSwing.Empty();
	ClearWeaponDebugData();

	// WeaponCollider 오버랩 비활성화
	if (WeaponCollider)
	{
		WeaponCollider->SetGenerateOverlapEvents(false);
		//UE_LOG("[Character] WeaponCollider overlap disabled");
	}

	UE_LOG("[Character] Weapon trace ended");
}

void ACharacter::PerformWeaponTrace()
{
	if (!bWeaponTraceActive)
	{
		return;
	}

	if (!WeaponMeshComp)
	{
		// WeaponMeshComp가 없으면 Sweep 불가 (WeaponCollider 오버랩으로만 작동)
		return;
	}

	// PhysX geometry 에러 방지: 0이면 기본값 사용
	if (WeaponTraceRadius <= 0.0f) WeaponTraceRadius = 0.1f;
	if (WeaponTraceLength <= 0.0f) WeaponTraceLength = 0.8f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FPhysScene* PhysScene = World->GetPhysScene();
	if (!PhysScene)
	{
		return;
	}

	// 현재 무기 위치 계산
	FVector WeaponPos = WeaponMeshComp->GetWorldLocation();
	FQuat WeaponRot = WeaponMeshComp->GetWorldRotation();

	FVector WeaponUp = WeaponRot.RotateVector(FVector(1, 0, 0));
	FVector CurrentBasePos = WeaponPos;
	FVector CurrentTipPos = WeaponPos + WeaponUp * WeaponTraceLength;

	// ========== 디버그 데이터 저장 (RenderDebugVolume에서 렌더링) ==========
	if (bDrawWeaponDebug)
	{
		UpdateWeaponDebugData(CurrentBasePos, CurrentTipPos,
							  PrevWeaponBasePos, PrevWeaponTipPos, WeaponRot);
	}
	// ========== 디버그 데이터 저장 끝 ==========

	// 이전 프레임 → 현재 프레임 경로를 캡슐로 분할 스윕
	const float HalfTraceLength = WeaponTraceLength * 0.5f;
	if (HalfTraceLength <= KINDA_SMALL_NUMBER)
	{
		PrevWeaponBasePos = CurrentBasePos;
		PrevWeaponTipPos = CurrentTipPos;
		return;
	}

	auto ProcessWeaponHit = [&](const FHitResult& Hit)
	{
		if (Hit.HitActor && !HitActorsThisSwing.Contains(Hit.HitActor))
		{
			HitActorsThisSwing.Add(Hit.HitActor);
			//UE_LOG("[Character] SweepCapsule HIT! Actor: %s, Damage: %.1f, DamageType: %d", Hit.HitActor->GetName().c_str(), CurrentWeaponDamageInfo.Damage, static_cast<int>(CurrentWeaponDamageInfo.DamageType));
			OnWeaponHitDetected(Hit.HitActor, Hit.ImpactPoint, Hit.ImpactNormal);
		}
	};

	auto PerformCapsuleSweep = [&](const FVector& StartCenter, const FVector& EndCenter, const FQuat& CapsuleRot)
	{
		FHitResult HitResult;
		if (PhysScene->SweepCapsuleOriented(StartCenter, EndCenter, WeaponTraceRadius, HalfTraceLength, CapsuleRot, HitResult, this))
		{
			ProcessWeaponHit(HitResult);
		}
	};

	auto ComputeDirection = [](const FVector& PrimaryDir, const FVector& SecondaryDir)
	{
		FVector Result = PrimaryDir;
		if (Result.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			Result = SecondaryDir;
		}
		if (Result.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			return FVector(0.0f, 0.0f, 1.0f);
		}
		Result.Normalize();
		return Result;
	};

	const int32 NumSubsteps = 3;
	const float InvSubstep = 1.0f / static_cast<float>(NumSubsteps);

	for (int32 Step = 0; Step < NumSubsteps; ++Step)
	{
		const float Alpha0 = Step * InvSubstep;
		const float Alpha1 = (Step + 1) * InvSubstep;

		const FVector StartBase = FVector::Lerp(PrevWeaponBasePos, CurrentBasePos, Alpha0);
		const FVector EndBase = FVector::Lerp(PrevWeaponBasePos, CurrentBasePos, Alpha1);
		const FVector StartTip = FVector::Lerp(PrevWeaponTipPos, CurrentTipPos, Alpha0);
		const FVector EndTip = FVector::Lerp(PrevWeaponTipPos, CurrentTipPos, Alpha1);

		const FVector PrimaryDir = StartTip - StartBase;
		const FVector SecondaryDir = EndTip - EndBase;
		const FVector StartDir = ComputeDirection(PrimaryDir, SecondaryDir);
		const FVector EndDir = ComputeDirection(SecondaryDir, PrimaryDir);

		const FQuat CapsuleRotation = FQuat::FindBetweenVectors(FVector(1.0f, 0.0f, 0.0f), StartDir);

		const FVector BaseStartCenter = StartBase + StartDir * HalfTraceLength;
		const FVector BaseEndCenter = EndBase + EndDir * HalfTraceLength;
		const FVector TipStartCenter = StartTip - StartDir * HalfTraceLength;
		const FVector TipEndCenter = EndTip - EndDir * HalfTraceLength;

		PerformCapsuleSweep(BaseStartCenter, BaseEndCenter, CapsuleRotation);
		PerformCapsuleSweep(TipStartCenter, TipEndCenter, CapsuleRotation);
	}

	// 현재 위치를 다음 프레임의 이전 위치로 저장
	PrevWeaponBasePos = CurrentBasePos;
	PrevWeaponTipPos = CurrentTipPos;
}

void ACharacter::InitializeBloodDecalPool()
{
	BloodDecalPool.Empty();
	BloodDecalFreeList.Empty();

	UWorld* World = GetWorld();
	if (!World || InitialBloodDecalPoolSize <= 0)
	{
		return;
	}

	BloodDecalTexture = RESOURCE.Load<UTexture>("Data/Textures/Blood.png");
	if (!BloodDecalTexture)
	{
		BloodDecalTexture = RESOURCE.Load<UTexture>(GDataDir + "/Textures/Blood.png");
	}
	if (!BloodDecalTexture)
	{
		UE_LOG("[Character] WARNING: Failed to load default blood decal texture (Data/Texture/Blood.png)");
	}

	BloodDecalPool.Reserve(InitialBloodDecalPoolSize);
	BloodDecalFreeList.Reserve(InitialBloodDecalPoolSize);

	for (int32 Index = 0; Index < InitialBloodDecalPoolSize; ++Index)
	{
		ADecalActor* Decal = World->SpawnActor<ADecalActor>(FTransform());
		if (!Decal)
		{
			continue;
		}

		ConfigureBloodDecalActor(Decal);

		FBloodDecalEntry Entry;
		Entry.Actor = Decal;
		Entry.bInUse = false;
		Entry.RemainingLifetime = 0.0f;

		const int32 NewIndex = BloodDecalPool.Add(Entry);
		BloodDecalFreeList.Add(NewIndex);
	}
}

ADecalActor* ACharacter::AcquireBloodDecal()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	auto TryActivateFromIndex = [&](int32 PoolIndex) -> ADecalActor*
	{
		if (PoolIndex < 0 || PoolIndex >= BloodDecalPool.Num())
		{
			return nullptr;
		}

		FBloodDecalEntry Entry;
		ADecalActor* Replacement = World->SpawnActor<ADecalActor>(FTransform());
		if (!Replacement)
		{
			return nullptr;
		}
		ConfigureBloodDecalActor(Replacement);
		Entry.Actor = Replacement;

		Entry.bInUse = true;
		Entry.RemainingLifetime = BloodDecalLifetime;

		if (UDecalComponent* DecalComponent = Entry.Actor->GetDecalComponent())
		{
			DecalComponent->SetOpacity(1.0f);
		}

		BloodDecalPool[PoolIndex] = Entry;

		return Entry.Actor;
	};

	while (!BloodDecalFreeList.IsEmpty())
	{
		const int32 PoolIndex = BloodDecalFreeList.Pop();
		if (ADecalActor* Decal = TryActivateFromIndex(PoolIndex))
		{
			return Decal;
		}
	}

	ADecalActor* Decal = World->SpawnActor<ADecalActor>(FTransform());
	if (Decal)
	{
		ConfigureBloodDecalActor(Decal);

		FBloodDecalEntry Entry;
		Entry.Actor = Decal;
		Entry.bInUse = true;
		Entry.RemainingLifetime = BloodDecalLifetime;
		
		if (UDecalComponent* DecalComponent = Entry.Actor ?
			Entry.Actor->GetDecalComponent() : nullptr)
			DecalComponent->SetOpacity(1.0f);

		BloodDecalPool.Add(Entry);
	}
	return Decal;
}

void ACharacter::ReleaseBloodDecal(ADecalActor* DecalActor)
{
	if (!DecalActor)
	{
		return;
	}

	for (int32 Index = 0; Index < BloodDecalPool.Num(); ++Index)
	{
		FBloodDecalEntry& Entry = BloodDecalPool[Index];
		if (Entry.Actor == DecalActor)
		{
			if (!Entry.bInUse)
			{
				return;
			}

			Entry.bInUse = false;
			Entry.RemainingLifetime = 0.0f;
			BloodDecalFreeList.Add(Index);

			DecalActor->SetActorHiddenInGame(true);
			return;
		}
	}
}

void ACharacter::UpdateBloodDecalPool(float DeltaSeconds)
{
	if (BloodDecalLifetime <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	for (int32 Index = 0; Index < BloodDecalPool.Num(); ++Index)
	{
		FBloodDecalEntry& Entry = BloodDecalPool[Index];
		if (!Entry.bInUse || !Entry.Actor)
		{
			continue;
		}

		Entry.RemainingLifetime -= DeltaSeconds;
		const float NormalizedLifetime = FMath::Clamp(Entry.RemainingLifetime / BloodDecalLifetime, 0.0f, 1.0f);

		if (UDecalComponent* DecalComponent = Entry.Actor->GetDecalComponent())
		{
			DecalComponent->SetOpacity(NormalizedLifetime);
		}

		if (Entry.RemainingLifetime <= 0.0f)
		{
			ReleaseBloodDecal(Entry.Actor);
		}
	}
}

void ACharacter::ConfigureBloodDecalActor(ADecalActor* DecalActor)
{
	if (!DecalActor)
	{
		return;
	}

	DecalActor->SetActorHiddenInGame(true);
	DecalActor->SetActorScale(FVector(2.0f, 2.0f, 1.0f));

	if (UDecalComponent* DecalComponent = DecalActor->GetDecalComponent())
	{
		if (BloodDecalTexture)
		{
			DecalComponent->SetDecalTexture(BloodDecalTexture);
		}
		else
		{
			DecalComponent->SetDecalTexture("Data/Texture/Blood.png");
		}

		DecalComponent->SetOpacity(1.0f);
	}
}
void ACharacter::SpawnBloodDecalAt(const FVector& HitLocation, const FVector& HitNormal)
{
	ADecalActor* DecalActor = AcquireBloodDecal();
	if (!DecalActor) return;

	const FVector SpawnLocation(HitLocation.X, HitLocation.Y, BloodDecalGroundZ);

	const FVector SafeNormal = HitNormal.SizeSquared() > KINDA_SMALL_NUMBER? 
							   HitNormal.GetNormalized() : FVector(0.f, 0.f, 1.f);
	const FQuat AlignRotation = FQuat::FindBetweenVectors(FVector(0.f, 0.f, 1.f), SafeNormal);
	const FQuat YAxisQuarterTurn = FQuat::FromAxisAngle(FVector(0.f, 1.f, 0.f), HALF_PI);
	const FQuat SpawnRotation = YAxisQuarterTurn; //*AlignRotation;

	DecalActor->SetActorHiddenInGame(false);
	DecalActor->SetActorLocation(SpawnLocation);
	DecalActor->SetActorRotation(SpawnRotation);

	for (FBloodDecalEntry& Entry : BloodDecalPool)
	{
		if (Entry.Actor == DecalActor)
		{
			Entry.bInUse = true;
			Entry.RemainingLifetime = BloodDecalLifetime;
			if (UDecalComponent* DecalComponent = Entry.Actor ? Entry.Actor->GetDecalComponent() : nullptr)
			{
				DecalComponent->SetOpacity(1.0f);
			}
			break;
		}
	}
}

void ACharacter::OnWeaponHitDetected(AActor* HitActor, const FVector& HitLocation, const FVector& HitNormal)
{
	// 데미지 정보에 충돌 정보 추가
	FDamageInfo DamageInfo = CurrentWeaponDamageInfo;
	DamageInfo.HitLocation = HitLocation;
	DamageInfo.DamageCauser = WeaponMeshComp ? WeaponMeshComp->GetOwner() : this;

	// 공격 방향 계산 (공격자 → 피격자)
	FVector AttackerPos = GetActorLocation();
	DamageInfo.HitDirection = (HitLocation - AttackerPos).GetNormalized();

	// 충돌 위치에 StaticMeshActor 스폰 (디버그용)
	UWorld* World = GetWorld();
	if (World && bDrawWeaponDebug)
	{
		FTransform SpawnTransform;
		SpawnTransform.Translation = HitLocation;

		//// 디버그 공 Spawn
		//AStaticMeshActor* HitMarker = World->SpawnActor<AStaticMeshActor>(SpawnTransform);
		//if (HitMarker)
		//{
		//	HitMarker->GetStaticMeshComponent()->SetStaticMesh(GDataDir + "/Model/Sphere8.obj");
		//	HitMarker->SetActorScale(FVector(0.1f, 0.1f, 0.1f));
		//}

		// Helper lambda to spawn a self-destroying particle effect
		auto SpawnImpactParticle = [&](UParticleSystem* Particle, const FVector& Location, const FVector& Rotation)
		{
			if (!Particle || !World)
			{
				return;
			}

			FTransform SpawnTransform(Location, FQuat(Rotation.X, Rotation.Y, Rotation.Z, 1.0), FVector(1.0, 1.0, 1.0));
			AActor* EmitterActor = World->SpawnActor<AActor>(SpawnTransform);
			if (!EmitterActor)
			{
				return;
			}

			UAnimNotifyParticleComponent* ParticleComp = Cast<UAnimNotifyParticleComponent>(EmitterActor->AddNewComponent(UAnimNotifyParticleComponent::StaticClass()));
			if (!ParticleComp)
			{
				EmitterActor->Destroy();
				return;
			}

			ParticleComp->SetTemplate(Particle);
			ParticleComp->SetWorldTransform(SpawnTransform);
			ParticleComp->SetForcedLifeTime(0.5f);  // 0.5초 후 자동 종료
			ParticleComp->ResetAndActivate();

			// Set up auto-destroy for the hosting actor (지연 삭제로 iterator 문제 방지)
			TWeakObjectPtr<AActor> EmitterActorWeak(EmitterActor);
			ParticleComp->OnParticleSystemFinished.Add([EmitterActorWeak](UParticleSystemComponent* FinishedComp) {
				if (EmitterActorWeak.IsValid() && GWorld) {
					// 즉시 삭제 대신 안전한 시점에 삭제
					GWorld->AddPendingKillActor(EmitterActorWeak.Get());
				}
			});
		};

		// World에서 Player를 가져와서 상태에 따라 파티클 선택
		APlayerCharacter* Player = GWorld->FindActor<APlayerCharacter>();
		if (HitActor == Player)
		{
			if (Player && Player->GetCombatState() == ECombatState::Blocking)
			{
				// 불꽃 파티클 소환 (방어 성공)
				GWorld->GetPlayerCameraManager()->StartCameraShake(0.3, 0.02, 0.015, 15);
				SpawnImpactParticle(BlockImpactParticle, HitLocation, HitNormal);
			}
			else
			{
				// 피 파티클 소환 (피격)
				GWorld->GetPlayerCameraManager()->StartCameraShake(0.3, 0.01, 0.01, 10);
				SpawnImpactParticle(FleshImpactParticle, HitLocation, HitNormal);
				
				SpawnBloodDecalAt(HitLocation, HitNormal);
			}
		}
		else if (Cast<ABossEnemy>(HitActor))
		{
			if (Player && Player->GetCombatState() == ECombatState::Blocking)
			{
				// 불꽃 파티클 소환 (방어 성공)
				GWorld->GetPlayerCameraManager()->StartCameraShake(0.3, 0.02, 0.015, 15);
				SpawnImpactParticle(BlockImpactParticle, HitLocation, HitNormal);
			}
			else
			{
				// 피 파티클 소환 (피격)
				GWorld->GetPlayerCameraManager()->StartCameraShake(0.3, 0.01, 0.01, 10);
				SpawnImpactParticle(FleshImpactParticle, HitLocation, HitNormal);

				SpawnBloodDecalAt(HitLocation, HitNormal);
			}
		}
	}

	UE_LOG("[Character] Weapon hit: %s at (%.2f, %.2f, %.2f) - Damage: %.1f",
		   HitActor->GetName().c_str(), HitLocation.X, HitLocation.Y, HitLocation.Z, DamageInfo.Damage);

	// 델리게이트 브로드캐스트 (FDamageInfo 전달)
	OnWeaponHit.Broadcast(HitActor, DamageInfo);
}

void ACharacter::OnWeaponColliderOverlap(AActor* OtherActor, const FVector& HitLocation, const FVector& HitNormal)
{
	// 무기 트레이스가 비활성화 상태면 무시
	if (!bWeaponTraceActive)
	{
		return;
	}

	// 자기 자신은 무시
	if (OtherActor == this)
	{
		return;
	}

	// 이미 이번 스윙에서 맞은 액터는 무시 (중복 히트 방지)
	if (HitActorsThisSwing.Contains(OtherActor))
	{
		return;
	}

	// 히트 처리
	HitActorsThisSwing.Add(OtherActor);

	//UE_LOG("[Character] WeaponCollider overlap with: %s", OtherActor->GetName().c_str());

	// 기존 OnWeaponHitDetected 로직 재사용
	OnWeaponHitDetected(OtherActor, HitLocation, HitNormal);
}

void ACharacter::UpdateSubWeaponTransform()
{
	if (!SubWeaponMeshComp || !SkeletalMeshComp)
	{
		return;
	}

	// 스켈레탈 메시가 로드되었는지 확인
	if (!SkeletalMeshComp->GetSkeletalMesh())
	{
		return;
	}

	// 본 인덱스 찾기
	int32 BoneIndex = SkeletalMeshComp->GetBoneIndexByName(FName(SubWeaponBoneName));
	if (BoneIndex < 0)
	{
		return;
	}

	// 본의 월드 트랜스폼 가져오기
	FTransform BoneTransform = SkeletalMeshComp->GetBoneWorldTransform(BoneIndex);

	// 오프셋 적용
	FVector FinalLocation = BoneTransform.Translation +
		BoneTransform.Rotation.RotateVector(SubWeaponOffset);

	// 회전 오프셋 적용 (오일러 → 쿼터니언)
	FQuat RotOffset = FQuat::MakeFromEulerZYX(SubWeaponRotationOffset);
	FQuat FinalRotation = BoneTransform.Rotation * RotOffset;

	// 무기 트랜스폼 설정
	SubWeaponMeshComp->SetWorldLocation(FinalLocation);
	SubWeaponMeshComp->SetWorldRotation(FinalRotation);
}

// ============================================================================
// 무기 디버그 렌더링 데이터 관리
// ============================================================================

void ACharacter::UpdateWeaponDebugData(const FVector& BasePos, const FVector& TipPos,
									   const FVector& PrevBase, const FVector& PrevTip,
									   const FQuat& Rotation)
{
	WeaponDebugData.CurrentBasePos = BasePos;
	WeaponDebugData.CurrentTipPos = TipPos;
	WeaponDebugData.PrevBasePos = PrevBase;
	WeaponDebugData.PrevTipPos = PrevTip;
	WeaponDebugData.WeaponRotation = Rotation;
	WeaponDebugData.TraceRadius = WeaponTraceRadius;
	WeaponDebugData.bIsValid = true;
}

void ACharacter::ClearWeaponDebugData()
{
	WeaponDebugData.bIsValid = false;
}