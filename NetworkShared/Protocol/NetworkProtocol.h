#pragma once

#include "Packet.h"
#include "../Common/NetworkTypes.h"

#include <cstdint>
#include <vector>

namespace Network
{
    enum class EClientNetworkOption : std::uint16_t
    {
        None = 0,
        UdpMovement = 1 << 0,
    };

    constexpr std::uint16_t ToOptionBits(EClientNetworkOption Option)
    {
        return static_cast<std::uint16_t>(Option);
    }

    constexpr bool HasOption(std::uint16_t Options, EClientNetworkOption Option)
    {
        return (Options & ToOptionBits(Option)) != 0;
    }

    struct FC2SHello
    {
        std::uint32_t Magic = ProtocolMagic;
        std::uint16_t Version = ProtocolVersion;
        std::uint16_t ClientOptions = ToOptionBits(EClientNetworkOption::UdpMovement);
    };

    struct FS2CConnected
    {
        FNetworkEntityId PlayerId = 0;
        std::uint16_t ServerTickRate = 30;
        std::uint16_t UdpPort = 7777;
        std::uint64_t UdpSessionToken = 0;
        std::uint16_t SimulatedLatencyMs = 0;
        std::uint16_t SimulatedPacketLossBasisPoints = 0;
        std::uint16_t AcceptedClientOptions = ToOptionBits(EClientNetworkOption::UdpMovement);
    };

    struct FS2CPlayerSpawn
    {
        FNetworkEntityId PlayerId = 0;
        FVector3 Position;
        float Yaw = 0.0f;
    };

    struct FS2CPlayerDespawn
    {
        FNetworkEntityId PlayerId = 0;
    };

    struct FC2SMoveInput
    {
        std::uint32_t Sequence = 0;
        float ClientDeltaTime = 0.0f; // 진단용이며 서버 이동 적분에는 사용하지 않음
        float MoveX = 0.0f;
        float MoveY = 0.0f;
    };

    struct FS2CPlayerState
    {
        FNetworkEntityId PlayerId = 0;
        std::uint32_t ServerTick = 0;
        FVector3 Position;
        float Yaw = 0.0f;
        std::uint32_t LastProcessedInput = 0;
    };

    struct FC2SPing { std::uint64_t ClientTimestampMicros = 0; };
    struct FS2CPong { std::uint64_t ClientTimestampMicros = 0; };

    std::vector<std::uint8_t> Serialize(const FC2SHello& Message);
    std::vector<std::uint8_t> Serialize(const FS2CConnected& Message);
    std::vector<std::uint8_t> Serialize(const FS2CPlayerSpawn& Message);
    std::vector<std::uint8_t> Serialize(const FS2CPlayerDespawn& Message);
    std::vector<std::uint8_t> Serialize(const FC2SMoveInput& Message);
    std::vector<std::uint8_t> Serialize(const FS2CPlayerState& Message);
    std::vector<std::uint8_t> Serialize(const FC2SPing& Message);
    std::vector<std::uint8_t> Serialize(const FS2CPong& Message);

    bool Deserialize(const FPacket& Packet, FC2SHello& OutMessage);
    bool Deserialize(const FPacket& Packet, FS2CConnected& OutMessage);
    bool Deserialize(const FPacket& Packet, FS2CPlayerSpawn& OutMessage);
    bool Deserialize(const FPacket& Packet, FS2CPlayerDespawn& OutMessage);
    bool Deserialize(const FPacket& Packet, FC2SMoveInput& OutMessage);
    bool Deserialize(const FPacket& Packet, FS2CPlayerState& OutMessage);
    bool Deserialize(const FPacket& Packet, FC2SPing& OutMessage);
    bool Deserialize(const FPacket& Packet, FS2CPong& OutMessage);
}
