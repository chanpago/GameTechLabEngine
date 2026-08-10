#pragma once
#include "Actor.h"
#include "AGameModeBase.generated.h"

class APawn;
class ACharacter;
class AController;
class APlayerController;
class AGameState;
class AGameStateBase;


class AGameModeBase : public AActor
{
public:
	GENERATED_REFLECTION_BODY()

	AGameModeBase();
	~AGameModeBase() override;

	APawn* DefaultPawn;
	APlayerController* PlayerController;

	UClass* DefaultPawnClass;
	UClass* PlayerControllerClass;

	// Runtime instance of the game state
	UClass* GameStateClass;
	AGameStateBase* GameState = nullptr;

	// 게임 시작 시 호출
	virtual void StartPlay();

	// 매 프레임 업데이트
	virtual void Tick(float DeltaSeconds) override;

	AGameStateBase* GetGameState() const { return GameState; }

	// 게임 재개 (일시정지 해제)
	void ResumeGame();

	// 게임 재시작
	void RestartGame();

	// 게임 종료
	void QuitGame();

private:
	// 키 입력 상태 저장 (이전 프레임)
	bool bWasEscPressed = false;
	bool bWasRPressed = false;
	bool bWasQPressed = false;
	bool bWasAnyKeyPressed = false;

public:
	// 플레이어 리스폰할 때 사용
	//virtual void RestartPlayer(APlayerController* NewPlayer); 		 

protected:
	
	// 플레이어 접속 처리(PlayerController생성)
	virtual APlayerController* Login();

	// 접속 후 폰 스폰 및 빙의
	virtual void PostLogin(APlayerController* NewPlayer);

	// 기본 폰 스폰
	APawn* SpawnDefaultPawnFor(AController* NewPlayer, AActor* StartSpot);
	// 시작 지점 찾기
	AActor* FindPlayerStart(AController* Player); 
};