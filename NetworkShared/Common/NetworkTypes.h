#pragma once

#include <cstddef>
#include <cstdint>

namespace Network
{
    using FNetworkEntityId = std::uint32_t;
    using FSessionId = std::uint64_t;

    constexpr std::uint32_t ProtocolMagic = 0x4E4C5447; // "GTLN" in little endian
    constexpr std::uint16_t ProtocolVersion = 4;
    constexpr std::uint32_t UdpProtocolMagic = 0x554C5447; // "GTLU" in little endian
    constexpr std::size_t UdpDatagramHeaderSize = 12;
    constexpr std::size_t MaxUdpDatagramSize = 512;
    constexpr std::size_t PacketHeaderSize = sizeof(std::uint16_t) * 2;
    constexpr std::size_t MaxPacketSize = 65535;
    constexpr std::size_t MaxReceiveBufferSize = MaxPacketSize * 4;
    constexpr std::size_t MaxSendBufferSize = 1024 * 1024;

    inline bool IsSequenceNewer(std::uint32_t Candidate, std::uint32_t Baseline)
    {
        return static_cast<std::int32_t>(Candidate - Baseline) > 0;
    }

    struct FVector3
    {
        float X = 0.0f;
        float Y = 0.0f;
        float Z = 0.0f;
    };
}
