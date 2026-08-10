#include "pch.h"
#include "SpringArmComponent.h"
#include "World.h"
#include "Actor.h"
#include "Source/Runtime/Engine/Collision/Collision.h"
#include "Source/Runtime/Engine/Physics/PhysScene.h"
#include "Source/Runtime/Core/Object/Pawn.h"
#include "Source/Runtime/Core/Object/Controller.h"
#include <cmath>

USpringArmComponent::USpringArmComponent()
    : TargetArmLength(300.0f)
    , TargetOffset(FVector::Zero())
    , SocketOffset(FVector::Zero())
    , bDoCollisionTest(true)
    , ProbeSize(0.5f)
    , bUsePawnControlRotation(true)
    , CurrentArmLength(300.0f)
{
    // TickComponent가 호출되도록 설정
    bCanEverTick = true;
}

USpringArmComponent::~USpringArmComponent()
{
}

void USpringArmComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);

    // 초기 암 길이 설정
    CurrentArmLength = TargetArmLength;
}

void USpringArmComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    // ═══════════════════════════════════════════════════════════
    // 엘든링 스타일 구르기 카메라 처리
    // ═══════════════════════════════════════════════════════════

    // 블렌드 아웃 타이머 업데이트
    if (bIsBlendingOutFromRoll)
    {
        RollBlendOutTimer += DeltaTime;
        if (RollBlendOutTimer >= RollBlendOutDuration)
        {
            bIsBlendingOutFromRoll = false;
            RollBlendOutTimer = 0.0f;

            // 블렌드 아웃 끝 → 이징 인 시작 (부드러운 속도 복귀)
            if (LockOnTarget)
            {
                bIsEasingInAfterRoll = true;
                EaseInTimer = 0.0f;
            }
        }
    }

    // 이징 인 타이머 업데이트
    if (bIsEasingInAfterRoll)
    {
        EaseInTimer += DeltaTime;
        if (EaseInTimer >= EaseInDuration)
        {
            bIsEasingInAfterRoll = false;
            EaseInTimer = 0.0f;
        }
    }

    // ── 케이스 1: 구르기 중 ──
    if (bIsRolling)
    {
        if (LockOnTarget)
        {
            // Lock-on + Rolling: 아주 느리게 타겟 추적 (구르기 끝날 때 보간 거리 줄이기)
            FQuat DesiredRotation = CalculateLockOnRotation();
            FQuat CurrentRotation = GetWorldRotation();

            // 기본 속도의 15%로 느리게 추적
            float SlowSpeed = LockOnRotationSpeed * 0.15f;
            FQuat NewRotation = FQuat::Slerp(CurrentRotation, DesiredRotation,
                                              FMath::Clamp(DeltaTime * SlowSpeed, 0.0f, 1.0f));
            SetWorldRotation(NewRotation);
        }
        else
        {
            // Non-lock-on + Rolling: 완전 고정
            SetWorldRotation(FrozenRotation);
        }
    }
    // ── 케이스 2: 구르기 종료 후 블렌드 아웃 중 ──
    else if (bIsBlendingOutFromRoll)
    {
        if (LockOnTarget)
        {
            // 블렌드 아웃 진행률 (0 → 1)
            float BlendAlpha = RollBlendOutTimer / RollBlendOutDuration;
            BlendAlpha = FMath::Clamp(BlendAlpha, 0.0f, 1.0f);

            // 부드러운 ease-out 커브 적용
            BlendAlpha = 1.0f - std::pow(1.0f - BlendAlpha, 2.0f);

            // 시작 회전 → 고정된 목표 회전으로 보간 (플레이어 이동 무시)
            FQuat NewRotation = FQuat::Slerp(BlendOutStartRotation, BlendOutTargetRotation, BlendAlpha);
            SetWorldRotation(NewRotation);
        }
        // Non-lock-on은 회전 변경 없음 (플레이어 입력에 의해서만 회전)
    }
    // ── 케이스 3: 일반 상태 (구르기 아님) ──
    // Lock-on 회전은 ForceUpdateLockOnRotation()에서 처리 (Tick 순서 문제 방지)
    // Non-lock-on은 PlayerController가 회전 담당
    // 여기서는 아무것도 안 함

    // ═══════════════════════════════════════════════════════════

    // 매 프레임 암 위치 업데이트 (보간 포함)
    UpdateDesiredArmLocation(DeltaTime);

    // 자식 컴포넌트(카메라)의 로컬 위치를 암 끝으로 설정
    // SpringArm의 로컬 좌표계에서 -X 방향(뒤쪽)으로 CurrentArmLength만큼
    FVector SocketLocalLocation = FVector(-CurrentArmLength, 0, 0) + SocketOffset;

    for (USceneComponent* Child : GetAttachChildren())
    {
        if (Child)
        {
            Child->SetRelativeLocation(SocketLocalLocation);
        }
    }
}

