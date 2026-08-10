#include "pch.h"
#include "GameState.h"
#include "GameStateBase.h"
#include "GameModeBase.h"

#include "World.h"
#include "PlayerController.h"
#include "Pawn.h"
#include "Character.h"
#include "PlayerCameraManager.h"
#include "Source/Runtime/InputCore/InputManager.h"
#include "Source/Runtime/Core/Misc/PathUtils.h"
#include "Source/Runtime/Game/Enemy/EnemyBase.h"
#include "Source/Runtime/Game/Enemy/BossEnemy.h"

void AGameState::SetGameFlowState(EGameFlowState NewState)
{
    if (GameFlowState == NewState)
    {
        return;
    }
    GameFlowState = NewState;
    // Reset the base timer for state-based animations/logic
    StateTimeSeconds = 0.0f; // from AGameStateBase (protected)
}

void AGameState::EnterPressAnyKey()
{
    ShowEndScreen(false, false);
    ShowStartScreen(true);
    SetPaused(true);
    SetGameFlowState(EGameFlowState::PressAnyKey);

    // 마우스 표시 (메뉴 상태이므로)
    UInputManager::GetInstance().SetCursorVisible(true);
    UInputManager::GetInstance().ReleaseCursor();

    // Immediate black screen using camera fade (PIE only desired behavior)
    if (GWorld && GWorld->GetPlayerCameraManager())
    {
        GWorld->GetPlayerCameraManager()->FadeOut(0.0f, FLinearColor(0,0,0,1.0f));
    }
}

void AGameState::EnterMainMenu()
{
    SetGameFlowState(EGameFlowState::MainMenu);
    // 마우스 표시
    UInputManager::GetInstance().SetCursorVisible(true);
    UInputManager::GetInstance().ReleaseCursor();
}

void AGameState::EnterStartMenu()
{
    // 호환성을 위해 EnterPressAnyKey로 리디렉션
    EnterPressAnyKey();
}

void AGameState::StartFight()
{
    ShowStartScreen(false);
    ShowEndScreen(false, false);
    SetPaused(false);
    SetGameFlowState(EGameFlowState::Fighting);

    // 전투 시작 시 플레이어/몬스터 틱 활성화
    SetGameplayActorsTickEnabled(true);
}

void AGameState::EnterBossIntro()
{
    ShowStartScreen(false);
    SetPaused(false);
    SetGameFlowState(EGameFlowState::BossIntro);

    // 인트로 중에는 플레이어/몬스터 틱 비활성화
    SetGameplayActorsTickEnabled(false);

    // 마우스 숨기고 잠금 (게임플레이 시작)
    UInputManager::GetInstance().SetCursorVisible(false);
    UInputManager::GetInstance().LockCursor();

    // 검정 화면에서 시작하고 천천히 페이드인 (2초에 걸쳐)
    if (GWorld && GWorld->GetPlayerCameraManager())
    {
        // 먼저 완전 검정 화면으로
        GWorld->GetPlayerCameraManager()->FadeOut(0.0f, FLinearColor(0,0,0,1.0f));
        // 2초에 걸쳐 페이드인
        GWorld->GetPlayerCameraManager()->FadeIn(BossIntroBannerTime, FLinearColor(0,0,0,1.0f));
    }
}

void AGameState::EnterVictory()
{
    bPlayerWon = true;
    ShowEndScreen(true, /*bPlayerWon*/true);
    SetPaused(true);
    SetGameFlowState(EGameFlowState::Victory);

    // 마우스 표시 및 잠금 해제
    UInputManager::GetInstance().SetCursorVisible(true);
    UInputManager::GetInstance().ReleaseCursor();
}

void AGameState::EnterDefeat()
{
    bPlayerWon = false;
    ShowEndScreen(true, /*bPlayerWon*/false);
    SetPaused(true);
    SetGameFlowState(EGameFlowState::Defeat);

    // 마우스 표시 및 잠금 해제
    UInputManager::GetInstance().SetCursorVisible(true);
    UInputManager::GetInstance().ReleaseCursor();
}

void AGameState::OnPlayerLogin(APlayerController* InController)
{
    AGameStateBase::OnPlayerLogin(InController);
}

void AGameState::OnPawnPossessed(APawn* InPawn)
{
    AGameStateBase::OnPawnPossessed(InPawn);
}

