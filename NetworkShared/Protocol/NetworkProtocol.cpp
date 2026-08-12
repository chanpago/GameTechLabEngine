#include "NetworkProtocol.h"
#include "PacketReader.h"
#include "PacketWriter.h"

namespace Network
{
    namespace
    {
        void WriteVector(FPacketWriter& Writer, const FVector3& Value)
        {
            Writer.WriteFloat(Value.X);
            Writer.WriteFloat(Value.Y);
            Writer.WriteFloat(Value.Z);
        }

        bool ReadVector(FPacketReader& Reader, FVector3& OutValue)
        {
            return Reader.ReadFloat(OutValue.X) && Reader.ReadFloat(OutValue.Y) && Reader.ReadFloat(OutValue.Z);
        }
    }

    std::vector<std::uint8_t> Serialize(const FC2SHello& M)
    {
        FPacketWriter W(EPacketType::C2S_Hello); W.WriteUInt32(M.Magic); W.WriteUInt16(M.Version); W.WriteUInt16(M.ClientOptions); return W.Build();
    }
    std::vector<std::uint8_t> Serialize(const FS2CConnected& M)
    {
        FPacketWriter W(EPacketType::S2C_Connected); W.WriteUInt32(M.PlayerId); W.WriteUInt16(M.ServerTickRate); W.WriteUInt16(M.UdpPort); W.WriteUInt64(M.UdpSessionToken); W.WriteUInt16(M.SimulatedLatencyMs); W.WriteUInt16(M.SimulatedPacketLossBasisPoints); W.WriteUInt16(M.AcceptedClientOptions); return W.Build();
    }
    std::vector<std::uint8_t> Serialize(const FS2CPlayerSpawn& M)
    {
        FPacketWriter W(EPacketType::S2C_PlayerSpawn); W.WriteUInt32(M.PlayerId); WriteVector(W, M.Position); W.WriteFloat(M.Yaw); return W.Build();
    }
    std::vector<std::uint8_t> Serialize(const FS2CPlayerDespawn& M)
    {
        FPacketWriter W(EPacketType::S2C_PlayerDespawn); W.WriteUInt32(M.PlayerId); return W.Build();
    }
    std::vector<std::uint8_t> Serialize(const FC2SMoveInput& M)
    {
        FPacketWriter W(EPacketType::C2S_MoveInput); W.WriteUInt32(M.Sequence); W.WriteFloat(M.ClientDeltaTime); W.WriteFloat(M.MoveX); W.WriteFloat(M.MoveY); return W.Build();
    }
    std::vector<std::uint8_t> Serialize(const FS2CPlayerState& M)
    {
        FPacketWriter W(EPacketType::S2C_PlayerState); W.WriteUInt32(M.PlayerId); W.WriteUInt32(M.ServerTick); WriteVector(W, M.Position); W.WriteFloat(M.Yaw); W.WriteUInt32(M.LastProcessedInput); return W.Build();
    }
    std::vector<std::uint8_t> Serialize(const FC2SPing& M)
    {
        FPacketWriter W(EPacketType::C2S_Ping); W.WriteUInt64(M.ClientTimestampMicros); return W.Build();
    }
    std::vector<std::uint8_t> Serialize(const FS2CPong& M)
    {
        FPacketWriter W(EPacketType::S2C_Pong); W.WriteUInt64(M.ClientTimestampMicros); return W.Build();
    }

    bool Deserialize(const FPacket& P, FC2SHello& M)
    {
        if (P.Type != EPacketType::C2S_Hello) return false; FPacketReader R(P.Payload); return R.ReadUInt32(M.Magic) && R.ReadUInt16(M.Version) && R.ReadUInt16(M.ClientOptions) && R.IsAtEnd();
    }
    bool Deserialize(const FPacket& P, FS2CConnected& M)
    {
        if (P.Type != EPacketType::S2C_Connected) return false; FPacketReader R(P.Payload); return R.ReadUInt32(M.PlayerId) && R.ReadUInt16(M.ServerTickRate) && R.ReadUInt16(M.UdpPort) && R.ReadUInt64(M.UdpSessionToken) && R.ReadUInt16(M.SimulatedLatencyMs) && R.ReadUInt16(M.SimulatedPacketLossBasisPoints) && R.ReadUInt16(M.AcceptedClientOptions) && R.IsAtEnd();
    }
    bool Deserialize(const FPacket& P, FS2CPlayerSpawn& M)
    {
        if (P.Type != EPacketType::S2C_PlayerSpawn) return false; FPacketReader R(P.Payload); return R.ReadUInt32(M.PlayerId) && ReadVector(R, M.Position) && R.ReadFloat(M.Yaw) && R.IsAtEnd();
    }
    bool Deserialize(const FPacket& P, FS2CPlayerDespawn& M)
    {
        if (P.Type != EPacketType::S2C_PlayerDespawn) return false; FPacketReader R(P.Payload); return R.ReadUInt32(M.PlayerId) && R.IsAtEnd();
    }
    bool Deserialize(const FPacket& P, FC2SMoveInput& M)
    {
        if (P.Type != EPacketType::C2S_MoveInput) return false; FPacketReader R(P.Payload); return R.ReadUInt32(M.Sequence) && R.ReadFloat(M.ClientDeltaTime) && R.ReadFloat(M.MoveX) && R.ReadFloat(M.MoveY) && R.IsAtEnd();
    }
    bool Deserialize(const FPacket& P, FS2CPlayerState& M)
    {
        if (P.Type != EPacketType::S2C_PlayerState) return false; FPacketReader R(P.Payload); return R.ReadUInt32(M.PlayerId) && R.ReadUInt32(M.ServerTick) && ReadVector(R, M.Position) && R.ReadFloat(M.Yaw) && R.ReadUInt32(M.LastProcessedInput) && R.IsAtEnd();
    }
    bool Deserialize(const FPacket& P, FC2SPing& M)
    {
        if (P.Type != EPacketType::C2S_Ping) return false; FPacketReader R(P.Payload); return R.ReadUInt64(M.ClientTimestampMicros) && R.IsAtEnd();
    }
    bool Deserialize(const FPacket& P, FS2CPong& M)
    {
        if (P.Type != EPacketType::S2C_Pong) return false; FPacketReader R(P.Payload); return R.ReadUInt64(M.ClientTimestampMicros) && R.IsAtEnd();
    }
}
