#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Network
{
    class FPacketReader
    {
    public:
        explicit FPacketReader(const std::vector<std::uint8_t>& InPayload);

        bool ReadUInt16(std::uint16_t& OutValue);
        bool ReadUInt32(std::uint32_t& OutValue);
        bool ReadUInt64(std::uint64_t& OutValue);
        bool ReadFloat(float& OutValue);

        bool IsAtEnd() const { return Offset == Payload.size(); }
        std::size_t Remaining() const { return Payload.size() - Offset; }

    private:
        template <typename T>
        bool ReadLittleEndian(T& OutValue);

        const std::vector<std::uint8_t>& Payload;
        std::size_t Offset = 0;
    };
}
