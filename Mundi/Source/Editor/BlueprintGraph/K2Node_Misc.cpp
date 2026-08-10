#include "pch.h"
#include "ImGui/imgui_stdlib.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "K2Node_Misc.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintEvaluator.h"
#include "SkeletalMeshComponent.h"
#include "Source/Runtime/Engine/Animation/AnimInstance.h"
#include "Source/Runtime/Engine/Components/InputComponent.h"
#include "Source/Runtime/Core/Object/PlayerController.h"
#include "Source/Runtime/Game/Player/PlayerCharacter.h"
#include "World.h"

// ----------------------------------------------------------------
//	Internal Helper: 컨텍스트에서 InputComponent 탐색
// ----------------------------------------------------------------
static UInputComponent* GetInputComponentFromContext(FBlueprintContext* Context)
{
    if (!Context || !Context->SourceObject)
    {
        return nullptr;
    }

    UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context->SourceObject);
    if (!AnimInstance)
    {
        return nullptr;
    }

    USkeletalMeshComponent* MeshComp = AnimInstance->GetOwningComponent();
    if (!MeshComp)
    {
        return nullptr;
    }

    AActor* OwnerActor = MeshComp->GetOwner();
    if (!OwnerActor)
    {
        return nullptr;
    }

    UWorld* World = OwnerActor->GetWorld();
    if (!World)
    {
        return nullptr;
    }

    // FindActor를 사용하여 PlayerController 찾기
    APlayerController* PC = World->FindActor<APlayerController>();
    if (!PC)
    {
        return nullptr;
    }

    return PC->GetInputComponent();
}

// ----------------------------------------------------------------
//	Internal Helper: 컨텍스트에서 PlayerCharacter 탐색
// ----------------------------------------------------------------
static APlayerCharacter* GetPlayerCharacterFromContext(FBlueprintContext* Context)
{
    if (!Context || !Context->SourceObject)
    {
        return nullptr;
    }

    UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context->SourceObject);
    if (!AnimInstance)
    {
        return nullptr;
    }

    USkeletalMeshComponent* MeshComp = AnimInstance->GetOwningComponent();
    if (!MeshComp)
    {
        return nullptr;
    }

    AActor* OwnerActor = MeshComp->GetOwner();
    if (!OwnerActor)
    {
        return nullptr;
    }

    return Cast<APlayerCharacter>(OwnerActor);
}

// ----------------------------------------------------------------
//	[Input] 키보드 입력 확인 노드
// ----------------------------------------------------------------

static int GetKeyCodeFromStr(const FString& InKeyName)
{
    // #1. 특수 키 매핑 (Static으로 선언하여 초기화 비용 절약)
    static const TMap<FString, int32> SpecialKeys = {
        { "Space", VK_SPACE },
        { "Enter", VK_RETURN },
        { "Return", VK_RETURN },
        { "Tab", VK_TAB },
        { "Esc", VK_ESCAPE },
        { "Escape", VK_ESCAPE },
        { "Shift", VK_SHIFT },
        { "Ctrl", VK_CONTROL },
        { "Alt", VK_MENU },
        { "Left", VK_LEFT },
        { "Right", VK_RIGHT },
        { "Up", VK_UP },
        { "Down", VK_DOWN },
        // 필요한 키 추가...
    };

    FString KeyStr = InKeyName;

    // #2. 맵에서 특수 키 검색
    auto It = SpecialKeys.find(KeyStr);
    if (It != SpecialKeys.end())
    {
        return It->second;
    }

    // #3. 단일 문자 처리 (A-Z, 0-9)
    if (KeyStr.length() == 1)
    {
        char C = std::toupper(KeyStr[0]);
        if ((C >= '0' && C <= '9') || (C >= 'A' && C <= 'Z'))
        {
            return static_cast<int>(C);
        }
    }

    return 0; // 알 수 없는 키
}

IMPLEMENT_CLASS(UK2Node_IsPressed)

UK2Node_IsPressed::UK2Node_IsPressed()
{
    TitleColor = ImColor(220, 48, 48);
}

