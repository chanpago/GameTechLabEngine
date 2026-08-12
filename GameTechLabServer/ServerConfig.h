#pragma once

#include <cstdint>
#include <string>

struct FServerConfig
{
    std::uint16_t Port = 7777;
    std::uint16_t UdpPort = 7777;
    std::size_t MaxClients = 32;
    std::uint32_t TickRate = 30;
    float MoveSpeed = 5.0f;
    bool bNetworkSimulationEnabled = false;
    std::uint16_t ArtificialLatencyMs = 0;
    float PacketLossPercent = 0.0f;
    std::uint32_t NetworkSimulationSeed = 1337;

    static FServerConfig Load(const std::string& Path);
};