void AGameState::OnPlayerDied()
{
    bPlayerAlive = false;
    EnterDefeat();
}

void AGameState::OnPlayerHealthChanged(float Current, float Max)
{
    // Only trigger vignette if health actually decreased (player took damage)
    const bool bTookDamage = Current < PlayerHealth.Current;

    PlayerHealth.Set(Current, Max);

    if (bTookDamage)
    {
        // Duration=3.0s, Radius=0.1, Softness=1.0, Intensity=1.0, Roundness=2.0, Color=Black, Priority=0, FadeIn=0.5s, FadeOut=0.5s
        World->GetPlayerCameraManager()->StartVignette(3.0f, 0.1f, 1.0f, 0.4f, 2.0f, FLinearColor(133.0f/255.0f,21.0f/255.0f,13.0f/255.0f,1.0f), 0, 0.5f, 0.5f);
    }

    bPlayerAlive = (Current > 0.0f);
    if (!bPlayerAlive)
    {
        OnPlayerDied();
    }
}

void AGameState::OnPlayerStaminaChanged(float Current, float Max)
{
    PlayerStamina.Set(Current, Max);
}

void AGameState::OnPlayerFocusChanged(float Current, float Max)
{
    PlayerFocus.Set(Current, Max);
}

void AGameState::RegisterBoss(const FString& InBossName, float BossMaxHealth)
{
    BossName = InBossName;
    BossHealth.Set(BossMaxHealth, BossMaxHealth);
    bBossActive = true;
}

void AGameState::UnregisterBoss()
{
    bBossActive = false;
    BossName.clear();
    BossHealth.Set(0.0f, 0.0f);
}

void AGameState::OnBossHealthChanged(float Current)
{
    BossHealth.Current = Current;
    if (BossHealth.Current <= 0.0f && bBossActive)
    {
        bBossActive = false;
        EnterVictory();
    }
}

void AGameState::ShowStartScreen(bool bShow)
{
    bStartScreenVisible = bShow;
}

void AGameState::ShowEndScreen(bool bShow, bool bInPlayerWon)
{
    bEndScreenVisible = bShow;
    if (bShow)
    {
        bPlayerWon = bInPlayerWon;
    }
}