void USpringArmComponent::UpdateDesiredArmLocation(float DeltaTime)
{
    // SpringArm의 월드 위치 (피벗 포인트)에서 카메라 방향으로 레이 발사
    FVector Origin = GetWorldLocation() + TargetOffset;

    // 암의 방향 계산 (뒤쪽으로 뻗음, -X 방향이 뒤)
    // SpringArm의 회전을 기준으로 뒤쪽 방향 계산
    FQuat ArmRotation = GetWorldRotation();
    FVector BackwardDir = ArmRotation.RotateVector(FVector(-1, 0, 0));

    // 목표 암 끝 위치
    FVector DesiredEnd = Origin + BackwardDir * TargetArmLength;

    // 충돌 체크로 목표 암 길이 계산
    float DesiredArmLength = TargetArmLength;
    if (bDoCollisionTest)
    {
        DesiredArmLength = CalculateArmLengthWithCollision(Origin, DesiredEnd);
    }

    // 부드러운 보간으로 CurrentArmLength 업데이트
    // 충돌 시(줄어들 때)는 빠르게, 복귀 시(늘어날 때)는 느리게
    const float ShrinkSpeed = 15.0f;  // 충돌 시 줄어드는 속도
    const float GrowSpeed = 5.0f;     // 복귀 시 늘어나는 속도

    if (DesiredArmLength < CurrentArmLength)
    {
        // 충돌로 인해 줄어들어야 함 - 빠르게
        float Alpha = FMath::Clamp(DeltaTime * ShrinkSpeed, 0.0f, 1.0f);
        CurrentArmLength = FMath::Lerp(CurrentArmLength, DesiredArmLength, Alpha);
    }
    else
    {
        // 충돌 없어서 원래 길이로 복귀 - 느리게
        float Alpha = FMath::Clamp(DeltaTime * GrowSpeed, 0.0f, 1.0f);
        CurrentArmLength = FMath::Lerp(CurrentArmLength, DesiredArmLength, Alpha);
    }
}

float USpringArmComponent::CalculateArmLengthWithCollision(const FVector& Origin, const FVector& DesiredEnd)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return TargetArmLength;
    }

    FPhysScene* PhysScene = World->GetPhysScene();
    if (!PhysScene)
    {
        return TargetArmLength;
    }

    // Owner Actor 가져오기 (충돌 무시용)
    AActor* OwnerActor = GetOwner();

    // SweepSphere로 충돌 체크
    FHitResult HitResult;
    bool bHit = PhysScene->SweepSphere(
        Origin,
        DesiredEnd,
        ProbeSize,
        HitResult,
        OwnerActor
    );

    if (bHit && HitResult.bBlockingHit)
    {
        // 충돌 시 암 길이를 충돌 지점까지로 줄임
        // HitResult.Time은 0~1 사이 값으로, 얼마나 진행했는지 나타냄
        float HitArmLength = TargetArmLength * HitResult.Time;

        // 최소 거리 보장 (너무 가까워지지 않도록)
        const float MinArmLength = ProbeSize * 2.0f;
        return FMath::Max(HitArmLength, MinArmLength);
    }

    return TargetArmLength;
}

FVector USpringArmComponent::GetSocketLocalLocation() const
{
    // 로컬 좌표계에서 암 끝 위치 계산
    // -X 방향(뒤쪽)으로 CurrentArmLength만큼 + SocketOffset
    return FVector(-CurrentArmLength, 0, 0) + SocketOffset;
}

void USpringArmComponent::SetRollingState(bool bRolling)
{
    if (bRolling && !bIsRolling)
    {
        // 구르기 시작: 현재 회전 저장
        FrozenRotation = GetWorldRotation();
        bIsBlendingOutFromRoll = false;
        RollBlendOutTimer = 0.0f;
        // 이징 인 중이었다면 취소
        bIsEasingInAfterRoll = false;
        EaseInTimer = 0.0f;
    }
    else if (!bRolling && bIsRolling)
    {
        // 구르기 종료: 블렌드 아웃 시작
        BlendOutStartRotation = GetWorldRotation();
        bIsBlendingOutFromRoll = true;
        RollBlendOutTimer = 0.0f;

        // Lock-on인 경우, 구르기 끝난 시점의 목표 회전을 저장 (이동에 따른 변화 무시)
        if (LockOnTarget)
        {
            BlendOutTargetRotation = CalculateLockOnRotation();
        }
    }

    bIsRolling = bRolling;
}

