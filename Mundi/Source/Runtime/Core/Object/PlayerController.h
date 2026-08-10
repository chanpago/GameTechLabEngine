#pragma once
#include "Controller.h"
#include "APlayerController.generated.h"

class UTargetingComponent;
class UInputComponent;

class APlayerController : public AController
{
public:
	GENERATED_REFLECTION_BODY()

	APlayerController();
	virtual ~APlayerController() override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void SetupInput();

	// ========================================================================
	// Input Component
	// ========================================================================
	UInputComponent* GetInputComponent() const { return InputComponent; }

	// ========================================================================
	// Targeting System
	// ========================================================================
	UTargetingComponent* GetTargetingComponent() const { return TargetingComponent; }

	/** Lock-on 상태 (애니메이션 그래프에서 사용) */
	bool bIsLockOn = false;

protected:
	// 입력 콜백 함수들
	void OnMoveForward(float Value);
	void OnMoveRight(float Value);
	void OnLookUp(float Value);
	void OnLookRight(float Value);
	void OnJump();
	void OnStopJump();
	void OnDodge();
	void OnToggleLockOn();
	void OnSwitchTargetLeft();
	void OnSwitchTargetRight();
	void OnToggleMouseLook();
	void OnAttack();
	void OnDashAttack();
	void OnUltimateAttack();
	void OnStartSprint();
	void OnStopSprint();
	void OnStartBlock();
	void OnStopBlock();
	void OnStartCharging();
	void OnStopCharging();
	void OnDrinkPotion();

    void ProcessRotationInput(float DeltaTime);
    void ProcessLockedMovement(float DeltaTime, const FVector& WorldMoveDir);
	void ApplyMovement(float DeltaTime);

protected:
    bool bMouseLookEnabled = true;

	// Input Component
	UInputComponent* InputComponent = nullptr;

    // Targeting Component (replaces old bIsLockOn/LockedYaw)
    UTargetingComponent* TargetingComponent = nullptr;

    // Smooth rotation speed when locked on
    float CharacterRotationSpeed = 10.0f;

	// 축 입력 값 저장
	float MoveForwardValue = 0.0f;
	float MoveRightValue = 0.0f;
	float LookUpValue = 0.0f;
	float LookRightValue = 0.0f;

private:
	float Sensitivity = 0.1f;
};
