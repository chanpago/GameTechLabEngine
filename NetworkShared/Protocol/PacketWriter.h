#pragma once

#include "Packet.h"

#include <cstdint>
#include <vector>

namespace Network
{
    class FPacketWriter
    {
    public:
        explicit FPacketWriter(EPacketType InType);

        void WriteUInt16(std::uint16_t Value);
        void WriteUInt32(std::uint32_t Value);
        void WriteUInt64(std::uint64_t Value);
        void WriteFloat(float Value);

        std::vector<std::uint8_t> Build() const;

    private:
        EPacketType Type;
        std::vector<std::uint8_t> Payload;
    };
}
