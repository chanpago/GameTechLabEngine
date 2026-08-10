#include "pch.h"
#include "GameModeBase.h"
#include "PlayerController.h"
#include "Pawn.h"
#include "World.h"
#include "CameraComponent.h"
#include "SpringArmComponent.h"
#include "PlayerCameraManager.h"
#include "Character.h"
#include "Source/Runtime/Core/Misc/PathUtils.h"
#include "GameState.h"
#include "InputManager.h"
#include "Source/Runtime/Game/Enemy/BossEnemy.h"
#include "Source/Runtime/Game/Combat/StatsComponent.h"
#include "UIManager.h"
#include <Windows.h>

AGameModeBase::AGameModeBase()
{
	//DefaultPawnClass = APawn::StaticClass();
	DefaultPawnClass = ACharacter::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
    GameStateClass = AGameState::StaticClass();
}

AGameModeBase::~AGameModeBase()
{
}

void AGameModeBase::StartPlay()
{
	// Ensure GameState exists (spawn using configured class)
	if (!GameStateClass)
	{
		GameStateClass = AGameState::StaticClass();
	}
	if (!GameState)
	{
		if (AActor* GSActor = GetWorld()->SpawnActor(GameStateClass, FTransform()))
		{
			GameState = Cast<AGameStateBase>(GSActor);
			if (GameState)
			{
				GameState->Initialize(GetWorld(), this);
			}
		}
	}

	// Player login + spawn/pawn possession
	Login();
	PostLogin(PlayerController);

	// Notify GameState of initial controller/pawn
	if (GameState && PlayerController)
	{
		GameState->OnPlayerLogin(PlayerController);
		if (APawn* P = PlayerController->GetPawn())
		{
			GameState->OnPawnPossessed(P);
		}
	}

	// Initialize flow: 에디터(PIE)에서는 바로 전투 시작, 스탠드얼론에서는 시작 메뉴
	if (GameState)
	{
		if (AGameState* GS = Cast<AGameState>(GameState))
		{
#ifdef _EDITOR
			// 에디터(PIE)에서는 바로 전투 시작
			GS->StartFight();
#else
			// 스탠드얼론(릴리즈)에서는 시작 메뉴 표시
			GS->EnterStartMenu();
			GS->SetGameplayActorsTickEnabled(false);
#endif
		}
	}
}

APlayerController* AGameModeBase::Login()
{
	if (PlayerControllerClass)
	{
		PlayerController = Cast<APlayerController>(GWorld->SpawnActor(PlayerControllerClass, FTransform())); 
	}
	else
	{
		PlayerController = GWorld->SpawnActor<APlayerController>(FTransform());
	}

	return PlayerController;
}

void AGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	// 스폰 위치 찾기
	AActor* StartSpot = FindPlayerStart(NewPlayer);
	APawn* NewPawn = NewPlayer->GetPawn();

	// Pawn이 없으면 생성
    if (!NewPawn && NewPlayer)
    {
        // Try spawning a default prefab as the player's pawn first
        {
            FWideString PrefabPath = UTF8ToWide(GDataDir) + L"/Prefabs/Shinobi.prefab";
            if (AActor* PrefabActor = GWorld->SpawnPrefabActor(PrefabPath))
            {
                if (APawn* PrefabPawn = Cast<APawn>(PrefabActor))
                {
                    NewPawn = PrefabPawn;
                }
            }
        }

        // Fallback to default pawn class if prefab did not yield a pawn
        if (!NewPawn)
        {
            NewPawn = SpawnDefaultPawnFor(NewPlayer, StartSpot);
        }

        if (NewPawn)
        {
            NewPlayer->Possess(NewPawn);
        }

    }

	// SpringArm + Camera 구조로 부착
	if (NewPawn && !NewPawn->GetComponent(USpringArmComponent::StaticClass()))
	{
		// 1. SpringArm 생성 및 RootComponent에 부착
		USpringArmComponent* SpringArm = nullptr;
		if (UActorComponent* SpringArmComp = NewPawn->AddNewComponent(USpringArmComponent::StaticClass(), NewPawn->GetRootComponent()))
		{
			SpringArm = Cast<USpringArmComponent>(SpringArmComp);
			SpringArm->SetRelativeLocation(FVector(0, 0, 0.8f));  // 캐릭터 머리 위쪽
			SpringArm->SetTargetArmLength(3.0f);                  // 카메라 거리 (스프링 암에서 카메라 컴포넌트까지의)
			SpringArm->SetDoCollisionTest(true);                  // 충돌 체크 활성화
			SpringArm->SetUsePawnControlRotation(true);           // 컨트롤러 회전 사용
		}

		// 2. Camera를 SpringArm에 부착
		if (SpringArm)
		{
			if (UActorComponent* CameraComp = NewPawn->AddNewComponent(UCameraComponent::StaticClass(), SpringArm))
			{
				auto* Camera = Cast<UCameraComponent>(CameraComp);
				// 카메라는 SpringArm 끝에 위치 (SpringArm이 위치 계산)
				Camera->SetRelativeLocation(FVector(0, 0, 0));
				Camera->SetRelativeRotationEuler(FVector(0, 0, 0));
			}
		}
	}

	if (auto* PCM = GWorld->GetPlayerCameraManager())
	{
		if (auto* Camera = Cast<UCameraComponent>(NewPawn->GetComponent(UCameraComponent::StaticClass())))
		{
			PCM->SetViewCamera(Camera);
		}
	}

	// Possess를 수행 
	if (NewPlayer)
	{
		NewPlayer->Possess(NewPlayer->GetPawn());
	}

	// Inform GameState of the possessed pawn
	if (GameState && NewPlayer)
	{
		if (APawn* P = NewPlayer->GetPawn())
		{
			GameState->OnPawnPossessed(P);
		}
	}
}

