#include "PacketReader.h"

#include <bit>

namespace Network
{
    FPacketReader::FPacketReader(const std::vector<std::uint8_t>& InPayload)
        : Payload(InPayload)
    {
    }

    template <typename T>
    bool FPacketReader::ReadLittleEndian(T& OutValue)
    {
        if (Remaining() < sizeof(T))
        {
            return false;
        }

        T Value = 0;
        for (std::size_t Index = 0; Index < sizeof(T); ++Index)
        {
            Value |= static_cast<T>(Payload[Offset + Index]) << (Index * 8);
        }
        Offset += sizeof(T);
        OutValue = Value;
        return true;
    }

    bool FPacketReader::ReadUInt16(std::uint16_t& OutValue) { return ReadLittleEndian(OutValue); }
    bool FPacketReader::ReadUInt32(std::uint32_t& OutValue) { return ReadLittleEndian(OutValue); }
    bool FPacketReader::ReadUInt64(std::uint64_t& OutValue) { return ReadLittleEndian(OutValue); }

    bool FPacketReader::ReadFloat(float& OutValue)
    {
        std::uint32_t Bits = 0;
        if (!ReadUInt32(Bits))
        {
            return false;
        }
        OutValue = std::bit_cast<float>(Bits);
        return true;
    }
}