void UK2Node_IsPressed::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadString(InOutHandle, "KeyName", KeyName);
    }
    else
    {
        InOutHandle["KeyName"] = KeyName;
    }
}

void UK2Node_IsPressed::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_IsPressed::RenderBody()
{
    ImGui::PushItemWidth(150.0f);
    ImGui::InputText("키 이름", &KeyName);
    ImGui::PopItemWidth();
}

FBlueprintValue UK2Node_IsPressed::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        int32 KeyCode = GetKeyCodeFromStr(KeyName);
        if (KeyCode == 0)
        {
            return false;
        }
        bool bIsPressed = UInputManager::GetInstance().IsKeyPressed(KeyCode);

        return bIsPressed;
    }

    return FBlueprintValue{};
}

void UK2Node_IsPressed::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_IsKeyDown)

UK2Node_IsKeyDown::UK2Node_IsKeyDown()
{
    TitleColor = ImColor(220, 48, 48);
}

void UK2Node_IsKeyDown::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadString(InOutHandle, "KeyName", KeyName);
    }
    else
    {
        InOutHandle["KeyName"] = KeyName;
    }
}

void UK2Node_IsKeyDown::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_IsKeyDown::RenderBody()
{
    ImGui::PushItemWidth(150.0f);
    ImGui::InputText("키 이름", &KeyName);
    ImGui::PopItemWidth();
}

FBlueprintValue UK2Node_IsKeyDown::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        int32 KeyCode = GetKeyCodeFromStr(KeyName);
        if (KeyCode == 0)
        {
            return false;
        }
        
        bool bIsDown = UInputManager::GetInstance().IsKeyDown(KeyCode);

        return bIsDown;
    }

    return FBlueprintValue{};
}

void UK2Node_IsKeyDown::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Input] 액션 입력 확인 노드 (InputComponent 기반)
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_IsActionPressed)

UK2Node_IsActionPressed::UK2Node_IsActionPressed()
{
    TitleColor = ImColor(48, 180, 48);  // 녹색 계열로 구분
}

void UK2Node_IsActionPressed::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadString(InOutHandle, "ActionName", ActionName);
    }
    else
    {
        InOutHandle["ActionName"] = ActionName;
    }
}

void UK2Node_IsActionPressed::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_IsActionPressed::RenderBody()
{
    ImGui::PushItemWidth(150.0f);
    ImGui::InputText("액션 이름", &ActionName);
    ImGui::PopItemWidth();
}

FBlueprintValue UK2Node_IsActionPressed::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        UInputComponent* InputComp = GetInputComponentFromContext(Context);
        if (!InputComp)
        {
            return false;
        }

        bool bIsPressed = InputComp->IsActionPressed(FName(ActionName));
        return bIsPressed;
    }

    return FBlueprintValue{};
}

void UK2Node_IsActionPressed::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

IMPLEMENT_CLASS(UK2Node_IsActionDown)

UK2Node_IsActionDown::UK2Node_IsActionDown()
{
    TitleColor = ImColor(48, 180, 48);  // 녹색 계열로 구분
}

void UK2Node_IsActionDown::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    UK2Node::Serialize(bInIsLoading, InOutHandle);

    if (bInIsLoading)
    {
        FJsonSerializer::ReadString(InOutHandle, "ActionName", ActionName);
    }
    else
    {
        InOutHandle["ActionName"] = ActionName;
    }
}

void UK2Node_IsActionDown::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Result");
}

void UK2Node_IsActionDown::RenderBody()
{
    ImGui::PushItemWidth(150.0f);
    ImGui::InputText("액션 이름", &ActionName);
    ImGui::PopItemWidth();
}

FBlueprintValue UK2Node_IsActionDown::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Result")
    {
        UInputComponent* InputComp = GetInputComponentFromContext(Context);
        if (!InputComp)
        {
            return false;
        }

        bool bIsDown = InputComp->IsActionDown(FName(ActionName));
        return bIsDown;
    }

    return FBlueprintValue{};
}

