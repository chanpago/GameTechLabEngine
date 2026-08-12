#include "SendBuffer.h"
#include "../Common/NetworkTypes.h"

#include <algorithm>

namespace Network
{
    bool FSendBuffer::Enqueue(std::vector<std::uint8_t> PacketBytes)
    {
        if (PacketBytes.empty() || TotalBufferedBytes + PacketBytes.size() > MaxSendBufferSize)
        {
            return false;
        }
        TotalBufferedBytes += PacketBytes.size();
        Packets.push_back(std::move(PacketBytes));
        return true;
    }

    const std::uint8_t* FSendBuffer::Data() const
    {
        return Packets.empty() ? nullptr : Packets.front().data() + FrontOffset;
    }

    std::size_t FSendBuffer::Size() const
    {
        return Packets.empty() ? 0 : Packets.front().size() - FrontOffset;
    }

    void FSendBuffer::Consume(std::size_t ByteCount)
    {
        while (ByteCount > 0 && !Packets.empty())
        {
            const std::size_t Available = Size();
            const std::size_t Consumed = std::min(ByteCount, Available);
            FrontOffset += Consumed;
            TotalBufferedBytes -= Consumed;
            ByteCount -= Consumed;
            if (FrontOffset == Packets.front().size())
            {
                Packets.pop_front();
                FrontOffset = 0;
            }
        }
    }

    void FSendBuffer::Clear()
    {
        Packets.clear();
        FrontOffset = 0;
        TotalBufferedBytes = 0;
    }
}
