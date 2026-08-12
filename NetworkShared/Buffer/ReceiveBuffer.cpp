#include "ReceiveBuffer.h"
#include "../Common/NetworkTypes.h"
#include "../Protocol/PacketType.h"

#include <algorithm>

namespace Network
{
    namespace
    {
        std::uint16_t ReadUInt16(const std::uint8_t* Data)
        {
            return static_cast<std::uint16_t>(Data[0]) |
                   (static_cast<std::uint16_t>(Data[1]) << 8);
        }
    }

    bool FReceiveBuffer::Append(const std::uint8_t* Data, std::size_t Size)
    {
        if (!Data || Size == 0)
        {
            return true;
        }
        if (BufferedBytes() + Size > MaxReceiveBufferSize)
        {
            return false;
        }

        Compact();
        Bytes.insert(Bytes.end(), Data, Data + Size);
        return true;
    }

    bool FReceiveBuffer::ExtractPackets(std::vector<FPacket>& OutPackets, std::string& OutError)
    {
        while (BufferedBytes() >= PacketHeaderSize)
        {
            const std::uint8_t* Header = Bytes.data() + ReadOffset;
            const std::uint16_t PacketSize = ReadUInt16(Header);
            const std::uint16_t PacketType = ReadUInt16(Header + sizeof(std::uint16_t));

            if (PacketSize < PacketHeaderSize || PacketSize > MaxPacketSize)
            {
                OutError = "invalid packet size: " + std::to_string(PacketSize);
                return false;
            }
            if (!IsKnownPacketType(PacketType))
            {
                OutError = "unknown packet type: " + std::to_string(PacketType);
                return false;
            }
            if (BufferedBytes() < PacketSize)
            {
                break;
            }

            FPacket Packet;
            Packet.Type = static_cast<EPacketType>(PacketType);
            const std::size_t PayloadBegin = ReadOffset + PacketHeaderSize;
            const std::size_t PayloadEnd = ReadOffset + PacketSize;
            Packet.Payload.assign(Bytes.begin() + PayloadBegin, Bytes.begin() + PayloadEnd);
            OutPackets.push_back(std::move(Packet));
            ReadOffset += PacketSize;
        }

        Compact();
        return true;
    }

    void FReceiveBuffer::Clear()
    {
        Bytes.clear();
        ReadOffset = 0;
    }

    void FReceiveBuffer::Compact()
    {
        if (ReadOffset == 0)
        {
            return;
        }
        if (ReadOffset == Bytes.size())
        {
            Clear();
            return;
        }
        if (ReadOffset >= 4096 || ReadOffset * 2 >= Bytes.size())
        {
            Bytes.erase(Bytes.begin(), Bytes.begin() + ReadOffset);
            ReadOffset = 0;
        }
    }
}
