#pragma once

#include "../Protocol/Packet.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Network
{
    class FReceiveBuffer
    {
    public:
        bool Append(const std::uint8_t* Data, std::size_t Size);
        bool ExtractPackets(std::vector<FPacket>& OutPackets, std::string& OutError);
        void Clear();
        std::size_t BufferedBytes() const { return Bytes.size() - ReadOffset; }

    private:
        void Compact();

        std::vector<std::uint8_t> Bytes;
        std::size_t ReadOffset = 0;
    };
}
