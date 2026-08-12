#pragma once

#include "../Common/NetworkTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Network
{
    enum class EUdpPacketType : std::uint16_t
    {
        C2S_MoveInput = 1,
        S2C_PlayerState = 2,
    };

    struct FUdpMoveInput
    {
        FNetworkEntityId PlayerId = 0;
        std::uint64_t SessionToken = 0;
        std::uint32_t Sequence = 0;
        float MoveX = 0.0f;
        float MoveY = 0.0f;
    };

    struct FUdpPlayerState
    {
        FNetworkEntityId PlayerId = 0;
        std::uint32_t ServerTick = 0;
        FVector3 Position;
        float Yaw = 0.0f;
        std::uint32_t LastProcessedInput = 0;
    };

    std::vector<std::uint8_t> SerializeUdp(const FUdpMoveInput& Message);
    std::vector<std::uint8_t> SerializeUdp(const FUdpPlayerState& Message);

    bool DeserializeUdp(const std::uint8_t* Data, std::size_t Size, FUdpMoveInput& OutMessage);
    bool DeserializeUdp(const std::uint8_t* Data, std::size_t Size, FUdpPlayerState& OutMessage);
}
