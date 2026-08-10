#pragma once

#include "Actor.h"
#include "ABossSword.generated.h"

class UStaticMeshComponent;
class UHitboxComponent;

// ============================================================================
// ABossSword - 보스 궁극기용 칼 투사체
// ============================================================================
UCLASS(DisplayName = "보스 칼", Description = "보스 궁극기 칼 투사체")
class ABossSword : public AActor
{
public:
    GENERATED_REFLECTION_BODY()

    ABossSword();
    virtual ~ABossSword() = default;

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // ========================================================================
    // 오프셋 설정 (원형 배치용)
    // ========================================================================

    /** 보스 기준 오프셋 설정 */
    void SetHoverOffset(float X, float Y);
    void SetHoverOffset(const FVector& Offset);

    /** 호버 높이 설정 */
    void SetHoverHeight(float Height);

    /** 호버 지속 시간 설정 (순차 발사용) */
    void SetHoverDuration(float Duration);

    /** 플레이어 방향으로 발사 */
    void LaunchTowardPlayer();

    /** 데미지 설정 */
    void SetDamage(float NewDamage) { Damage = NewDamage; }

    /** 호버링 상태 설정 (Lua에서 호출) */
    void SetIsHovering(bool bHovering) { bIsHovering = bHovering; }

protected:
    // ========== 오프셋 (보스 기준 원형 배치) ==========
    UPROPERTY(EditAnywhere, Category = "Hover")
    FVector HoverOffset = FVector();

    UPROPERTY(EditAnywhere, Category = "Hover")
    float HoverHeight = 3.f;  // 3m

    // ========== 호버링 ==========
    UPROPERTY(EditAnywhere, Category = "Hover")
    float HoverDuration = 0.6f;

    float HoverTimer = 0.f;
    bool bIsHovering = true;
    bool bHasLaunched = false;
    float FloatPhase = 0.f;

    // ========== 발사 ==========
    UPROPERTY(EditAnywhere, Category = "Launch")
    float LaunchSpeed = 30.f;  // 30m/s

    FVector LaunchDirection = FVector();
    FVector LaunchStartPos = FVector();  // 발사 시작 위치 (거리 계산용)

    // ========== 데미지 ==========
    UPROPERTY(EditAnywhere, Category = "Combat")
    float Damage = 60.f;

    // ========== 보스 참조 ==========
    AActor* BossActor = nullptr;

    // ========== 컴포넌트 ==========
    UStaticMeshComponent* MeshComponent = nullptr;
    UHitboxComponent* HitboxComponent = nullptr;
};
