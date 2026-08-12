#pragma once

#include "PacketType.h"

#include <cstdint>
#include <vector>

namespace Network
{
    struct FPacketHeader
    {
        std::uint16_t Size = 0; // Header를 포함한 전체 packet byte 수
        std::uint16_t Type = 0;
    };

    struct FPacket
    {
        EPacketType Type = EPacketType::C2S_Hello;
        std::vector<std::uint8_t> Payload;
    };
}
