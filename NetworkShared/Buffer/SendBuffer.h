#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace Network
{
    class FSendBuffer
    {
    public:
        bool Enqueue(std::vector<std::uint8_t> PacketBytes);
        const std::uint8_t* Data() const;
        std::size_t Size() const;
        void Consume(std::size_t ByteCount);
        bool Empty() const { return Packets.empty(); }
        std::size_t BufferedBytes() const { return TotalBufferedBytes; }
        void Clear();

    private:
        std::deque<std::vector<std::uint8_t>> Packets;
        std::size_t FrontOffset = 0;
        std::size_t TotalBufferedBytes = 0;
    };
}