void UK2Node_IsActionDown::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Jump] 점프 상태 확인 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_IsJumpPending)

UK2Node_IsJumpPending::UK2Node_IsJumpPending()
{
    TitleColor = ImColor(100, 149, 237);  // 코발트 블루
}

void UK2Node_IsJumpPending::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Bool, "Is Pending");
}

void UK2Node_IsJumpPending::RenderBody()
{
    // 추가 UI 없음
}

FBlueprintValue UK2Node_IsJumpPending::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    if (OutputPin->PinName == "Is Pending")
    {
        APlayerCharacter* PlayerChar = GetPlayerCharacterFromContext(Context);
        if (!PlayerChar)
        {
            return false;
        }

        return PlayerChar->IsJumpPending();
    }

    return FBlueprintValue{};
}

void UK2Node_IsJumpPending::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Input] 마우스 위치 확인 노드
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_GetMousePosition)

UK2Node_GetMousePosition::UK2Node_GetMousePosition()
{
    TitleColor = ImColor(147, 226, 74);
}

void UK2Node_GetMousePosition::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "X");
    CreatePin(EEdGraphPinDirection::EGPD_Output, FEdGraphPinCategory::Float, "Y");
}

void UK2Node_GetMousePosition::RenderBody()
{
}

FBlueprintValue UK2Node_GetMousePosition::EvaluatePin(const UEdGraphPin* OutputPin, FBlueprintContext* Context)
{
    FVector2D Position = UInputManager::GetInstance().GetMousePosition();

    if (OutputPin->PinName == "X")
    {
        return FBlueprintValue(Position.X);    
    }
    else if (OutputPin->PinName == "Y")
    {
        return FBlueprintValue(Position.Y);
    }

    return FBlueprintValue{};
}

void UK2Node_GetMousePosition::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());

    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();

    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Debug] Watch Int
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Watch_Int)

UK2Node_Watch_Int::UK2Node_Watch_Int()
{
    TitleColor = ImColor(100, 100, 100);
}

void UK2Node_Watch_Int::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Int, "Value", "0");
}

void UK2Node_Watch_Int::RenderBody()
{
    UEdGraphPin* InputPin = FindPin("Value");
    int32 CurrentValue = FBlueprintEvaluator::EvaluateInput<int32>(InputPin, nullptr);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Value: %d", CurrentValue);
}

void UK2Node_Watch_Int::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Debug] Watch Float
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Watch_Float)

UK2Node_Watch_Float::UK2Node_Watch_Float()
{
    TitleColor = ImColor(100, 100, 100);
}

void UK2Node_Watch_Float::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Float, "Value", "0.0");
}

void UK2Node_Watch_Float::RenderBody()
{
    UEdGraphPin* InputPin = FindPin("Value");
    float CurrentValue = FBlueprintEvaluator::EvaluateInput<float>(InputPin, nullptr);

    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Value: %.3f", CurrentValue);
}

void UK2Node_Watch_Float::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}

// ----------------------------------------------------------------
//	[Debug] Watch Bool
// ----------------------------------------------------------------

IMPLEMENT_CLASS(UK2Node_Watch_Bool)

UK2Node_Watch_Bool::UK2Node_Watch_Bool()
{
    TitleColor = ImColor(100, 100, 100);
}

void UK2Node_Watch_Bool::AllocateDefaultPins()
{
    CreatePin(EEdGraphPinDirection::EGPD_Input, FEdGraphPinCategory::Bool, "Value", "false");
}

void UK2Node_Watch_Bool::RenderBody()
{
    UEdGraphPin* InputPin = FindPin("Value");
    bool bValue = FBlueprintEvaluator::EvaluateInput<bool>(InputPin, nullptr);

    if (bValue)
    {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "TRUE");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "FALSE");
    }
}

void UK2Node_Watch_Bool::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
    UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
    Spawner->MenuName = GetNodeTitle();
    Spawner->Category = GetMenuCategory();
    ActionRegistrar.AddAction(Spawner);
}