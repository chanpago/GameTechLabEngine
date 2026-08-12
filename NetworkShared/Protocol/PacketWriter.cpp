#include "PacketWriter.h"
#include "../Common/NetworkTypes.h"

#include <bit>
#include <stdexcept>

namespace Network
{
    namespace
    {
        template <typename T>
        void WriteLittleEndian(std::vector<std::uint8_t>& Out, T Value)
        {
            for (std::size_t Index = 0; Index < sizeof(T); ++Index)
            {
                Out.push_back(static_cast<std::uint8_t>((Value >> (Index * 8)) & 0xff));
            }
        }
    }

    FPacketWriter::FPacketWriter(EPacketType InType)
        : Type(InType)
    {
    }

    void FPacketWriter::WriteUInt16(std::uint16_t Value) { WriteLittleEndian(Payload, Value); }
    void FPacketWriter::WriteUInt32(std::uint32_t Value) { WriteLittleEndian(Payload, Value); }
    void FPacketWriter::WriteUInt64(std::uint64_t Value) { WriteLittleEndian(Payload, Value); }

    void FPacketWriter::WriteFloat(float Value)
    {
        WriteUInt32(std::bit_cast<std::uint32_t>(Value));
    }

    std::vector<std::uint8_t> FPacketWriter::Build() const
    {
        const std::size_t TotalSize = PacketHeaderSize + Payload.size();
        if (TotalSize > MaxPacketSize)
        {
            throw std::length_error("network packet exceeds uint16 size limit");
        }

        std::vector<std::uint8_t> Bytes;
        Bytes.reserve(TotalSize);
        WriteLittleEndian(Bytes, static_cast<std::uint16_t>(TotalSize));
        WriteLittleEndian(Bytes, static_cast<std::uint16_t>(Type));
        Bytes.insert(Bytes.end(), Payload.begin(), Payload.end());
        return Bytes;
    }
}
