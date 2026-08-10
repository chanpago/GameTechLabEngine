#pragma once
#include "GameStateBase.h"
#include "AGameState.generated.h"

// Game-specific flow and UI state for the jam project
enum class EGameFlowState : uint8
{
    PressAnyKey,    // "Press Any Key" 화면
    MainMenu,       // 메인 메뉴 (게임 시작, 튜토리얼, 종료)
    BossIntro,
    Fighting,
    Paused,
    Victory,
    Defeat
};

struct FHealthState
{
    float Current = 0.0f;
    float Max = 0.0f;
    float GetPercent() const { return (Max > 0.0f) ? (Current / Max) : 0.0f; }
    void Set(float InCurrent, float InMax) { Current = InCurrent; Max = InMax; }
};

struct FStaminaState
{
    float Current = 0.0f;
    float Max = 0.0f;
    float GetPercent() const { return (Max > 0.0f) ? (Current / Max) : 0.0f; }
    float GetStamina() const { return Current; }
    void Set(float InCurrent, float InMax) { Current = InCurrent; Max = InMax; }
};

struct FFocusState
{
    float Current = 0.0f;
    float Max = 0.0f;
    float GetPercent() const { return (Max > 0.0f) ? (Current / Max) : 0.0f; }
    float GetFocus() const { return Current; }
    void Set(float InCurrent, float InMax) { Current = InCurrent; Max = InMax; }
};

class AGameState : public AGameStateBase
{
public:
    GENERATED_REFLECTION_BODY()

    AGameState() {};
    ~AGameState() override = default;

    // Flow control
    EGameFlowState GetGameFlowState() const { return GameFlowState; }
    void SetGameFlowState(EGameFlowState NewState);
    void EnterPressAnyKey();    // "Press Any Key" 화면
    void EnterMainMenu();       // 메인 메뉴
    void EnterStartMenu();      // (호환성 유지 - EnterPressAnyKey로 리디렉션)
    void StartFight();
    void EnterBossIntro();
    void EnterVictory();
    void EnterDefeat();

    // Notifications from GameMode/Controllers
    void OnPlayerLogin(APlayerController* InController) override;
    void OnPawnPossessed(APawn* InPawn) override;
    void OnPlayerDied() override;

    // Player health updates
    void OnPlayerHealthChanged(float Current, float Max);
    const FHealthState& GetPlayerHealth() const { return PlayerHealth; }

    // Player stamina updates
    void OnPlayerStaminaChanged(float Current, float Max);
    const FStaminaState& GetPlayerStamina() const { return PlayerStamina; }

    // Player focus updates
    void OnPlayerFocusChanged(float Current, float Max);
    const FFocusState& GetPlayerFocus() const { return PlayerFocus; }

    // Boss info and health
    void RegisterBoss(const FString& InBossName, float BossMaxHealth);
    void UnregisterBoss();
    void OnBossHealthChanged(float Current);
    bool HasActiveBoss() const { return bBossActive; }
    const FString& GetBossName() const { return BossName; }
    const FHealthState& GetBossHealth() const { return BossHealth; }

    // UI toggles for overlay
    void ShowStartScreen(bool bShow);
    void ShowEndScreen(bool bShow, bool bPlayerWon);
    bool IsStartScreenVisible() const { return bStartScreenVisible; }
    bool IsEndScreenVisible() const { return bEndScreenVisible; }
    bool DidPlayerWin() const { return bPlayerWon; }

    float GetStateElapsedTime() const { return StateTimeSeconds; }

    // 플레이어/몬스터 틱 활성화/비활성화
    void SetGameplayActorsTickEnabled(bool bEnabled);

protected:
    void HandleStateTick(float DeltaTime) override;

protected:
    // Flow
    EGameFlowState GameFlowState = EGameFlowState::PressAnyKey;

    // Player
    FHealthState PlayerHealth;
    FStaminaState PlayerStamina;
    FFocusState PlayerFocus;
    bool bPlayerAlive = true;

    // Boss
    FString BossName;
    FHealthState BossHealth;
    bool bBossActive = false;

    // UI flags
    bool bStartScreenVisible = true;
    bool bEndScreenVisible = false;
    bool bPlayerWon = false;

    // Timings (seconds)
    float StartFadeInDuration = 0.5f;
    float EndFadeInDuration = 0.6f;
    float BossIntroBannerTime = 2.0f;  // 게임 시작 전 대기 시간 (검정화면 페이드인)
};
