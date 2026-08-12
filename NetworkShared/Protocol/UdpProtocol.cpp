#include "UdpProtocol.h"

#include <bit>

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

        template <typename T>
        bool ReadLittleEndian(const std::uint8_t* Data, std::size_t Size, std::size_t& Offset, T& OutValue)
        {
            if (!Data || Offset + sizeof(T) > Size) return false;
            T Value = 0;
            for (std::size_t Index = 0; Index < sizeof(T); ++Index)
            {
                Value |= static_cast<T>(Data[Offset + Index]) << (Index * 8);
            }
            Offset += sizeof(T);
            OutValue = Value;
            return true;
        }

        void WriteFloat(std::vector<std::uint8_t>& Out, float Value)
        {
            WriteLittleEndian(Out, std::bit_cast<std::uint32_t>(Value));
        }

        bool ReadFloat(const std::uint8_t* Data, std::size_t Size, std::size_t& Offset, float& OutValue)
        {
            std::uint32_t Bits = 0;
            if (!ReadLittleEndian(Data, Size, Offset, Bits)) return false;
            OutValue = std::bit_cast<float>(Bits);
            return true;
        }

        void WriteHeader(std::vector<std::uint8_t>& Out, EUdpPacketType Type, std::uint16_t TotalSize)
        {
            WriteLittleEndian(Out, UdpProtocolMagic);
            WriteLittleEndian(Out, ProtocolVersion);
            WriteLittleEndian(Out, static_cast<std::uint16_t>(Type));
            WriteLittleEndian(Out, TotalSize);
            WriteLittleEndian(Out, static_cast<std::uint16_t>(0));
        }

        bool ReadHeader(const std::uint8_t* Data, std::size_t Size, EUdpPacketType ExpectedType, std::size_t& Offset)
        {
            if (!Data || Size < UdpDatagramHeaderSize || Size > MaxUdpDatagramSize) return false;
            std::uint32_t Magic = 0;
            std::uint16_t Version = 0;
            std::uint16_t Type = 0;
            std::uint16_t DeclaredSize = 0;
            std::uint16_t Reserved = 0;
            if (!ReadLittleEndian(Data, Size, Offset, Magic) ||
                !ReadLittleEndian(Data, Size, Offset, Version) ||
                !ReadLittleEndian(Data, Size, Offset, Type) ||
                !ReadLittleEndian(Data, Size, Offset, DeclaredSize) ||
                !ReadLittleEndian(Data, Size, Offset, Reserved))
            {
                return false;
            }
            return Magic == UdpProtocolMagic && Version == ProtocolVersion &&
                   Type == static_cast<std::uint16_t>(ExpectedType) && DeclaredSize == Size && Reserved == 0;
        }
    }

    std::vector<std::uint8_t> SerializeUdp(const FUdpMoveInput& M)
    {
        constexpr std::uint16_t TotalSize = static_cast<std::uint16_t>(UdpDatagramHeaderSize + 24);
        std::vector<std::uint8_t> Bytes;
        Bytes.reserve(TotalSize);
        WriteHeader(Bytes, EUdpPacketType::C2S_MoveInput, TotalSize);
        WriteLittleEndian(Bytes, M.PlayerId);
        WriteLittleEndian(Bytes, M.SessionToken);
        WriteLittleEndian(Bytes, M.Sequence);
        WriteFloat(Bytes, M.MoveX);
        WriteFloat(Bytes, M.MoveY);
        return Bytes;
    }

    std::vector<std::uint8_t> SerializeUdp(const FUdpPlayerState& M)
    {
        constexpr std::uint16_t TotalSize = static_cast<std::uint16_t>(UdpDatagramHeaderSize + 28);
        std::vector<std::uint8_t> Bytes;
        Bytes.reserve(TotalSize);
        WriteHeader(Bytes, EUdpPacketType::S2C_PlayerState, TotalSize);
        WriteLittleEndian(Bytes, M.PlayerId);
        WriteLittleEndian(Bytes, M.ServerTick);
        WriteFloat(Bytes, M.Position.X);
        WriteFloat(Bytes, M.Position.Y);
        WriteFloat(Bytes, M.Position.Z);
        WriteFloat(Bytes, M.Yaw);
        WriteLittleEndian(Bytes, M.LastProcessedInput);
        return Bytes;
    }

    bool DeserializeUdp(const std::uint8_t* Data, std::size_t Size, FUdpMoveInput& M)
    {
        std::size_t Offset = 0;
        if (!ReadHeader(Data, Size, EUdpPacketType::C2S_MoveInput, Offset)) return false;
        return ReadLittleEndian(Data, Size, Offset, M.PlayerId) &&
               ReadLittleEndian(Data, Size, Offset, M.SessionToken) &&
               ReadLittleEndian(Data, Size, Offset, M.Sequence) &&
               ReadFloat(Data, Size, Offset, M.MoveX) &&
               ReadFloat(Data, Size, Offset, M.MoveY) && Offset == Size;
    }

    bool DeserializeUdp(const std::uint8_t* Data, std::size_t Size, FUdpPlayerState& M)
    {
        std::size_t Offset = 0;
        if (!ReadHeader(Data, Size, EUdpPacketType::S2C_PlayerState, Offset)) return false;
        return ReadLittleEndian(Data, Size, Offset, M.PlayerId) &&
               ReadLittleEndian(Data, Size, Offset, M.ServerTick) &&
               ReadFloat(Data, Size, Offset, M.Position.X) &&
               ReadFloat(Data, Size, Offset, M.Position.Y) &&
               ReadFloat(Data, Size, Offset, M.Position.Z) &&
               ReadFloat(Data, Size, Offset, M.Yaw) &&
               ReadLittleEndian(Data, Size, Offset, M.LastProcessedInput) && Offset == Size;
    }
}
