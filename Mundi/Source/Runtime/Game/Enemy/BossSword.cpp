#include "pch.h"
#include "BossSword.h"
#include "World.h"
#include "StaticMeshComponent.h"
#include "HitboxComponent.h"
#include "CombatTypes.h"
#include "PlayerCharacter.h"

ABossSword::ABossSword()
{
    // 기본값 설정 (단위: 미터)
    HoverOffset = FVector();
    HoverHeight = 3.f;       // 3m 높이
    HoverDuration = 0.6f;    // 0.6초
    LaunchSpeed = 3.f;      // 30m/s
    Damage = 60.f;
}

void ABossSword::BeginPlay()
{
    Super::BeginPlay();

    bIsHovering = true;
    bHasLaunched = false;
    HoverTimer = 0.f;
    FloatPhase = static_cast<float>(rand()) / RAND_MAX * 3.14159f * 2.f;

    // 보스 찾기
    if (UWorld* World = GetWorld())
    {
        BossActor = World->FindActorByName(FName("Valthor, Luminous Blacksteel"));
        if (BossActor)
        {
            UE_LOG("[BossSword] Boss found!");
        }
        else
        {
            UE_LOG("[BossSword] WARNING: Boss not found!");
        }
    }

    // 컴포넌트 찾기
    for (UActorComponent* Comp : GetOwnedComponents())
    {
        if (!MeshComponent)
        {
            MeshComponent = Cast<UStaticMeshComponent>(Comp);
        }
        if (!HitboxComponent)
        {
            HitboxComponent = Cast<UHitboxComponent>(Comp);
        }
    }

    UE_LOG("[BossSword] Spawned with offset (%.1f, %.1f, %.1f)", HoverOffset.X, HoverOffset.Y, HoverOffset.Z);
}

void ABossSword::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Lua에서 모든 위치/이동 처리하므로 C++에서는 아무것도 안함
    
    if (bIsHovering)
    {
        HoverTimer += DeltaSeconds;
        FloatPhase += DeltaSeconds * 3.f;

        // Hover 시간이 끝나면 플레이어를 향해 발사
      /*  if (HoverTimer >= HoverDuration)
        {
            LaunchTowardPlayer();
            return;
        }*/

        // 보스 위치 따라가기 + 오프셋 적용
        if (BossActor)
        {
            FVector BossPos = BossActor->GetActorLocation();

            // 위아래 둥둥 떠다니는 효과
            float FloatZ = sinf(FloatPhase) * 0.5f;
            float FloatX = cosf(FloatPhase * 0.7f) * 0.5f;

            FVector NewLocation;
            NewLocation.X = BossPos.X + HoverOffset.X + FloatX;
            NewLocation.Y = BossPos.Y + HoverOffset.Y;
            NewLocation.Z = BossPos.Z + HoverHeight + FloatZ;

            SetActorLocation(NewLocation);
        }
    }
    /*
    else if (bHasLaunched)
    {
        // 발사 중 - 직선 이동
        FVector CurrentPos = GetActorLocation();
        FVector NewPos = CurrentPos + LaunchDirection * LaunchSpeed * DeltaSeconds;
        SetActorLocation(NewPos);

        // 일정 거리 이동 후 삭제 (30m)
        float TraveledDistance = (NewPos - LaunchStartPos).Size();
        if (TraveledDistance > 30.f)
        {
            Destroy();
        }
    }
    */
}

void ABossSword::SetHoverOffset(float X, float Y)
{
    HoverOffset.X = X;
    HoverOffset.Y = Y;
    UE_LOG("[BossSword] Offset set to (%.1f, %.1f)", X, Y);
}

void ABossSword::SetHoverOffset(const FVector& Offset)
{
    HoverOffset = Offset;
    UE_LOG("[BossSword] Offset set to (%.1f, %.1f, %.1f)", Offset.X, Offset.Y, Offset.Z);
}

void ABossSword::SetHoverHeight(float Height)
{
    HoverHeight = Height;
}

void ABossSword::SetHoverDuration(float Duration)
{
    HoverDuration = Duration;
    UE_LOG("[BossSword] HoverDuration set to %.2f", Duration);
}

void ABossSword::LaunchTowardPlayer()
{
    bHasLaunched = true;
    bIsHovering = false;
    LaunchStartPos = GetActorLocation();  // 발사 시작 위치 저장

    // 플레이어 방향 계산
    if (UWorld* World = GetWorld())
    {
        AActor* PlayerActor = World->FindActorByName(FName("플레이어 캐릭터_0"));
        if (!PlayerActor)
        {
            // "Player" 이름으로 못 찾으면 다른 방법 시도
            PlayerActor = World->FindActorByName(FName("Shinobi"));
        }

        if (APlayerCharacter* Player = Cast<APlayerCharacter>(PlayerActor))
        {
            FVector MyPos = GetActorLocation();
            FVector TargetPos = Player->GetActorLocation();
            TargetPos.Z += 1.f; // 플레이어 중심 높이 (1m)

            LaunchDirection = (TargetPos - MyPos).GetNormalized();

            // 칼 회전 (진행 방향)
            float Yaw = atan2f(LaunchDirection.Y, LaunchDirection.X) * 180.f / 3.14159f;
            SetActorRotation(FVector(0, 90.f, Yaw));

            UE_LOG("[BossSword] Launched toward player at (%.1f, %.1f, %.1f)!", TargetPos.X, TargetPos.Y, TargetPos.Z);
        }
        else
        {
            UE_LOG("[BossSword] ERROR: Player not found! Launching forward.");
            // 플레이어를 못 찾으면 보스가 바라보는 방향으로 발사
            if (BossActor)
            {
                LaunchDirection = BossActor->GetActorForward();
            }
            else
            {
                LaunchDirection = FVector(1.f, 0.f, 0.f);  // 기본값: X축 방향
            }
        }
    }

    // 히트박스 활성화
    if (HitboxComponent)
    {
        FDamageInfo DamageInfo;
        DamageInfo.Damage = Damage;
        DamageInfo.DamageType = EDamageType::Heavy;
        DamageInfo.HitReaction = EHitReaction::Knockback;
        DamageInfo.StaggerDuration = 0.5f;
        HitboxComponent->EnableHitbox(DamageInfo, 1.0f);
    }
}
