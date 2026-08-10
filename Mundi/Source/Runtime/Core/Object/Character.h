#pragma once
#include "Pawn.h"
#include "Source/Runtime/Game/Combat/CombatTypes.h"
#include "ACharacter.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UCharacterMovementComponent;
class UParticleSystem;
class UTexture;
class ADecalActor;

UCLASS(DisplayName = "캐릭터", Description = "캐릭터 액터")
class ACharacter : public APawn
{ 
public:
	GENERATED_REFLECTION_BODY()

	ACharacter();
	virtual ~ACharacter() override;

	virtual void Tick(float DeltaSecond) override;
    virtual void BeginPlay() override;
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
	void DuplicateSubObjects() override;

	// 캐릭터 고유 기능
	virtual void Jump();
	virtual void StopJumping();

	/** 피격 이펙트 파티클 */
	UParticleSystem* FleshImpactParticle = nullptr;

	/** 방어 이펙트 파티클 */
	UParticleSystem* BlockImpactParticle = nullptr;
	 
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
	UCharacterMovementComponent* GetCharacterMovement() const { return CharacterMovement; }

	// APawn 인터페이스: 파생 클래스의 MovementComponent를 노출
	virtual UPawnMovementComponent* GetMovementComponent() const override { return reinterpret_cast<UPawnMovementComponent*>(CharacterMovement); }

	//APawn에서 정의 됨
	USkeletalMeshComponent* GetMesh() const { return SkeletalMeshComp; }

	// ========================================================================
	// 무기 어태치 시스템
	// ========================================================================

	/** 무기 메시 컴포넌트 (컴포넌트는 별도 직렬화됨) */
	UStaticMeshComponent* WeaponMeshComp = nullptr;

	/** 무기 충돌 캡슐 컴포넌트 (PhysX 오버랩 기반 충돌) */
	UCapsuleComponent* WeaponCollider = nullptr;

	/** 무기가 부착될 본 이름 */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	FString WeaponBoneName = "hand_r";

	/** 무기 오프셋 (본 기준 로컬) */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	FVector WeaponOffset = FVector::Zero();

	/** 무기 회전 오프셋 (본 기준 로컬, 오일러 각도) */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	FVector WeaponRotationOffset = FVector::Zero();


	/** 서브 무기 메시 컴포넌트 (컴포넌트는 별도 직렬화됨) */
	UStaticMeshComponent* SubWeaponMeshComp = nullptr;
	
	/** 서브 무기가 부착될 본 이름 */
	UPROPERTY(EditAnywhere, Category = "SubWeapon")
	FString SubWeaponBoneName = "hand_l";

	/** 서브 무기 오프셋 (본 기준 로컬) */
	UPROPERTY(EditAnywhere, Category = "SubWeapon")
	FVector SubWeaponOffset = FVector::Zero();

	/** 서브 무기 회전 오프셋 (본 기준 로컬, 오일러 각도) */
	UPROPERTY(EditAnywhere, Category = "SubWeapon")
	FVector SubWeaponRotationOffset = FVector::Zero();
	

	/** 무기 위치를 본에 맞춰 업데이트 */
	void UpdateWeaponTransform();

	/** 서브 무기 위치를 본에 맞춰 업데이트 */
	void UpdateSubWeaponTransform();

	/** 무기 충돌 델리게이트 */
	DECLARE_DELEGATE(OnWeaponHit, AActor* /*HitActor*/, const FDamageInfo& /*DamageInfo*/);

	/** 현재 무기 공격의 데미지 정보 설정 */
	void SetWeaponDamageInfo(const FDamageInfo& InDamageInfo) { CurrentWeaponDamageInfo = InDamageInfo; }
	const FDamageInfo& GetWeaponDamageInfo() const { return CurrentWeaponDamageInfo; }

	/** 무기 Sweep 시작 (AnimNotify에서 호출, 미리 설정된 데미지 정보 사용) */
	void StartWeaponTrace();

	/** 무기 Sweep 종료 (AnimNotify에서 호출) */
	void EndWeaponTrace();

	/** 무기 Sweep 판정 수행 (Tick에서 호출) */
	void PerformWeaponTrace();

	/** 무기 Sweep 파라미터 */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	float WeaponTraceRadius = 0.1f;      // 무기 판정 반지름

	UPROPERTY(EditAnywhere, Category = "Weapon")
	float WeaponTraceLength = 0.8f;      // 무기 판정 길이

	/** 디버그 드로우 활성화 */
	UPROPERTY(EditAnywhere, Category = "Weapon")
	bool bDrawWeaponDebug = true;
	
	//// Decal 관련
	UPROPERTY(EditAnywhere, Category = "VFX")
	int InitialBloodDecalPoolSize = 2;

	UPROPERTY(EditAnywhere, Category = "VFX")
	float BloodDecalGroundZ = -0.f;

	UPROPERTY(EditAnywhere, Category = "VFX")
	float BloodDecalLifetime = 8.f;

	// ========================================================================
	// 무기 디버그 렌더링 데이터 (RenderDebugVolume에서 사용)
	// ========================================================================
	struct FWeaponDebugData
	{
		FVector CurrentBasePos;
		FVector CurrentTipPos;
		FVector PrevBasePos;
		FVector PrevTipPos;
		FQuat WeaponRotation;
		float TraceRadius;
		bool bIsValid = false;
	};
	FWeaponDebugData WeaponDebugData;

	/** 디버그 데이터 업데이트 (PerformWeaponTrace에서 호출) */
	void UpdateWeaponDebugData(const FVector& BasePos, const FVector& TipPos,
							   const FVector& PrevBase, const FVector& PrevTip,
							   const FQuat& Rotation);

	/** 디버그 데이터 초기화 */
	void ClearWeaponDebugData();

	/** WeaponCollider 오버랩 시 호출 (PhysX 오버랩 콜백) */
	void OnWeaponColliderOverlap(AActor* OtherActor, const FVector& HitLocation, const FVector& HitNormal);

	UTexture* BloodDecalTexture = nullptr;

	struct FBloodDecalEntry
	{
		ADecalActor* Actor;
		float RemainingLifetime = 0.0f;
		bool bInUse = false;
	};
	TArray<FBloodDecalEntry> BloodDecalPool;
	TArray<int32> BloodDecalFreeList;

    void InitializeBloodDecalPool();
	ADecalActor* AcquireBloodDecal();
	void ReleaseBloodDecal(ADecalActor* DecalActor);
	void SpawnBloodDecalAt(const FVector& HitLocation, const FVector& HitNormal);
	void UpdateBloodDecalPool(float DeltaSeconds);
	void ConfigureBloodDecalActor(ADecalActor* DecalActor);

protected:
	/** 무기 충돌 시 호출 */
	void OnWeaponHitDetected(AActor* HitActor, const FVector& HitLocation, const FVector& HitNormal);

	/** 이전 프레임 무기 위치 (Sweep용) */
	FVector PrevWeaponTipPos = FVector::Zero();
	FVector PrevWeaponBasePos = FVector::Zero();
	bool bWeaponTraceActive = false;

	/** 이번 공격에서 이미 맞은 액터들 (중복 히트 방지) */
	TArray<AActor*> HitActorsThisSwing;

	/** 현재 무기 공격의 데미지 정보 */
	FDamageInfo CurrentWeaponDamageInfo;

    UCapsuleComponent* CapsuleComponent;
    UCharacterMovementComponent* CharacterMovement;

    /** 초기화 대기 프레임 카운터 */
    int32 InitFrameCounter = 0;
};
