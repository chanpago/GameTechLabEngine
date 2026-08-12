#pragma once

#include "ServerConfig.h"
#include "ServerLogger.h"

#include "../NetworkShared/Common/NetworkTypes.h"
#include "../NetworkShared/Common/ThreadSafeQueue.h"
#include "../NetworkShared/Protocol/Packet.h"
#include "../NetworkShared/Protocol/UdpProtocol.h"

#include <WinSock2.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class FDedicatedServer
{
public:
    explicit FDedicatedServer(FServerConfig InConfig);
    ~FDedicatedServer();

    bool Initialize();
    int Run();
    void RequestStop();
    void Shutdown();

private:
    enum class ENetworkEventType { Connected, Packet, UdpMove, Disconnected };

    struct FNetworkEvent
    {
        ENetworkEventType Type = ENetworkEventType::Packet;
        Network::FSessionId SessionId = 0;
        Network::FPacket Packet;
        int ErrorCode = 0;
        Network::FUdpMoveInput UdpMove;
        sockaddr_storage UdpRemoteAddress{};
        int UdpRemoteAddressLength = 0;
    };

    struct FSendCommand
    {
        Network::FSessionId SessionId = 0;
        bool bBroadcast = false;
        bool bDisconnect = false;
        std::vector<Network::FSessionId> BroadcastRecipients;
        std::vector<std::uint8_t> Bytes;
    };

    struct FServerPlayer
    {
        Network::FSessionId SessionId = 0;
        Network::FNetworkEntityId PlayerId = 0;
        Network::FVector3 Position;
        float Yaw = 0.0f;
        float MoveX = 0.0f;
        float MoveY = 0.0f;
        std::uint32_t LastInputSequence = 0;
        std::uint64_t UdpSessionToken = 0;
        sockaddr_storage UdpRemoteAddress{};
        int UdpRemoteAddressLength = 0;
        bool bUdpReady = false;
        bool bUseUdpMovement = true;
        float InputSilenceSeconds = 0.0f;
    };

    struct FUdpSendCommand
    {
        sockaddr_storage RemoteAddress{};
        int RemoteAddressLength = 0;
        std::vector<std::uint8_t> Bytes;
    };

    bool CreateListenSocket();
    bool CreateUdpSocket();
    void NetworkLoop();
    void ProcessNetworkEvents();
    void HandlePacket(Network::FSessionId SessionId, const Network::FPacket& Packet);
    void HandleHello(Network::FSessionId SessionId, const Network::FPacket& Packet);
    void HandleUdpMove(const FNetworkEvent& Event);
    bool ApplyMoveInput(FServerPlayer& Player, std::uint32_t Sequence, float MoveX, float MoveY);
    void HandleDisconnect(Network::FSessionId SessionId, int ErrorCode);
    void TickServer(float FixedDeltaSeconds);

    void SendTo(Network::FSessionId SessionId, std::vector<std::uint8_t> Bytes);
    void Broadcast(std::vector<std::uint8_t> Bytes);
    void DisconnectSession(Network::FSessionId SessionId);
    void SendUdpTo(const FServerPlayer& Recipient, std::vector<std::uint8_t> Bytes);

    FServerConfig Config;
    FServerLogger Logger;
    WSADATA WsaData{};
    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET UdpSocket = INVALID_SOCKET;
    std::thread NetworkThread;
    std::atomic<bool> bRunning{false};
    std::atomic<bool> bInitialized{false};
    std::atomic<std::uint64_t> SimulatedUdpDelayedPackets{0};
    std::atomic<std::uint64_t> SimulatedUdpDroppedPackets{0};

    Network::TThreadSafeQueue<FNetworkEvent> IncomingEvents{8192};
    Network::TThreadSafeQueue<FSendCommand> OutgoingCommands{16384};
    Network::TThreadSafeQueue<FUdpSendCommand> OutgoingUdpCommands{32768};

    std::unordered_map<Network::FSessionId, FServerPlayer> PlayersBySession;
    Network::FNetworkEntityId NextPlayerId = 1001;
    std::uint32_t ServerTick = 0;
    std::uint64_t NextUdpSessionToken = 0x9e3779b97f4a7c15ULL;
};