void AGameState::HandleStateTick(float DeltaTime)
{
    // Simple timed transition from BossIntro to Fighting
    if (GameFlowState == EGameFlowState::BossIntro)
    {
        // 처음 0.3초 동안은 캐릭터가 스폰되기를 기다림
        if (StateTimeSeconds > 0.3f && GWorld)
        {
            // 보스 찾기
            ABossEnemy* Boss = nullptr;
            for (AActor* Actor : GWorld->GetActors())
            {
                if (ABossEnemy* BossActor = Cast<ABossEnemy>(Actor))
                {
                    Boss = BossActor;
                    break;
                }
            }

            // 플레이어 찾기
            APawn* PlayerPawn = nullptr;
            if (AGameModeBase* GM = GWorld->GetGameMode())
            {
                if (GM->PlayerController)
                {
                    PlayerPawn = GM->PlayerController->GetPawn();
                }
            }

            // 플레이어가 보스를 바라보도록 회전
            if (Boss && PlayerPawn)
            {
                FVector PlayerPos = PlayerPawn->GetActorLocation();
                FVector BossPos = Boss->GetActorLocation();
                FVector Direction = BossPos - PlayerPos;
                Direction.Z = 0.0f;  // 수평 방향만

                if (Direction.SizeSquared() > 0.01f)
                {
                    Direction.Normalize();
                    // Yaw 각도 계산
                    float Yaw = std::atan2(Direction.Y, Direction.X);
                    FQuat TargetRot = FQuat::FromAxisAngle(FVector(0, 0, 1), Yaw);

                    // 부드럽게 회전 (Lerp)
                    FQuat CurrentRot = PlayerPawn->GetActorRotation();
                    FQuat NewRot = FQuat::Slerp(CurrentRot, TargetRot, DeltaTime * 3.0f);
                    PlayerPawn->SetActorRotation(NewRot);
                }
            }
        }

        if (StateTimeSeconds >= BossIntroBannerTime)
        {
            StartFight();
        }
    }

    // PressAnyKey: in PIE, continue on any key press (legacy comment compatibility)
    if (GameFlowState == EGameFlowState::PressAnyKey && GWorld && GWorld->bPie)
    {
        UInputManager& Input = UInputManager::GetInstance();
        bool bAnyPressed = false;
        for (int key = 0; key < 256; ++key)
        {
            if (Input.IsKeyPressed(key)) { bAnyPressed = true; break; }
        }
        if (bAnyPressed)
        {
            EnterMainMenu();
            return;
        }
    }

    // Clamp health/stamina/focus values for safety
    if (PlayerHealth.Current < 0.0f) PlayerHealth.Current = 0.0f;
    if (PlayerHealth.Current > PlayerHealth.Max) PlayerHealth.Current = PlayerHealth.Max;
    if (PlayerStamina.Current < 0.0f) PlayerStamina.Current = 0.0f;
    if (PlayerStamina.Current > PlayerStamina.Max) PlayerStamina.Current = PlayerStamina.Max;
    if (PlayerFocus.Current < 0.0f) PlayerFocus.Current = 0.0f;
    if (PlayerFocus.Current > PlayerFocus.Max) PlayerFocus.Current = PlayerFocus.Max;
    if (BossHealth.Current < 0.0f) BossHealth.Current = 0.0f;
    if (BossHealth.Current > BossHealth.Max) BossHealth.Current = BossHealth.Max;

    // Debug keys for testing death/victory screens (only during Fighting state)
    if (GameFlowState == EGameFlowState::Fighting)
    {
        UInputManager& Input = UInputManager::GetInstance();
        if (Input.IsKeyPressed('K'))
        {
            EnterDefeat();
        }
        if (Input.IsKeyPressed('L'))
        {
            EnterVictory();
        }
        // Debug key to test boss health bar - M to decrease health by 10%
        if (Input.IsKeyPressed('M'))
        {
            // Register a test boss if none is active
            if (!bBossActive)
            {
                RegisterBoss("Test Boss", 100.0f);
            }
            // Decrease health by 10% of max
            float NewHealth = BossHealth.Current - (BossHealth.Max * 0.1f);
            OnBossHealthChanged(NewHealth);
        }

        // Debug keys for player HP/Stamina - H to decrease HP, J to decrease stamina
        if (Input.IsKeyPressed('H'))
        {
            // Initialize player health if not set
            if (PlayerHealth.Max <= 0.0f)
            {
                PlayerHealth.Set(100.0f, 100.0f);
            }
            float NewHealth = PlayerHealth.Current - (PlayerHealth.Max * 0.1f);
            OnPlayerHealthChanged(NewHealth, PlayerHealth.Max);
        }
        if (Input.IsKeyPressed('J'))
        {
            // Initialize player stamina if not set
            if (PlayerStamina.Max <= 0.0f)
            {
                PlayerStamina.Set(100.0f, 100.0f);
            }
            float NewStamina = PlayerStamina.Current - (PlayerStamina.Max * 0.15f);
            OnPlayerStaminaChanged(NewStamina, PlayerStamina.Max);
        }
        // Debug key for focus - N to decrease focus by 20%
        if (Input.IsKeyPressed('N'))
        {
            // Initialize player focus if not set
            if (PlayerFocus.Max <= 0.0f)
            {
                PlayerFocus.Set(100.0f, 100.0f);
            }
            float NewFocus = PlayerFocus.Current - (PlayerFocus.Max * 0.2f);
            OnPlayerFocusChanged(NewFocus, PlayerFocus.Max);
        }
    }
}

void AGameState::SetGameplayActorsTickEnabled(bool bEnabled)
{
    if (!GWorld)
    {
        return;
    }

    for (AActor* Actor : GWorld->GetActors())
    {
        if (!Actor)
        {
            continue;
        }

        // 플레이어(Character) 또는 적(EnemyBase) 타입인지 확인
        bool bIsGameplayActor = false;

        if (AEnemyBase* Enemy = Cast<AEnemyBase>(Actor))
        {
            bIsGameplayActor = true;
        }
        else if (ACharacter* Character = Cast<ACharacter>(Actor))
        {
            // EnemyBase가 아닌 Character는 플레이어
            bIsGameplayActor = true;
        }

        if (bIsGameplayActor)
        {
            Actor->SetActorActive(bEnabled);
        }
    }
}
