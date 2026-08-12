#pragma once

#include "../../../../NetworkShared/Buffer/ReceiveBuffer.h"
#include "../../../../NetworkShared/Common/NetworkTypes.h"
#include "../../../../NetworkShared/Common/ThreadSafeQueue.h"
#include "../../../../NetworkShared/Protocol/NetworkProtocol.h"
#include "../../../../NetworkShared/Protocol/UdpProtocol.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class UWorld;
class ANetworkPlayerActor;

enum class ENetworkConnectionState : std::uint8_t
{
    Disconnected,
    Connecting,
    Connected,
    ShuttingDown,
};

struct FNetworkClientStats
{
    std::uint64_t BytesSent = 0;
    std::uint64_t BytesReceived = 0;
    std::uint64_t PacketsSent = 0;
    std::uint64_t PacketsReceived = 0;
    std::uint64_t UdpPacketsSent = 0;
    std::uint64_t UdpPacketsReceived = 0;
    std::uint64_t UdpPacketsDropped = 0;
    double SendBytesPerSecond = 0.0;
    double ReceiveBytesPerSecond = 0.0;
    double RTTMilliseconds = 0.0;
    std::uint32_t LastServerTick = 0;
    std::uint16_t SimulatedLatencyMs = 0;
    float SimulatedPacketLossPercent = 0.0f;
    bool bUseUdpMovement = true;
    bool bServerReconciliationEnabled = true;
    bool bUdpActive = false;
};

class FNetworkManager
{
public:
    FNetworkManager();
    ~FNetworkManager();

    FNetworkManager(const FNetworkManager&) = delete;
    FNetworkManager& operator=(const FNetworkManager&) = delete;

    bool Initialize();
    void ConfigureMovement(bool bInUseUdpMovement, bool bInUseServerReconciliation);
    bool Connect(const std::string& Address, std::uint16_t Port);
    void Disconnect();

    // 반드시 game/main thread에서만 호출한다.
    void Tick(UWorld* World, float DeltaSeconds);
    bool SendMoveInput(std::uint32_t Sequence, float ClientDeltaTime, float MoveX, float MoveY);
    void DrawDebugHUD() const;

    ENetworkConnectionState GetConnectionState() const { return ConnectionState.load(); }
    Network::FNetworkEntityId GetLocalPlayerId() const { return LocalPlayerId; }
    std::size_t GetRemotePlayerCount() const;
    FNetworkClientStats GetStats() const;
    const std::string& GetServerAddress() const { return ServerAddress; }
    std::uint16_t GetServerPort() const { return ServerPort; }

private:
    void NetworkLoop();
    bool QueuePacket(std::vector<std::uint8_t> Bytes);
    bool QueueUdpDatagram(std::vector<std::uint8_t> Bytes);
    void HandlePacket(UWorld* World, const Network::FPacket& Packet);
    void HandlePlayerState(UWorld* World, const Network::FUdpPlayerState& Message, bool bFromUdp);
    void HandleTransportDisconnect(UWorld* World);
    void SpawnOrUpdatePlayer(UWorld* World, const Network::FS2CPlayerSpawn& Message);
    static std::uint64_t NowMicros();

    WSADATA WsaData{};
    SOCKET Socket = INVALID_SOCKET;
    SOCKET UdpSocket = INVALID_SOCKET;
    sockaddr_storage UdpServerAddress{};
    int UdpServerAddressLength = 0;
    std::thread NetworkThread;
    std::atomic<bool> bNetworkThreadRunning{false};
    bool bWinsockInitialized = false;
    std::atomic<ENetworkConnectionState> ConnectionState{ENetworkConnectionState::Disconnected};
    std::atomic<int> LastSocketError{0};

    Network::TThreadSafeQueue<Network::FPacket> IncomingPackets{8192};
    Network::TThreadSafeQueue<std::vector<std::uint8_t>> OutgoingPackets{8192};
    Network::TThreadSafeQueue<Network::FUdpPlayerState> IncomingUdpStates{16384};
    Network::TThreadSafeQueue<std::vector<std::uint8_t>> OutgoingUdpDatagrams{8192};

    // 아래 객체 포인터와 map은 main thread에서만 접근한다.
    std::unordered_map<Network::FNetworkEntityId, ANetworkPlayerActor*> NetworkPlayers;
    std::unordered_map<Network::FNetworkEntityId, std::uint32_t> LastUdpTickByPlayer;
    Network::FNetworkEntityId LocalPlayerId = 0;
    std::uint16_t ServerTickRate = 0;
    bool bUseUdpMovement = true;
    bool bServerReconciliationEnabled = true;
    std::uint16_t SimulatedLatencyMs = 0;
    float SimulatedPacketLossPercent = 0.0f;
    std::atomic<std::uint16_t> UdpServerPort{0};
    std::uint64_t UdpSessionToken = 0;
    float PingAccumulator = 0.0f;
    float RateAccumulator = 0.0f;
    std::uint64_t RateBytesSentBaseline = 0;
    std::uint64_t RateBytesReceivedBaseline = 0;
    bool bDisconnectHandledOnMainThread = false;

    std::string ServerAddress;
    std::uint16_t ServerPort = 0;

    std::atomic<std::uint64_t> BytesSent{0};
    std::atomic<std::uint64_t> BytesReceived{0};
    std::atomic<std::uint64_t> PacketsSent{0};
    std::atomic<std::uint64_t> PacketsReceived{0};
    std::atomic<std::uint64_t> UdpPacketsSent{0};
    std::atomic<std::uint64_t> UdpPacketsReceived{0};
    std::atomic<std::uint64_t> UdpPacketsDropped{0};
    std::atomic<bool> bUdpActive{false};
    std::atomic<int> LastUdpSocketError{0};
    std::atomic<double> RTTMilliseconds{0.0};
    std::atomic<std::uint32_t> LastServerTick{0};
    double SendBytesPerSecond = 0.0;
    double ReceiveBytesPerSecond = 0.0;
};
