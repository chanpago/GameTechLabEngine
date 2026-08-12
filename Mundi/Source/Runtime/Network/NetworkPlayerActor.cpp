#include "pch.h"
#include "NetworkPlayerActor.h"

#include "NetworkManager.h"
#include "StaticMeshComponent.h"
#include "World.h"

IMPLEMENT_CLASS(ANetworkPlayerActor)

ANetworkPlayerActor::ANetworkPlayerActor()
{
    ObjectName = "Network Player";
    SetTag("NetworkPlayer");
    SetActorScale(FVector(0.8f, 0.8f, 1.6f));
}

void ANetworkPlayerActor::InitializeNetworkPlayer(Network::FNetworkEntityId InNetworkId, bool bInLocallyControlled,
    const FVector& InitialPosition, float InitialYaw, bool bInServerReconciliationEnabled)
{
    NetworkId = InNetworkId;
    bLocallyControlled = bInLocallyControlled;
    bServerReconciliationEnabled = bInServerReconciliationEnabled;
    PendingInputs.clear();
    PredictionDurationAccumulator = 0.0f;
    PredictionDeltaAccumulator = FVector::Zero();
    PredictionYawAccumulator = InitialYaw;
    TargetPosition = InitialPosition;
    TargetYaw = InitialYaw;
    SetActorLocation(InitialPosition);
    SetActorRotation(FQuat::FromAxisAngle(FVector(0.0f, 0.0f, 1.0f), DegreesToRadians(InitialYaw)));
    ObjectName = (bLocallyControlled ? "LocalPlayer_" : "RemotePlayer_") + std::to_string(NetworkId);

    if (UStaticMeshComponent* Mesh = GetStaticMeshComponent())
    {
        if (Mesh->GetMaterial(0))
        {
            const FLinearColor Color = bLocallyControlled
                ? FLinearColor(0.05f, 0.75f, 1.0f, 1.0f)
                : FLinearColor(1.0f, 0.3f, 0.05f, 1.0f);
            Mesh->SetMaterialColorByUser(0, "DiffuseColor", Color);
        }
    }
}

bool ANetworkPlayerActor::ApplyServerState(const FVector& ServerPosition, float ServerYaw,
    std::uint32_t ServerTick, std::uint32_t LastProcessedInput)
{
    if (!Network::IsSequenceNewer(ServerTick, LastServerTick)) return false;
    LastServerTick = ServerTick;

    if (!bLocallyControlled || !bServerReconciliationEnabled)
    {
        TargetPosition = ServerPosition;
        TargetYaw = ServerYaw;
        if (bLocallyControlled && (ServerPosition - GetActorLocation()).SizeSquared() > 16.0f)
            SetActorLocation(ServerPosition);
        return true;
    }

    // 서버가 확인한 sequence까지 제거하고, 아직 확인되지 않은 로컬 예측만 권위 위치 위에 재현한다.
    while (!PendingInputs.empty() &&
        !Network::IsSequenceNewer(PendingInputs.front().Sequence, LastProcessedInput))
    {
        PendingInputs.pop_front();
    }

    FVector ReplayedPosition = ServerPosition;
    float ReplayedYaw = ServerYaw;
    for (const FPendingInput& Input : PendingInputs)
    {
        ReplayedPosition += Input.PredictedDelta;
        if (Input.PredictedDelta.SizeSquared() > KINDA_SMALL_NUMBER) ReplayedYaw = Input.PredictedYaw;
    }
    ReplayedPosition += PredictionDeltaAccumulator;
    if (PredictionDeltaAccumulator.SizeSquared() > KINDA_SMALL_NUMBER)
        ReplayedYaw = PredictionYawAccumulator;

    TargetPosition = ReplayedPosition;
    TargetYaw = ReplayedYaw;
    if ((ReplayedPosition - GetActorLocation()).SizeSquared() > 16.0f)
        SetActorLocation(ReplayedPosition);
    return true;
}

void ANetworkPlayerActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (NetworkId == 0 || !GetWorld()) return;

    const float InterpolationAlpha = 1.0f - std::exp(-10.0f * FMath::Max(0.0f, DeltaSeconds));
    if (!bLocallyControlled)
    {
        SetActorLocation(FVector::Lerp(GetActorLocation(), TargetPosition, InterpolationAlpha));
        const FQuat TargetRotation = FQuat::FromAxisAngle(FVector(0.0f, 0.0f, 1.0f), DegreesToRadians(TargetYaw));
        SetActorRotation(FQuat::Slerp(GetActorRotation(), TargetRotation, InterpolationAlpha));
        return;
    }

    float MoveX = 0.0f;
    float MoveY = 0.0f;
    if (INPUT.IsKeyDown('W')) MoveX += 1.0f;
    if (INPUT.IsKeyDown('S')) MoveX -= 1.0f;
    if (INPUT.IsKeyDown('D')) MoveY += 1.0f;
    if (INPUT.IsKeyDown('A')) MoveY -= 1.0f;

    const float LengthSquared = MoveX * MoveX + MoveY * MoveY;
    if (LengthSquared > 1.0f)
    {
        const float InverseLength = 1.0f / std::sqrt(LengthSquared);
        MoveX *= InverseLength;
        MoveY *= InverseLength;
    }

    // 작은 오차는 움직임을 멈추지 않고 서서히 authoritative state로 수렴시킨다.
    SetActorLocation(FVector::Lerp(GetActorLocation(), TargetPosition, 1.0f - std::exp(-2.0f * DeltaSeconds)));
    if (LengthSquared > KINDA_SMALL_NUMBER)
    {
        const FVector PredictedDelta = FVector(MoveX, MoveY, 0.0f) * (5.0f * DeltaSeconds);
        SetActorLocation(GetActorLocation() + PredictedDelta);
        if (bServerReconciliationEnabled)
        {
            TargetPosition += PredictedDelta;
            PredictionDeltaAccumulator += PredictedDelta;
        }
        TargetYaw = RadiansToDegrees(std::atan2(MoveY, MoveX));
        PredictionYawAccumulator = TargetYaw;
    }

    // Local prediction also follows the same smooth rotation path as remote players.
    // Position remains immediately responsive while facing direction converges over frames.
    const FQuat TargetRotation = FQuat::FromAxisAngle(
        FVector(0.0f, 0.0f, 1.0f), DegreesToRadians(TargetYaw));
    SetActorRotation(FQuat::Slerp(GetActorRotation(), TargetRotation, InterpolationAlpha));

    SendAccumulator += DeltaSeconds;
    PredictionDurationAccumulator += DeltaSeconds;
    constexpr float InputSendInterval = 1.0f / 30.0f;
    if (SendAccumulator >= InputSendInterval)
    {
        SendAccumulator = std::fmod(SendAccumulator, InputSendInterval);
        if (FNetworkManager* Manager = GetWorld()->GetNetworkManager())
        {
            const std::uint32_t NextSequence = InputSequence + 1;
            if (Manager->SendMoveInput(NextSequence, PredictionDurationAccumulator, MoveX, MoveY))
            {
                InputSequence = NextSequence;
                if (bServerReconciliationEnabled)
                {
                    constexpr std::size_t MaxPendingInputs = 1024;
                    if (PendingInputs.size() >= MaxPendingInputs) PendingInputs.pop_front();
                    PendingInputs.push_back({InputSequence, PredictionDeltaAccumulator, PredictionYawAccumulator});
                }
                PredictionDurationAccumulator = 0.0f;
                PredictionDeltaAccumulator = FVector::Zero();
            }
        }
    }
}

void ANetworkPlayerActor::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}
