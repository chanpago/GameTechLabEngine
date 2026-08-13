#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class ELoadTestScenario
{
    Connect,
    Ping,
    Movement,
};

struct FLoadTestConfig
{
    std::string ServerAddress = "127.0.0.1";
    std::uint16_t ServerPort = 7777;
    std::size_t ClientCount = 500;
    std::uint32_t DurationSeconds = 60;
    std::uint32_t PacketRatePerClient = 10;
    std::uint32_t ConnectRatePerSecond = 100;
    std::uint32_t WorkerCount = 0;
    ELoadTestScenario Scenario = ELoadTestScenario::Ping;
    bool bCsvEnabled = true;
    std::string CsvPath;
};

const char* ToString(ELoadTestScenario Scenario);
bool ParseLoadTestConfig(int ArgC, char** ArgV, FLoadTestConfig& OutConfig, bool& OutHelpRequested, std::string& OutError);
void PrintLoadTestUsage();
