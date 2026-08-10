#pragma once

#include "ActorComponent.h"
#include <functional>
#include <unordered_map>

#include "InputManager.h"

// 입력 이벤트 타입
enum class EInputEvent
{
    Pressed,    // 키를 누른 순간
    Released,   // 키를 뗀 순간
    Held        // 키를 누르고 있는 동안
};

// 액션 바인딩 정보
struct FInputActionBinding
{
    FName ActionName;
    EInputEvent EventType;
    std::function<void()> Callback;
};

// 축 바인딩 정보
struct FInputAxisBinding
{
    FName AxisName;
    std::function<void(float)> Callback;
    float Scale = 1.0f;
};

// 키-액션 매핑
struct FInputActionKeyMapping
{
    FName ActionName;
    int32 Key;          // Virtual key code (VK_SPACE, 'W', etc.)
    bool bShift = false;
    bool bCtrl = false;
    bool bAlt = false;
};

// 키-축 매핑
struct FInputAxisKeyMapping
{
    FName AxisName;
    int32 Key;
    float Scale = 1.0f; // 양수/음수로 방향 지정
};

// Gamepad action mapping
struct FInputActionGamepadMapping
{
    FName ActionName;
    UInputManager::EGamepadButton Button;
};

// Gamepad axis mapping
struct FInputAxisGamepadMapping
{
    FName AxisName;
    UInputManager::EGamepadAxis Axis;
    float Scale = 1.0f;
};

// ============================================================================
// UInputComponent - 입력 바인딩 및 처리
// ============================================================================
class UInputComponent : public UActorComponent
{
public:
    DECLARE_CLASS(UInputComponent, UActorComponent)

    UInputComponent();
    virtual ~UInputComponent();

    virtual void TickComponent(float DeltaSeconds) override;

    // ========================================================================
    // 액션 바인딩 (단발성 입력: Jump, Dodge, Attack 등)
    // ========================================================================
    template<typename T>
    void BindAction(const FName& ActionName, EInputEvent EventType, T* Object, void(T::*Func)())
    {
        FInputActionBinding Binding;
        Binding.ActionName = ActionName;
        Binding.EventType = EventType;
        Binding.Callback = [Object, Func]() { (Object->*Func)(); };
        ActionBindings.Add(Binding);
    }

    void BindAction(const FName& ActionName, EInputEvent EventType, std::function<void()> Callback);

    // ========================================================================
    // 축 바인딩 (연속 입력: Move, Look 등)
    // ========================================================================
    template<typename T>
    void BindAxis(const FName& AxisName, T* Object, void(T::*Func)(float))
    {
        FInputAxisBinding Binding;
        Binding.AxisName = AxisName;
        Binding.Callback = [Object, Func](float Value) { (Object->*Func)(Value); };
        AxisBindings.Add(Binding);
    }

    void BindAxis(const FName& AxisName, std::function<void(float)> Callback);

    // ========================================================================
    // 키 매핑 설정
    // ========================================================================
    void MapActionToKey(const FName& ActionName, int32 Key, bool bShift = false, bool bCtrl = false, bool bAlt = false);
    void MapAxisToKey(const FName& AxisName, int32 Key, float Scale = 1.0f);

    // Gamepad mappings
    void MapActionToGamepad(const FName& ActionName, UInputManager::EGamepadButton Button);
    void MapAxisToGamepad(const FName& AxisName, UInputManager::EGamepadAxis Axis, float Scale = 1.0f);

    // 마우스 축 매핑
    void MapAxisToMouseX(const FName& AxisName, float Scale = 1.0f);
    void MapAxisToMouseY(const FName& AxisName, float Scale = 1.0f);

    // ========================================================================
    // 바인딩 해제
    // ========================================================================
    void ClearActionBindings();
    void ClearAxisBindings();
    void ClearAllBindings();

    // ========================================================================
    // 입력 활성화/비활성화
    // ========================================================================
    void SetInputEnabled(bool bEnabled) { bInputEnabled = bEnabled; }
    bool IsInputEnabled() const { return bInputEnabled; }

    // 특정 액션 블록
    void BlockAction(const FName& ActionName);
    void UnblockAction(const FName& ActionName);
    bool IsActionBlocked(const FName& ActionName) const;

    // ========================================================================
    // 액션 상태 조회 (애니메이션 블루프린트용)
    // ========================================================================
    /** 액션이 이번 프레임에 눌렸는지 (Pressed) */
    bool IsActionPressed(const FName& ActionName) const;
    /** 액션이 눌려있는지 (Held/Down) */
    bool IsActionDown(const FName& ActionName) const;
    /** 액션이 이번 프레임에 떼어졌는지 (Released) */
    bool IsActionReleased(const FName& ActionName) const;

private:
    void ProcessActionBindings();
    void ProcessAxisBindings();

    bool CheckKeyState(int32 Key, EInputEvent EventType);
    bool CheckModifiers(const FInputActionKeyMapping& Mapping);
    float GetAxisValue(const FName& AxisName);

private:
    TArray<FInputActionBinding> ActionBindings;
    TArray<FInputAxisBinding> AxisBindings;

    TArray<FInputActionKeyMapping> ActionKeyMappings;
    TArray<FInputAxisKeyMapping> AxisKeyMappings;
    TArray<FInputActionGamepadMapping> ActionGamepadMappings;
    TArray<FInputAxisGamepadMapping> AxisGamepadMappings;

    // 마우스 축 매핑
    FName MouseXAxisName;
    FName MouseYAxisName;
    float MouseXScale = 1.0f;
    float MouseYScale = 1.0f;

    bool bInputEnabled = true;
    TArray<FName> BlockedActions;

    // 이전 프레임 키 상태 (Pressed/Released 판단용)
    std::unordered_map<int32, bool> PreviousKeyStates;
};
