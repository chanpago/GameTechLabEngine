#pragma once

#include "EnemyBase.h"
#include "ABasicEnemy.generated.h"

class UAnimMontage;

// ============================================================================
// ABasicEnemy - 기본 추적+공격 적
// ============================================================================
// 단순히 플레이어를 감지하면 따라가고, 범위 안에 들어오면 공격
// ============================================================================

UCLASS(DisplayName = "기본 적", Description = "추적+공격만 하는 기본 적")
class ABasicEnemy : public AEnemyBase
{
public:
    GENERATED_REFLECTION_BODY()

    ABasicEnemy();
    virtual ~ABasicEnemy() = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
    virtual void OnDeath() override;

    // ========================================================================
    // Lua 바인딩 함수들
    // ========================================================================

    /** 몽타주 이름으로 재생 (Lua용) */
    bool PlayMontageByName(const FString& MontageName, float BlendIn = 0.1f, float BlendOut = 0.1f, float PlayRate = 1.0f);

    /** 현재 재생 중인 몽타주의 재생 속도 변경 (Lua용) */
    void SetMontagePlayRate(float NewPlayRate);

    // ========================================================================
    // 애니메이션
    // ========================================================================
    UPROPERTY(EditAnywhere, Category = "Animation")
    FString AttackAnimPath;

    UAnimMontage* AttackMontage = nullptr;

    // ========================================================================
    // 스탯 설정
    // ========================================================================
    UPROPERTY(EditAnywhere, Category = "Stats")
    float BasicEnemyMaxHealth = 50.f;

    UPROPERTY(EditAnywhere, Category = "Stats")
    float BasicEnemyDamage = 10.f;

    // 공격 지속 시간 (애니메이션 없을 때 사용)
    float AttackDuration = 1.0f;
    float AttackTimeRemaining = 0.f;
};
