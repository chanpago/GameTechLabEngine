#include "DedicatedServer.h"
#include "ServerConfig.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <string>

namespace
{
    std::atomic<bool> GStopRequested{false};

    BOOL WINAPI ConsoleHandler(DWORD Event)
    {
        if (Event == CTRL_C_EVENT || Event == CTRL_BREAK_EVENT || Event == CTRL_CLOSE_EVENT || Event == CTRL_SHUTDOWN_EVENT)
        {
            GStopRequested = true;
            return TRUE;
        }
        return FALSE;
    }
}

int main(int ArgC, char** ArgV)
{
    std::string ConfigPath = "Config/Server.ini";
    for (int Index = 1; Index < ArgC; ++Index)
    {
        const std::string Argument = ArgV[Index];
        if (Argument.rfind("-config=", 0) == 0) ConfigPath = Argument.substr(8);
    }

    FServerConfig Config = FServerConfig::Load(ConfigPath);
    bool bPortOverridden = false;
    bool bUdpPortOverridden = false;
    for (int Index = 1; Index < ArgC; ++Index)
    {
        const std::string Argument = ArgV[Index];
        try
        {
            if (Argument == "-netsim") Config.bNetworkSimulationEnabled = true;
            else if (Argument == "-no-netsim") Config.bNetworkSimulationEnabled = false;
            else if (Argument == "-io=iocp") Config.NetworkIoMode = EServerNetworkIoMode::Iocp;
            else if (Argument == "-io=wsapoll") Config.NetworkIoMode = EServerNetworkIoMode::WsaPoll;
            else if (Argument.rfind("-iocp-workers=", 0) == 0)
            {
                Config.IocpWorkerThreads = static_cast<std::uint32_t>(
                    std::clamp(std::stoi(Argument.substr(14)), 0, 64));
            }
            else if (Argument == "-perf-stats") Config.bPerformanceStatsEnabled = true;
            else if (Argument == "-no-perf-stats") Config.bPerformanceStatsEnabled = false;
            else if (Argument == "-perf-csv") Config.bPerformanceCsvEnabled = true;
            else if (Argument == "-no-perf-csv") Config.bPerformanceCsvEnabled = false;
            else if (Argument == "-quiet-connections") Config.bVerboseConnectionLogs = false;
            else if (Argument.rfind("-port=", 0) == 0)
            {
                Config.Port = static_cast<std::uint16_t>(std::clamp(std::stoi(Argument.substr(6)), 1, 65535));
                bPortOverridden = true;
            }
            else if (Argument.rfind("-udp-port=", 0) == 0)
            {
                Config.UdpPort = static_cast<std::uint16_t>(std::clamp(std::stoi(Argument.substr(10)), 1, 65535));
                bUdpPortOverridden = true;
            }
            else if (Argument.rfind("-max-clients=", 0) == 0)
            {
                Config.MaxClients = static_cast<std::size_t>(std::clamp(std::stoi(Argument.substr(13)), 1, 1024));
            }
            else if (Argument.rfind("-perf-interval=", 0) == 0)
            {
                Config.PerformanceLogIntervalSeconds = static_cast<std::uint32_t>(
                    std::clamp(std::stoi(Argument.substr(15)), 1, 60));
            }
            else if (Argument.rfind("-latency=", 0) == 0)
            {
                Config.ArtificialLatencyMs = static_cast<std::uint16_t>(
                    std::clamp(std::stoi(Argument.substr(9)), 0, 300));
                Config.bNetworkSimulationEnabled = true;
            }
            else if (Argument.rfind("-loss=", 0) == 0)
            {
                Config.PacketLossPercent = std::clamp(std::stof(Argument.substr(6)), 0.0f, 30.0f);
                Config.bNetworkSimulationEnabled = true;
            }
        }
        catch (...)
        {
            std::cerr << "Invalid server argument: " << Argument << std::endl;
            return 2;
        }
    }
    if (bPortOverridden && !bUdpPortOverridden) Config.UdpPort = Config.Port;

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    FDedicatedServer Server(Config);
    if (!Server.Initialize()) return 1;

    std::thread StopWatcher([&Server]()
    {
        while (!GStopRequested) Sleep(25);
        Server.RequestStop();
    });

    const int Result = Server.Run();
    GStopRequested = true;
    if (StopWatcher.joinable()) StopWatcher.join();
    return Result;
}
