#pragma once

#include <cstdint>
#include <string>

enum class EServerNetworkIoMode
{
    WsaPoll,
    Iocp
};

struct FServerConfig
{
    std::uint16_t Port = 7777;
    std::uint16_t UdpPort = 7777;
    std::size_t MaxClients = 32;
    std::uint32_t TickRate = 30;
    float MoveSpeed = 5.0f;
    EServerNetworkIoMode NetworkIoMode = EServerNetworkIoMode::Iocp;
    // 0이면 논리 프로세서 수 - 1로 자동 결정한다.
    std::uint32_t IocpWorkerThreads = 4;
    bool bPerformanceStatsEnabled = true;
    std::uint32_t PerformanceLogIntervalSeconds = 1;
    bool bPerformanceCsvEnabled = true;
    bool bVerboseConnectionLogs = true;
    bool bNetworkSimulationEnabled = false;
    std::uint16_t ArtificialLatencyMs = 0;
    float PacketLossPercent = 0.0f;
    std::uint32_t NetworkSimulationSeed = 1337;

    static FServerConfig Load(const std::string& Path);
};

const char* ToString(EServerNetworkIoMode Mode);
