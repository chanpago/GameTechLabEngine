#include "LoadTestConfig.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <iostream>
#include <string_view>
#include <thread>

namespace
{
    template <typename T>
    bool ParseUnsigned(std::string_view Text, T& OutValue)
    {
        unsigned long long Value = 0;
        const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
        if (Result.ec != std::errc{} || Result.ptr != Text.data() + Text.size()) return false;
        OutValue = static_cast<T>(Value);
        return static_cast<unsigned long long>(OutValue) == Value;
    }

    bool ValueAfter(const std::string& Argument, const char* Prefix, std::string_view& OutValue)
    {
        const std::string_view ArgView(Argument);
        const std::string_view PrefixView(Prefix);
        if (!ArgView.starts_with(PrefixView)) return false;
        OutValue = ArgView.substr(PrefixView.size());
        return true;
    }
}

const char* ToString(ELoadTestScenario Scenario)
{
    switch (Scenario)
    {
    case ELoadTestScenario::Connect: return "connect";
    case ELoadTestScenario::Ping: return "ping";
    case ELoadTestScenario::Movement: return "movement";
    default: return "unknown";
    }
}

bool ParseLoadTestConfig(int ArgC, char** ArgV, FLoadTestConfig& OutConfig, bool& OutHelpRequested, std::string& OutError)
{
    OutHelpRequested = false;
    for (int Index = 1; Index < ArgC; ++Index)
    {
        std::string Argument = ArgV[Index];
        constexpr std::array<const char*, 9> ValueOptions = {
            "-server", "-port", "-clients", "-duration", "-rate",
            "-connect-rate", "-workers", "-scenario", "-csv"};
        for (const char* Option : ValueOptions)
        {
            if (Argument != Option) continue;
            if (Index + 1 >= ArgC)
            {
                OutError = Argument + " requires a value";
                return false;
            }
            Argument += '=';
            Argument += ArgV[++Index];
            break;
        }
        std::string_view Value;
        if (Argument == "-help" || Argument == "--help" || Argument == "/?")
        {
            OutHelpRequested = true;
            continue;
        }
        if (Argument == "-no-csv")
        {
            OutConfig.bCsvEnabled = false;
            continue;
        }
        if (ValueAfter(Argument, "-server=", Value))
        {
            if (Value.empty()) { OutError = "-server requires an address"; return false; }
            OutConfig.ServerAddress.assign(Value);
        }
        else if (ValueAfter(Argument, "-port=", Value))
        {
            if (!ParseUnsigned(Value, OutConfig.ServerPort) || OutConfig.ServerPort == 0)
            { OutError = "-port must be between 1 and 65535"; return false; }
        }
        else if (ValueAfter(Argument, "-clients=", Value))
        {
            if (!ParseUnsigned(Value, OutConfig.ClientCount) || OutConfig.ClientCount == 0 || OutConfig.ClientCount > 5000)
            { OutError = "-clients must be between 1 and 5000"; return false; }
        }
        else if (ValueAfter(Argument, "-duration=", Value))
        {
            if (!ParseUnsigned(Value, OutConfig.DurationSeconds) || OutConfig.DurationSeconds == 0 || OutConfig.DurationSeconds > 3600)
            { OutError = "-duration must be between 1 and 3600 seconds"; return false; }
        }
        else if (ValueAfter(Argument, "-rate=", Value))
        {
            if (!ParseUnsigned(Value, OutConfig.PacketRatePerClient) || OutConfig.PacketRatePerClient == 0 || OutConfig.PacketRatePerClient > 240)
            { OutError = "-rate must be between 1 and 240 packets/sec/client"; return false; }
        }
        else if (ValueAfter(Argument, "-connect-rate=", Value))
        {
            if (!ParseUnsigned(Value, OutConfig.ConnectRatePerSecond) || OutConfig.ConnectRatePerSecond == 0 || OutConfig.ConnectRatePerSecond > 2000)
            { OutError = "-connect-rate must be between 1 and 2000 clients/sec"; return false; }
        }
        else if (ValueAfter(Argument, "-workers=", Value))
        {
            if (!ParseUnsigned(Value, OutConfig.WorkerCount) || OutConfig.WorkerCount > 64)
            { OutError = "-workers must be between 0 (auto) and 64"; return false; }
        }
        else if (ValueAfter(Argument, "-scenario=", Value))
        {
            if (Value == "connect") OutConfig.Scenario = ELoadTestScenario::Connect;
            else if (Value == "ping") OutConfig.Scenario = ELoadTestScenario::Ping;
            else if (Value == "movement") OutConfig.Scenario = ELoadTestScenario::Movement;
            else { OutError = "-scenario must be connect, ping, or movement"; return false; }
        }
        else if (ValueAfter(Argument, "-csv=", Value))
        {
            if (Value.empty()) { OutError = "-csv requires a file path"; return false; }
            OutConfig.bCsvEnabled = true;
            OutConfig.CsvPath.assign(Value);
        }
        else
        {
            OutError = "unknown argument: " + Argument;
            return false;
        }
    }

    if (OutConfig.WorkerCount == 0)
    {
        const unsigned HardwareThreads = std::max(1u, std::thread::hardware_concurrency());
        OutConfig.WorkerCount = std::min(4u, HardwareThreads);
    }
    OutConfig.WorkerCount = static_cast<std::uint32_t>(std::min<std::size_t>(OutConfig.WorkerCount, OutConfig.ClientCount));
    return true;
}

void PrintLoadTestUsage()
{
    std::cout
        << "GameTechLabLoadTester - headless virtual client load generator\n\n"
        << "Usage:\n"
        << "  GameTechLabLoadTester.exe -server 127.0.0.1 -port 7777 [options]\n\n"
        << "Options:\n"
        << "  -clients=N             Virtual clients (default: 500, max: 5000)\n"
        << "  -duration=N            Total test seconds including ramp-up (default: 60)\n"
        << "  -scenario=TYPE         connect | ping | movement (default: ping)\n"
        << "  -rate=N                Ping/move packets per client per second (default: 10)\n"
        << "  -connect-rate=N        New connection attempts per second (default: 100)\n"
        << "  -workers=N             WSAPoll I/O workers, 0=auto (default: 0)\n"
        << "  -csv=PATH              CSV output path (default: Logs/LoadTest_*.csv)\n"
        << "  -no-csv                Disable CSV output\n"
        << "  -help                   Show this help\n";
}