APawn* AGameModeBase::SpawnDefaultPawnFor(AController* NewPlayer, AActor* StartSpot)
{
	// 위치 결정
	FVector SpawnLoc = FVector::Zero();
	FQuat SpawnRot = FQuat::Identity();

	if (StartSpot)
	{
		SpawnLoc = StartSpot->GetActorLocation();
		SpawnRot = StartSpot->GetActorRotation();
	}
	 
	APawn* ResultPawn = nullptr;
	if (DefaultPawnClass)
	{
		ResultPawn = Cast<APawn>(GetWorld()->SpawnActor(DefaultPawnClass, FTransform(SpawnLoc, SpawnRot, FVector(1, 1, 1))));
	}

	return ResultPawn;
}

AActor* AGameModeBase::FindPlayerStart(AController* Player)
{
	// TODO: PlayerStart Actor를 찾아서 반환하도록 구현 필요
	return nullptr;
}

void AGameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!GameState)
	{
		return;
	}

	AGameState* GS = Cast<AGameState>(GameState);
	if (!GS)
	{
		return;
	}

	EGameFlowState CurrentState = GS->GetGameFlowState();

	// ========================================================================
	// 입력 처리 (키를 눌렀다 뗐을 때만 반응)
	// ========================================================================

	// ESC - 일시정지 토글
	bool bEscPressed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
	if (bEscPressed && !bWasEscPressed)
	{
		if (CurrentState == EGameFlowState::Fighting)
		{
			GS->SetGameFlowState(EGameFlowState::Paused);
			GWorld->SetPaused(true);
			// 마우스 커서 표시 및 잠금 해제
			UInputManager::GetInstance().SetCursorVisible(true);
			UInputManager::GetInstance().ReleaseCursor();
		}
		else if (CurrentState == EGameFlowState::Paused)
		{
			GS->SetGameFlowState(EGameFlowState::Fighting);
			GWorld->SetPaused(false);
			// 마우스 커서 숨김 및 잠금
			UInputManager::GetInstance().SetCursorVisible(false);
			UInputManager::GetInstance().LockCursor();
		}
	}
	bWasEscPressed = bEscPressed;

	// R - 게임 재시작
	bool bRPressed = (GetAsyncKeyState('G') & 0x8000) != 0;
	if (bRPressed && !bWasRPressed)
	{
		RestartGame();
	}
	bWasRPressed = bRPressed;

	// Q - 게임 종료
	bool bQPressed = (GetAsyncKeyState('Q') & 0x8000) != 0;
	if (bQPressed && !bWasQPressed)
	{
		QuitGame();
	}
	bWasQPressed = bQPressed;

	// PressAnyKey에서 아무 키나 누르면 메인 메뉴로
	if (CurrentState == EGameFlowState::PressAnyKey)
	{
		bool bAnyKeyPressed = false;
		for (int i = 0; i < 256; i++)
		{
			if (GetAsyncKeyState(i) & 0x8000)
			{
				bAnyKeyPressed = true;
				break;
			}
		}

		if (bAnyKeyPressed && !bWasAnyKeyPressed)
		{
			GS->EnterMainMenu();
		}
		bWasAnyKeyPressed = bAnyKeyPressed;
	}

	// Victory/Defeat 화면에서 R키로 재시작, Q키로 종료
	if (CurrentState == EGameFlowState::Victory || CurrentState == EGameFlowState::Defeat)
	{
		// R키와 Q키는 위에서 이미 처리됨
	}
}

void AGameModeBase::ResumeGame()
{
	if (!GWorld)
	{
		return;
	}

	AGameState* GS = Cast<AGameState>(GameState);
	if (!GS)
	{
		return;
	}

	// 일시정지 해제
	GWorld->SetPaused(false);
	GS->SetGameFlowState(EGameFlowState::Fighting);

	// 마우스 숨기고 잠그기
	UInputManager::GetInstance().SetCursorVisible(false);
	UInputManager::GetInstance().LockCursor();
}

void AGameModeBase::RestartGame()
{
	if (!GWorld)
	{
		return;
	}

	// 다음 프레임에 재시작 처리 (현재 프레임에서 처리하면 this가 삭제되어 크래시)
	GWorld->RequestRestart();
}

void AGameModeBase::QuitGame()
{
	// 게임 종료
	exit(0);
}

