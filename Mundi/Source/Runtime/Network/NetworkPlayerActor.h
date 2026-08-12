#pragma once

#include "StaticMeshActor.h"
#include "../../../../NetworkShared/Common/NetworkTypes.h"

#include <deque>

class ANetworkPlayerActor : public AStaticMeshActor
{
    DECLARE_CLASS(ANetworkPlayerActor, AStaticMeshActor)
    DECLARE_DUPLICATE(ANetworkPlayerActor)

public:
    ANetworkPlayerActor();
    void Tick(float DeltaSeconds) override;
    void DuplicateSubObjects() override;

    void InitializeNetworkPlayer(Network::FNetworkEntityId InNetworkId, bool bInLocallyControlled,
        const FVector& InitialPosition, float InitialYaw, bool bInServerReconciliationEnabled);
    bool ApplyServerState(const FVector& ServerPosition, float ServerYaw, std::uint32_t ServerTick,
        std::uint32_t LastProcessedInput);

    Network::FNetworkEntityId GetNetworkId() const { return NetworkId; }
    bool IsLocallyControlled() const { return bLocallyControlled; }

protected:
    ~ANetworkPlayerActor() override = default;

private:
    struct FPendingInput
    {
        std::uint32_t Sequence = 0;
        FVector PredictedDelta = FVector::Zero();
        float PredictedYaw = 0.0f;
    };

    Network::FNetworkEntityId NetworkId = 0;
    bool bLocallyControlled = false;
    bool bServerReconciliationEnabled = true;
    FVector TargetPosition = FVector::Zero();
    float TargetYaw = 0.0f;
    std::uint32_t LastServerTick = 0;
    float SendAccumulator = 0.0f;
    float PredictionDurationAccumulator = 0.0f;
    FVector PredictionDeltaAccumulator = FVector::Zero();
    float PredictionYawAccumulator = 0.0f;
    std::uint32_t InputSequence = 0;
    std::deque<FPendingInput> PendingInputs;
};
