#pragma once

#include <cstdint>

namespace Network
{
    enum class EPacketType : std::uint16_t
    {
        C2S_Hello = 1,
        S2C_Connected,
        S2C_PlayerSpawn,
        S2C_PlayerDespawn,
        C2S_MoveInput,
        S2C_PlayerState,
        C2S_Ping,
        S2C_Pong,
    };

    inline bool IsKnownPacketType(std::uint16_t Value)
    {
        return Value >= static_cast<std::uint16_t>(EPacketType::C2S_Hello) &&
               Value <= static_cast<std::uint16_t>(EPacketType::S2C_Pong);
    }

    inline const char* ToString(EPacketType Type)
    {
        switch (Type)
        {
        case EPacketType::C2S_Hello: return "C2S_Hello";
        case EPacketType::S2C_Connected: return "S2C_Connected";
        case EPacketType::S2C_PlayerSpawn: return "S2C_PlayerSpawn";
        case EPacketType::S2C_PlayerDespawn: return "S2C_PlayerDespawn";
        case EPacketType::C2S_MoveInput: return "C2S_MoveInput";
        case EPacketType::S2C_PlayerState: return "S2C_PlayerState";
        case EPacketType::C2S_Ping: return "C2S_Ping";
        case EPacketType::S2C_Pong: return "S2C_Pong";
        default: return "Unknown";
        }
    }
}