void USpringArmComponent::ForceUpdateLockOnRotation(float DeltaTime)
{
    if (!LockOnTarget)
    {
        return;
    }

    // 구르기 중 또는 블렌드 아웃 중이면 TickComponent에서 처리
    if (bIsRolling || bIsBlendingOutFromRoll)
    {
        return;
    }

    FQuat DesiredRotation = CalculateLockOnRotation();
    FQuat CurrentRotation = GetWorldRotation();

    // 이징 인 중이면 속도를 점진적으로 올림 (느리게 시작 → 정상 속도)
    float EffectiveSpeed = LockOnRotationSpeed;
    if (bIsEasingInAfterRoll)
    {
        float EaseAlpha = EaseInTimer / EaseInDuration;
        EaseAlpha = FMath::Clamp(EaseAlpha, 0.0f, 1.0f);
        // ease-in 커브: 느리게 시작해서 점점 빨라짐
        EaseAlpha = EaseAlpha * EaseAlpha;
        // 최소 속도 10%에서 시작하여 100%까지 증가
        EffectiveSpeed = LockOnRotationSpeed * (0.1f + 0.9f * EaseAlpha);
    }

    // 부드러운 회전 보간
    FQuat NewRotation = FQuat::Slerp(CurrentRotation, DesiredRotation,
                                      FMath::Clamp(DeltaTime * EffectiveSpeed, 0.0f, 1.0f));
    SetWorldRotation(NewRotation);
}

FQuat USpringArmComponent::CalculateLockOnRotation() const
{
    if (!LockOnTarget)
    {
        return GetWorldRotation();
    }

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return GetWorldRotation();
    }

    // Player and target positions
    FVector PlayerPos = Owner->GetActorLocation();
    FVector TargetPos = LockOnTarget->GetActorLocation() + LockOnTargetOffset;

    // Horizontal direction to target (for yaw only)
    FVector ToTarget = TargetPos - PlayerPos;
    ToTarget.Z = 0;

    if (ToTarget.IsZero())
    {
        return GetWorldRotation();
    }

    ToTarget.Normalize();

    // Yaw: rotate horizontally to face target
    float TargetYaw = std::atan2(ToTarget.Y, ToTarget.X) * (180.0f / PI);

    // Pitch: Dark Souls style - mostly fixed angle, only slight adjustment for very tall/short enemies
    // Use LockOnPitchOffset as the base pitch (default: looking slightly down at action)
    float TargetPitch = LockOnPitchOffset;

    // Optional: subtle pitch adjustment for extreme height differences (e.g., giant bosses)
    if (bAdjustPitchForTargetHeight)
    {
        FVector FullToTarget = TargetPos - (PlayerPos + FVector(0, 0, TargetOffset.Z));
        float HorizontalDist = FVector(FullToTarget.X, FullToTarget.Y, 0).Size();
        float HeightDiff = FullToTarget.Z;

        if (HorizontalDist > 0.01f)
        {
            // Very subtle adjustment - only 20% of the actual angle, clamped tightly
            float HeightAngle = std::atan2(HeightDiff, HorizontalDist) * (180.0f / PI);
            float PitchAdjustment = HeightAngle * 0.2f;
            PitchAdjustment = FMath::Clamp(PitchAdjustment, -15.0f, 15.0f);
            TargetPitch += PitchAdjustment;
        }
    }

    TargetPitch = FMath::Clamp(TargetPitch, -45.0f, 30.0f);

    return FQuat::MakeFromEulerZYX(FVector(0.0f, TargetPitch, TargetYaw));
}

void USpringArmComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadFloat(InOutHandle, "TargetArmLength", TargetArmLength, 300.0f);
        FJsonSerializer::ReadVector(InOutHandle, "TargetOffset", TargetOffset, FVector::Zero());
        FJsonSerializer::ReadVector(InOutHandle, "SocketOffset", SocketOffset, FVector::Zero());
        FJsonSerializer::ReadBool(InOutHandle, "bDoCollisionTest", bDoCollisionTest, true);
        FJsonSerializer::ReadFloat(InOutHandle, "ProbeSize", ProbeSize, 12.0f);
        FJsonSerializer::ReadBool(InOutHandle, "bUsePawnControlRotation", bUsePawnControlRotation, true);
    }
    else
    {
        InOutHandle["TargetArmLength"] = TargetArmLength;
        InOutHandle["TargetOffset"] = FJsonSerializer::VectorToJson(TargetOffset);
        InOutHandle["SocketOffset"] = FJsonSerializer::VectorToJson(SocketOffset);
        InOutHandle["bDoCollisionTest"] = bDoCollisionTest;
        InOutHandle["ProbeSize"] = ProbeSize;
        InOutHandle["bUsePawnControlRotation"] = bUsePawnControlRotation;
    }
}
