#include "ServerConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace
{
    std::string Trim(std::string Value)
    {
        const auto IsSpace = [](unsigned char C) { return std::isspace(C) != 0; };
        Value.erase(Value.begin(), std::find_if_not(Value.begin(), Value.end(), IsSpace));
        Value.erase(std::find_if_not(Value.rbegin(), Value.rend(), IsSpace).base(), Value.end());
        return Value;
    }

    std::string ToLower(std::string Value)
    {
        std::transform(Value.begin(), Value.end(), Value.begin(),
            [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        return Value;
    }

    bool ParseBool(const std::string& Value)
    {
        const std::string Lower = ToLower(Trim(Value));
        return Lower == "true" || Lower == "1" || Lower == "yes" || Lower == "on";
    }
}

FServerConfig FServerConfig::Load(const std::string& Path)
{
    FServerConfig Config;
    std::ifstream Input(Path);
    std::string Line;
    enum class ESection { None, Server, NetworkSimulation };
    ESection Section = ESection::None;
    while (std::getline(Input, Line))
    {
        Line = Trim(Line);
        if (Line.empty() || Line[0] == ';' || Line[0] == '#') continue;
        if (Line.front() == '[' && Line.back() == ']')
        {
            const std::string SectionName = ToLower(Trim(Line.substr(1, Line.size() - 2)));
            if (SectionName == "server") Section = ESection::Server;
            else if (SectionName == "networksimulation") Section = ESection::NetworkSimulation;
            else Section = ESection::None;
            continue;
        }
        if (Section == ESection::None) continue;

        const std::size_t Equals = Line.find('=');
        if (Equals == std::string::npos) continue;
        const std::string Key = Trim(Line.substr(0, Equals));
        const std::string Value = Trim(Line.substr(Equals + 1));
        try
        {
            if (Section == ESection::Server)
            {
                if (Key == "Port") Config.Port = static_cast<std::uint16_t>(std::clamp(std::stoi(Value), 1, 65535));
                else if (Key == "UdpPort") Config.UdpPort = static_cast<std::uint16_t>(std::clamp(std::stoi(Value), 1, 65535));
                else if (Key == "MaxClients") Config.MaxClients = static_cast<std::size_t>(std::clamp(std::stoi(Value), 1, 1024));
                else if (Key == "TickRate") Config.TickRate = static_cast<std::uint32_t>(std::clamp(std::stoi(Value), 1, 240));
                else if (Key == "MoveSpeed") Config.MoveSpeed = std::clamp(std::stof(Value), 0.1f, 100.0f);
            }
            else if (Section == ESection::NetworkSimulation)
            {
                if (Key == "Enabled") Config.bNetworkSimulationEnabled = ParseBool(Value);
                else if (Key == "LatencyMs") Config.ArtificialLatencyMs =
                    static_cast<std::uint16_t>(std::clamp(std::stoi(Value), 0, 300));
                else if (Key == "PacketLossPercent") Config.PacketLossPercent =
                    std::clamp(std::stof(Value), 0.0f, 30.0f);
                else if (Key == "RandomSeed") Config.NetworkSimulationSeed =
                    static_cast<std::uint32_t>(std::stoul(Value));
            }
        }
        catch (...)
        {
            // 잘못된 한 항목은 기본값을 유지한다.
        }
    }
    return Config;
}
