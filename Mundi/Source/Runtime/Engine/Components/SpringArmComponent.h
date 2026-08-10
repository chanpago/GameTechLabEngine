#pragma once

#include "SceneComponent.h"
#include "USpringArmComponent.generated.h"

class AActor;

UCLASS(DisplayName="스프링 암 컴포넌트", Description="카메라 붐/스프링 암 컴포넌트입니다")
class USpringArmComponent : public USceneComponent
{
public:

    GENERATED_REFLECTION_BODY()

    USpringArmComponent();

protected:
    ~USpringArmComponent() override;

public:
    void OnRegister(UWorld* InWorld) override;
    void TickComponent(float DeltaTime) override;

    // ──────────────────────────────
    // Spring Arm 설정
    // ──────────────────────────────

    /** 암의 기본 길이 (충돌이 없을 때의 거리) */
    void SetTargetArmLength(float InLength) { TargetArmLength = InLength; CurrentArmLength = InLength; }
    float GetTargetArmLength() const { return TargetArmLength; }

    /** 타겟 오프셋 (타겟 위치에서의 오프셋) */
    void SetTargetOffset(const FVector& InOffset) { TargetOffset = InOffset; }
    FVector GetTargetOffset() const { return TargetOffset; }

    /** 소켓 오프셋 (암 끝 위치에서의 오프셋) */
    void SetSocketOffset(const FVector& InOffset) { SocketOffset = InOffset; }
    FVector GetSocketOffset() const { return SocketOffset; }

    /** 충돌 테스트 활성화 여부 */
    void SetDoCollisionTest(bool bEnable) { bDoCollisionTest = bEnable; }
    bool DoesCollisionTest() const { return bDoCollisionTest; }

    /** 충돌 테스트용 프로브 반지름 */
    void SetProbeSize(float InSize) { ProbeSize = InSize; }
    float GetProbeSize() const { return ProbeSize; }

    /** 카메라 회전을 부모(Pawn)와 동기화할지 여부 */
    void SetUsePawnControlRotation(bool bUse) { bUsePawnControlRotation = bUse; }
    bool GetUsePawnControlRotation() const { return bUsePawnControlRotation; }

    // ──────────────────────────────
    // Lock-On 카메라 추적
    // ──────────────────────────────

    /** Lock-on 타겟 설정 */
    void SetLockOnTarget(AActor* Target) { LockOnTarget = Target; }
    AActor* GetLockOnTarget() const { return LockOnTarget; }
    void ClearLockOnTarget() { LockOnTarget = nullptr; }
    bool HasLockOnTarget() const { return LockOnTarget != nullptr; }

    /** Lock-on 회전을 즉시 적용 (Tick 순서 문제 해결용) */
    void ForceUpdateLockOnRotation(float DeltaTime);

    /** Lock-on 카메라 회전 속도 */
    UPROPERTY(EditAnywhere, Category="LockOn")
    float LockOnRotationSpeed = 40.0f;

    /** Lock-on 타겟 높이 오프셋 (카메라가 바라볼 위치) */
    UPROPERTY(EditAnywhere, Category="LockOn")
    FVector LockOnTargetOffset = FVector(0, 0, 100.0f);

    /** Lock-on 시 카메라 피치 오프셋 (약간 아래를 바라봄) */
    UPROPERTY(EditAnywhere, Category="LockOn")
    float LockOnPitchOffset = -5.0f;

    /** Whether to slightly adjust pitch for tall/short enemies (like giant bosses) */
    UPROPERTY(EditAnywhere, Category="LockOn")
    bool bAdjustPitchForTargetHeight = false;

    // ──────────────────────────────
    // 구르기(Rolling) 카메라 - 엘든링 스타일
    // ──────────────────────────────

    /** 구르기 상태 설정 (PlayerCharacter에서 호출) */
    void SetRollingState(bool bRolling);
    bool IsRolling() const { return bIsRolling; }

    /** 구르기 종료 후 블렌드 아웃 시간 (초) */
    UPROPERTY(EditAnywhere, Category="Rolling")
    float RollBlendOutDuration = 0.25f;

    /** 구르기 복귀 회전 속도 (블렌드 아웃 시) */
    UPROPERTY(EditAnywhere, Category="Rolling")
    float RollRecoveryRotationSpeed = 8.0f;

    // ──────────────────────────────
    // 결과 값 (읽기 전용)
    // ──────────────────────────────

    /** 충돌 계산 후 실제 암 길이 */
    float GetCurrentArmLength() const { return CurrentArmLength; }

    /** 자식 컴포넌트(카메라)가 부착될 소켓 로컬 위치 */
    FVector GetSocketLocalLocation() const;

    // ──────────────────────────────
    // Serialization
    // ──────────────────────────────
    void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

protected:
    /** 암 위치 및 충돌 계산 (보간 포함) */
    void UpdateDesiredArmLocation(float DeltaTime);

    /** 충돌 체크 후 암 길이 조정 */
    float CalculateArmLengthWithCollision(const FVector& Origin, const FVector& DesiredEnd);

    /** Lock-on 타겟을 향한 회전 계산 */
    FQuat CalculateLockOnRotation() const;

private:
    /** Lock-on 타겟 액터 */
    AActor* LockOnTarget = nullptr;
    /** 암의 기본 길이 */
    UPROPERTY(EditAnywhere, Category="SpringArm", Range="0.0, 1000.0")
    float TargetArmLength;

    /** 타겟 위치에서의 오프셋 */
    UPROPERTY(EditAnywhere, Category="SpringArm")
    FVector TargetOffset;

    /** 암 끝(소켓)에서의 오프셋 */
    UPROPERTY(EditAnywhere, Category="SpringArm")
    FVector SocketOffset;

    /** 충돌 테스트 활성화 여부 */
    UPROPERTY(EditAnywhere, Category="SpringArm")
    bool bDoCollisionTest;

    /** 충돌 체크용 구체 반지름 */
    UPROPERTY(EditAnywhere, Category="SpringArm", Range="0.1, 50.0")
    float ProbeSize;

    /** PlayerController의 ControlRotation을 사용할지 여부 */
    UPROPERTY(EditAnywhere, Category="SpringArm")
    bool bUsePawnControlRotation;

    /** 충돌 계산 후 실제 암 길이 */
    float CurrentArmLength;

    // ── 구르기 카메라 상태 ──
    /** 현재 구르기 중 여부 */
    bool bIsRolling = false;

    /** 구르기 시작 시 저장된 카메라 회전 (고정용) */
    FQuat FrozenRotation;

    /** 구르기 종료 후 블렌드 아웃 진행 중 여부 */
    bool bIsBlendingOutFromRoll = false;

    /** 블렌드 아웃 시작 시 저장된 회전 */
    FQuat BlendOutStartRotation;

    /** 블렌드 아웃 목표 회전 (구르기 끝난 시점에 고정) */
    FQuat BlendOutTargetRotation;

    /** 블렌드 아웃 타이머 */
    float RollBlendOutTimer = 0.0f;

    // ── 블렌드 아웃 후 이징 인 (부드러운 속도 복귀) ──
    /** 블렌드 아웃 종료 후 이징 인 중 여부 */
    bool bIsEasingInAfterRoll = false;

    /** 이징 인 타이머 */
    float EaseInTimer = 0.0f;

    /** 이징 인 지속 시간 (초) */
    float EaseInDuration = 0.3f;
};
