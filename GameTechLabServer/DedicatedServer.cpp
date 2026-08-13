#include "DedicatedServer.h"

#include "../NetworkShared/Buffer/ReceiveBuffer.h"
#include "../NetworkShared/Buffer/SendBuffer.h"
#include "../NetworkShared/Protocol/NetworkProtocol.h"
#include "../NetworkShared/Protocol/UdpProtocol.h"

#include <Mstcpip.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <unordered_map>

namespace
{
    struct FClientConnection
    {
        SOCKET Socket = INVALID_SOCKET;
        Network::FReceiveBuffer ReceiveBuffer;
        Network::FSendBuffer SendBuffer;
    };

    bool SetNonBlocking(SOCKET Socket)
    {
        u_long Enabled = 1;
        return ioctlsocket(Socket, FIONBIO, &Enabled) == 0;
    }

    bool IsWouldBlock(int Error)
    {
        return Error == WSAEWOULDBLOCK;
    }

    void DisableUdpConnectionReset(SOCKET Socket)
    {
        BOOL Disabled = FALSE;
        DWORD BytesReturned = 0;
        constexpr DWORD UdpConnectionResetControl = _WSAIOW(IOC_VENDOR, 12);
        WSAIoctl(Socket, UdpConnectionResetControl, &Disabled, sizeof(Disabled), nullptr, 0, &BytesReturned, nullptr, nullptr);
    }
}

FDedicatedServer::FDedicatedServer(FServerConfig InConfig)
    : Config(InConfig)
{
}

FDedicatedServer::~FDedicatedServer()
{
    Shutdown();
}

bool FDedicatedServer::Initialize()
{
    if (bInitialized.exchange(true)) return true;
    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        Logger.Log("WSAStartup failed");
        bInitialized = false;
        return false;
    }
    if (!CreateListenSocket())
    {
        WSACleanup();
        bInitialized = false;
        return false;
    }
    if (!CreateUdpSocket())
    {
        closesocket(ListenSocket);
        ListenSocket = INVALID_SOCKET;
        WSACleanup();
        bInitialized = false;
        return false;
    }

    PerformanceStats.Initialize(Config, Logger);
    bRunning = true;
    NetworkThread = std::thread(&FDedicatedServer::NetworkLoop, this);
    std::ostringstream Message;
    Message << "Server Started : TCP 0.0.0.0:" << Config.Port
            << "  UDP 0.0.0.0:" << Config.UdpPort
            << "  TickRate=" << Config.TickRate << "  MaxClients=" << Config.MaxClients
            << "  I/O=" << ToString(Config.NetworkIoMode);
    if (Config.bNetworkSimulationEnabled)
    {
        Message << "  UDP NetSim=" << Config.ArtificialLatencyMs << "ms one-way/"
                << std::fixed << std::setprecision(1) << Config.PacketLossPercent << "% loss"
                << "  Seed=" << Config.NetworkSimulationSeed;
    }
    else
    {
        Message << "  UDP NetSim=Off";
    }
    Logger.Log(Message.str());
    return true;
}

bool FDedicatedServer::CreateListenSocket()
{
    ListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (ListenSocket == INVALID_SOCKET)
    {
        Logger.Log("socket() failed: " + std::to_string(WSAGetLastError()));
        return false;
    }

    BOOL ReuseAddress = TRUE;
    setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&ReuseAddress), sizeof(ReuseAddress));
    if (Config.NetworkIoMode == EServerNetworkIoMode::WsaPoll && !SetNonBlocking(ListenSocket))
    {
        Logger.Log("failed to set listen socket non-blocking");
        closesocket(ListenSocket);
        ListenSocket = INVALID_SOCKET;
        return false;
    }

    sockaddr_in Address{};
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_ANY);
    Address.sin_port = htons(Config.Port);
    if (bind(ListenSocket, reinterpret_cast<sockaddr*>(&Address), sizeof(Address)) == SOCKET_ERROR ||
        listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        Logger.Log("bind/listen failed: " + std::to_string(WSAGetLastError()));
        closesocket(ListenSocket);
        ListenSocket = INVALID_SOCKET;
        return false;
    }
    return true;
}

bool FDedicatedServer::CreateUdpSocket()
{
    UdpSocket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (UdpSocket == INVALID_SOCKET)
    {
        Logger.Log("UDP socket() failed: " + std::to_string(WSAGetLastError()));
        return false;
    }

    BOOL ReuseAddress = TRUE;
    setsockopt(UdpSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&ReuseAddress), sizeof(ReuseAddress));
    DisableUdpConnectionReset(UdpSocket);
    if (Config.NetworkIoMode == EServerNetworkIoMode::WsaPoll && !SetNonBlocking(UdpSocket))
    {
        Logger.Log("failed to set UDP socket non-blocking");
        closesocket(UdpSocket);
        UdpSocket = INVALID_SOCKET;
        return false;
    }

    sockaddr_in Address{};
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_ANY);
    Address.sin_port = htons(Config.UdpPort);
    if (bind(UdpSocket, reinterpret_cast<sockaddr*>(&Address), sizeof(Address)) == SOCKET_ERROR)
    {
        Logger.Log("UDP bind failed: " + std::to_string(WSAGetLastError()));
        closesocket(UdpSocket);
        UdpSocket = INVALID_SOCKET;
        return false;
    }
    return true;
}

int FDedicatedServer::Run()
{
    using Clock = std::chrono::steady_clock;
    const auto TickDuration = std::chrono::duration<double>(1.0 / static_cast<double>(Config.TickRate));
    auto NextTick = Clock::now();

    while (bRunning)
    {
        const auto WorkBegin = Clock::now();
        if (PerformanceStats.IsEnabled())
        {
            PerformanceStats.SampleQueueDepths(
                IncomingEvents.Size(), OutgoingCommands.Size(), OutgoingUdpCommands.Size());
        }
        ProcessNetworkEvents();
        TickServer(1.0f / static_cast<float>(Config.TickRate));

        const auto WorkEnd = Clock::now();
        PerformanceStats.RecordTick(
            std::chrono::duration<double, std::milli>(WorkEnd - WorkBegin).count(),
            std::chrono::duration<double, std::milli>(TickDuration).count());
        PerformanceStats.ReportIfDue(Logger, PlayersBySession.size());

        NextTick += std::chrono::duration_cast<Clock::duration>(TickDuration);
        const auto Now = Clock::now();
        if (NextTick < Now - std::chrono::seconds(1)) NextTick = Now;
        std::this_thread::sleep_until(NextTick);
    }

    Shutdown();
    return 0;
}

void FDedicatedServer::RequestStop()
{
    bRunning = false;
}

void FDedicatedServer::Shutdown()
{
    if (!bInitialized.exchange(false)) return;
    bRunning = false;
    if (ListenSocket != INVALID_SOCKET)
    {
        shutdown(ListenSocket, SD_BOTH);
    }
    if (NetworkThread.joinable()) NetworkThread.join();
    PerformanceStats.SetActiveConnections(0);
    PerformanceStats.ReportIfDue(Logger, PlayersBySession.size(), true);
    if (ListenSocket != INVALID_SOCKET)
    {
        closesocket(ListenSocket);
        ListenSocket = INVALID_SOCKET;
    }
    if (UdpSocket != INVALID_SOCKET)
    {
        closesocket(UdpSocket);
        UdpSocket = INVALID_SOCKET;
    }
    PlayersBySession.clear();
    IncomingEvents.Clear();
    OutgoingCommands.Clear();
    OutgoingUdpCommands.Clear();
    WSACleanup();
    if (Config.bNetworkSimulationEnabled)
    {
        Logger.Log("UDP simulation final stats  Delayed=" + std::to_string(SimulatedUdpDelayedPackets.load()) +
            "  Dropped=" + std::to_string(SimulatedUdpDroppedPackets.load()));
    }
    Logger.Log("Server Stopped");
}

void FDedicatedServer::NetworkLoop()
{
    if (Config.NetworkIoMode == EServerNetworkIoMode::Iocp)
    {
        IocpNetworkLoop();
    }
    else
    {
        WsaPollNetworkLoop();
    }
}

void FDedicatedServer::WsaPollNetworkLoop()
{
    using Clock = std::chrono::steady_clock;

    struct FDelayedUdpInput
    {
        Clock::time_point DeliveryTime;
        FNetworkEvent Event;
    };

    struct FDelayedUdpOutput
    {
        Clock::time_point DeliveryTime;
        FUdpSendCommand Command;
    };

    std::unordered_map<Network::FSessionId, FClientConnection> Connections;
    Network::FSessionId NextSessionId = 1;
    std::deque<FDelayedUdpInput> DelayedUdpInputs;
    std::deque<FDelayedUdpOutput> DelayedUdpOutputs;
    constexpr std::size_t MaxDelayedUdpPackets = 65536;
    const bool bSimulateUdp = Config.bNetworkSimulationEnabled &&
        (Config.ArtificialLatencyMs > 0 || Config.PacketLossPercent > 0.0f);
    const auto ArtificialLatency = std::chrono::milliseconds(Config.ArtificialLatencyMs);
    std::mt19937 Random(Config.NetworkSimulationSeed);
    std::uniform_real_distribution<float> LossRoll(0.0f, 100.0f);
    auto LastSimulationLogTime = Clock::now();
    std::uint64_t LastLoggedDelayedPackets = 0;
    std::uint64_t LastLoggedDroppedPackets = 0;

    auto ShouldDropUdp = [&]()
    {
        return bSimulateUdp && Config.PacketLossPercent > 0.0f &&
            LossRoll(Random) < Config.PacketLossPercent;
    };

    auto SendUdpNow = [&](const FUdpSendCommand& Command)
    {
        if (Command.Bytes.empty() || Command.RemoteAddressLength <= 0) return;
        const int Sent = sendto(UdpSocket, reinterpret_cast<const char*>(Command.Bytes.data()),
            static_cast<int>(Command.Bytes.size()), 0,
            reinterpret_cast<const sockaddr*>(&Command.RemoteAddress), Command.RemoteAddressLength);
        if (Sent > 0) PerformanceStats.RecordUdpSend(static_cast<std::size_t>(Sent));
        else
        {
            const int Error = WSAGetLastError();
            if (!IsWouldBlock(Error)) PerformanceStats.RecordIoError();
        }
        // UDP snapshot은 최신 데이터가 계속 오므로 would-block/error 시 재전송하지 않는다.
    };

    auto SubmitIncomingUdp = [&](FNetworkEvent Event, Clock::time_point Now)
    {
        if (ShouldDropUdp())
        {
            SimulatedUdpDroppedPackets.fetch_add(1);
            return;
        }
        if (bSimulateUdp && Config.ArtificialLatencyMs > 0)
        {
            if (DelayedUdpInputs.size() >= MaxDelayedUdpPackets)
            {
                PerformanceStats.RecordQueueOverflow();
                SimulatedUdpDroppedPackets.fetch_add(1);
                return;
            }
            DelayedUdpInputs.push_back({Now + ArtificialLatency, std::move(Event)});
            SimulatedUdpDelayedPackets.fetch_add(1);
            return;
        }
        if (!IncomingEvents.TryPush(std::move(Event)))
        {
            PerformanceStats.RecordQueueOverflow();
        }
    };

    auto SubmitOutgoingUdp = [&](FUdpSendCommand Command, Clock::time_point Now)
    {
        if (ShouldDropUdp())
        {
            SimulatedUdpDroppedPackets.fetch_add(1);
            return;
        }
        if (bSimulateUdp && Config.ArtificialLatencyMs > 0)
        {
            if (DelayedUdpOutputs.size() >= MaxDelayedUdpPackets)
            {
                PerformanceStats.RecordQueueOverflow();
                SimulatedUdpDroppedPackets.fetch_add(1);
                return;
            }
            DelayedUdpOutputs.push_back({Now + ArtificialLatency, std::move(Command)});
            SimulatedUdpDelayedPackets.fetch_add(1);
            return;
        }
        SendUdpNow(Command);
    };

    auto Disconnect = [&](Network::FSessionId SessionId, int ErrorCode)
    {
        const auto It = Connections.find(SessionId);
        if (It == Connections.end()) return;
        shutdown(It->second.Socket, SD_BOTH);
        closesocket(It->second.Socket);
        Connections.erase(It);
        PerformanceStats.RecordConnectionClosed();
        IncomingEvents.TryPush({ENetworkEventType::Disconnected, SessionId, {}, ErrorCode});
    };

    while (bRunning)
    {
        const auto Now = Clock::now();
        if (PerformanceStats.IsEnabled())
        {
            PerformanceStats.SampleQueueDepths(
                IncomingEvents.Size(), OutgoingCommands.Size(), OutgoingUdpCommands.Size());
        }
        while (!DelayedUdpInputs.empty() && DelayedUdpInputs.front().DeliveryTime <= Now)
        {
            if (!IncomingEvents.TryPush(std::move(DelayedUdpInputs.front().Event)))
            {
                PerformanceStats.RecordQueueOverflow();
            }
            DelayedUdpInputs.pop_front();
        }
        while (!DelayedUdpOutputs.empty() && DelayedUdpOutputs.front().DeliveryTime <= Now)
        {
            SendUdpNow(DelayedUdpOutputs.front().Command);
            DelayedUdpOutputs.pop_front();
        }

        for (FUdpSendCommand& Command : OutgoingUdpCommands.Drain())
        {
            SubmitOutgoingUdp(std::move(Command), Now);
        }

        if (bSimulateUdp && Now - LastSimulationLogTime >= std::chrono::seconds(5))
        {
            const std::uint64_t Delayed = SimulatedUdpDelayedPackets.load();
            const std::uint64_t Dropped = SimulatedUdpDroppedPackets.load();
            if (Delayed != LastLoggedDelayedPackets || Dropped != LastLoggedDroppedPackets)
            {
                Logger.Log("UDP simulation stats  Delayed=" + std::to_string(Delayed) +
                    "  Dropped=" + std::to_string(Dropped));
                LastLoggedDelayedPackets = Delayed;
                LastLoggedDroppedPackets = Dropped;
            }
            LastSimulationLogTime = Now;
        }

        std::vector<Network::FSessionId> DisconnectRequests;
        for (FSendCommand& Command : OutgoingCommands.Drain())
        {
            if (Command.bDisconnect)
            {
                DisconnectRequests.push_back(Command.SessionId);
                continue;
            }
            if (Command.bBroadcast)
            {
                for (const Network::FSessionId SessionId : Command.BroadcastRecipients)
                {
                    const auto It = Connections.find(SessionId);
                    if (It != Connections.end() && !It->second.SendBuffer.Enqueue(Command.Bytes))
                    {
                        PerformanceStats.RecordQueueOverflow();
                        DisconnectRequests.push_back(SessionId);
                    }
                    else if (It != Connections.end()) PerformanceStats.RecordTcpPacketQueued(Command.PacketCount);
                }
            }
            else
            {
                const auto It = Connections.find(Command.SessionId);
                if (It != Connections.end() && !It->second.SendBuffer.Enqueue(std::move(Command.Bytes)))
                {
                    PerformanceStats.RecordQueueOverflow();
                    DisconnectRequests.push_back(Command.SessionId);
                }
                else if (It != Connections.end()) PerformanceStats.RecordTcpPacketQueued(Command.PacketCount);
            }
        }
        for (const Network::FSessionId SessionId : DisconnectRequests) Disconnect(SessionId, WSAENOBUFS);

        std::vector<WSAPOLLFD> PollDescriptors;
        std::vector<Network::FSessionId> PollSessions;
        PollDescriptors.reserve(Connections.size() + 2);
        PollSessions.reserve(Connections.size());
        PollDescriptors.push_back({ListenSocket, POLLRDNORM, 0});
        PollDescriptors.push_back({UdpSocket, POLLRDNORM, 0});
        for (const auto& Pair : Connections)
        {
            SHORT Events = POLLRDNORM;
            if (!Pair.second.SendBuffer.Empty()) Events |= POLLWRNORM;
            PollDescriptors.push_back({Pair.second.Socket, Events, 0});
            PollSessions.push_back(Pair.first);
        }

        const int PollResult = WSAPoll(PollDescriptors.data(), static_cast<ULONG>(PollDescriptors.size()), 10);
        if (PollResult == SOCKET_ERROR)
        {
            PerformanceStats.RecordIoError();
            if (bRunning) Logger.Log("WSAPoll failed: " + std::to_string(WSAGetLastError()));
            continue;
        }
        PerformanceStats.RecordPollCycle(PollResult);

        if ((PollDescriptors[0].revents & POLLRDNORM) != 0)
        {
            for (;;)
            {
                sockaddr_in RemoteAddress{};
                int AddressLength = sizeof(RemoteAddress);
                SOCKET ClientSocket = accept(ListenSocket, reinterpret_cast<sockaddr*>(&RemoteAddress), &AddressLength);
                if (ClientSocket == INVALID_SOCKET)
                {
                    if (!IsWouldBlock(WSAGetLastError())) Logger.Log("accept failed: " + std::to_string(WSAGetLastError()));
                    break;
                }
                if (Connections.size() >= Config.MaxClients || !SetNonBlocking(ClientSocket))
                {
                    closesocket(ClientSocket);
                    continue;
                }
                BOOL NoDelay = TRUE;
                setsockopt(ClientSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay));
                const Network::FSessionId SessionId = NextSessionId++;
                Connections.emplace(SessionId, FClientConnection{ClientSocket});
                PerformanceStats.RecordConnectionAccepted();
                if (!IncomingEvents.TryPush({ENetworkEventType::Connected, SessionId, {}, 0}))
                {
                    PerformanceStats.RecordQueueOverflow();
                    Disconnect(SessionId, WSAENOBUFS);
                }
            }
        }

        if ((PollDescriptors[1].revents & POLLRDNORM) != 0)
        {
            std::uint8_t Datagram[Network::MaxUdpDatagramSize];
            for (;;)
            {
                sockaddr_storage RemoteAddress{};
                int RemoteAddressLength = sizeof(RemoteAddress);
                const int Received = recvfrom(UdpSocket, reinterpret_cast<char*>(Datagram), sizeof(Datagram), 0,
                    reinterpret_cast<sockaddr*>(&RemoteAddress), &RemoteAddressLength);
                if (Received > 0)
                {
                    Network::FUdpMoveInput Move;
                    const bool bValidPacket = Network::DeserializeUdp(
                        Datagram, static_cast<std::size_t>(Received), Move);
                    PerformanceStats.RecordUdpReceive(static_cast<std::size_t>(Received), bValidPacket);
                    if (bValidPacket)
                    {
                        FNetworkEvent Event;
                        Event.Type = ENetworkEventType::UdpMove;
                        Event.UdpMove = Move;
                        Event.UdpRemoteAddress = RemoteAddress;
                        Event.UdpRemoteAddressLength = RemoteAddressLength;
                        SubmitIncomingUdp(std::move(Event), Clock::now());
                    }
                    continue;
                }
                const int Error = WSAGetLastError();
                if (!IsWouldBlock(Error) && Error != WSAECONNRESET)
                {
                    Logger.Log("UDP recvfrom failed: " + std::to_string(Error));
                }
                break;
            }
        }

        std::vector<std::pair<Network::FSessionId, int>> PendingDisconnects;
        std::uint8_t TemporaryBuffer[8192];
        for (std::size_t Index = 0; Index < PollSessions.size(); ++Index)
        {
            const Network::FSessionId SessionId = PollSessions[Index];
            const auto It = Connections.find(SessionId);
            if (It == Connections.end()) continue;
            FClientConnection& Connection = It->second;
            const SHORT Events = PollDescriptors[Index + 2].revents;

            if ((Events & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            {
                PendingDisconnects.emplace_back(SessionId, WSAECONNRESET);
                continue;
            }
            if ((Events & POLLRDNORM) != 0)
            {
                for (;;)
                {
                    const int Received = recv(Connection.Socket, reinterpret_cast<char*>(TemporaryBuffer), sizeof(TemporaryBuffer), 0);
                    if (Received > 0)
                    {
                        PerformanceStats.RecordTcpReceive(static_cast<std::size_t>(Received));
                        if (!Connection.ReceiveBuffer.Append(TemporaryBuffer, static_cast<std::size_t>(Received)))
                        {
                            PendingDisconnects.emplace_back(SessionId, WSAENOBUFS);
                            break;
                        }
                        std::vector<Network::FPacket> Packets;
                        std::string ParseError;
                        if (!Connection.ReceiveBuffer.ExtractPackets(Packets, ParseError))
                        {
                            Logger.Log("Session " + std::to_string(SessionId) + " protocol error: " + ParseError);
                            PendingDisconnects.emplace_back(SessionId, WSAEPROTONOSUPPORT);
                            break;
                        }
                        PerformanceStats.RecordTcpPacketsReceived(Packets.size());
                        for (Network::FPacket& Packet : Packets)
                        {
                            if (!IncomingEvents.TryPush({ENetworkEventType::Packet, SessionId, std::move(Packet), 0}))
                            {
                                PerformanceStats.RecordQueueOverflow();
                                PendingDisconnects.emplace_back(SessionId, WSAENOBUFS);
                                break;
                            }
                        }
                        continue;
                    }
                    if (Received == 0) PendingDisconnects.emplace_back(SessionId, 0);
                    else if (!IsWouldBlock(WSAGetLastError())) PendingDisconnects.emplace_back(SessionId, WSAGetLastError());
                    break;
                }
            }
            if ((Events & POLLWRNORM) != 0 && !Connection.SendBuffer.Empty())
            {
                while (!Connection.SendBuffer.Empty())
                {
                    const int ToSend = static_cast<int>(std::min<std::size_t>(Connection.SendBuffer.Size(), std::numeric_limits<int>::max()));
                    const int Sent = send(Connection.Socket, reinterpret_cast<const char*>(Connection.SendBuffer.Data()), ToSend, 0);
                    if (Sent > 0)
                    {
                        PerformanceStats.RecordTcpSend(static_cast<std::size_t>(Sent));
                        Connection.SendBuffer.Consume(static_cast<std::size_t>(Sent));
                    }
                    else
                    {
                        const int Error = WSAGetLastError();
                        if (!IsWouldBlock(Error)) PendingDisconnects.emplace_back(SessionId, Error);
                        break;
                    }
                }
            }
        }

        std::sort(PendingDisconnects.begin(), PendingDisconnects.end());
        PendingDisconnects.erase(std::unique(PendingDisconnects.begin(), PendingDisconnects.end()), PendingDisconnects.end());
        for (const auto& Pair : PendingDisconnects) Disconnect(Pair.first, Pair.second);
    }

    for (auto& Pair : Connections)
    {
        shutdown(Pair.second.Socket, SD_BOTH);
        closesocket(Pair.second.Socket);
        PerformanceStats.RecordConnectionClosed();
    }
    Connections.clear();
}

void FDedicatedServer::ProcessNetworkEvents()
{
    for (FNetworkEvent& Event : IncomingEvents.Drain())
    {
        switch (Event.Type)
        {
        case ENetworkEventType::Connected:
            if (Config.bVerboseConnectionLogs)
            {
                Logger.Log("Client Connected  Session=" + std::to_string(Event.SessionId));
            }
            break;
        case ENetworkEventType::Packet:
            HandlePacket(Event.SessionId, Event.Packet);
            break;
        case ENetworkEventType::UdpMove:
            HandleUdpMove(Event);
            break;
        case ENetworkEventType::Disconnected:
            HandleDisconnect(Event.SessionId, Event.ErrorCode);
            break;
        }
    }
}

void FDedicatedServer::HandlePacket(Network::FSessionId SessionId, const Network::FPacket& Packet)
{
    if (Packet.Type == Network::EPacketType::C2S_Hello)
    {
        HandleHello(SessionId, Packet);
        return;
    }

    const auto PlayerIt = PlayersBySession.find(SessionId);
    if (PlayerIt == PlayersBySession.end())
    {
        Logger.Log("Packet before handshake; disconnecting Session=" + std::to_string(SessionId));
        DisconnectSession(SessionId);
        return;
    }

    if (Packet.Type == Network::EPacketType::C2S_Ping)
    {
        Network::FC2SPing Ping;
        if (!Network::Deserialize(Packet, Ping))
        {
            DisconnectSession(SessionId);
            return;
        }
        SendTo(SessionId, Network::Serialize(Network::FS2CPong{Ping.ClientTimestampMicros}));
    }
    else if (Packet.Type == Network::EPacketType::C2S_MoveInput && !PlayerIt->second.bUseUdpMovement)
    {
        Network::FC2SMoveInput Move;
        if (!Network::Deserialize(Packet, Move) ||
            !ApplyMoveInput(PlayerIt->second, Move.Sequence, Move.MoveX, Move.MoveY))
        {
            DisconnectSession(SessionId);
        }
    }
    else
    {
        Logger.Log("Unexpected client packet " + std::string(Network::ToString(Packet.Type)));
        DisconnectSession(SessionId);
    }
}

void FDedicatedServer::HandleUdpMove(const FNetworkEvent& Event)
{
    FServerPlayer* Player = nullptr;
    for (auto& Pair : PlayersBySession)
    {
        if (Pair.second.PlayerId == Event.UdpMove.PlayerId)
        {
            Player = &Pair.second;
            break;
        }
    }
    if (!Player || !Player->bUseUdpMovement || Player->UdpSessionToken != Event.UdpMove.SessionToken ||
        Event.UdpRemoteAddressLength <= 0 ||
        !std::isfinite(Event.UdpMove.MoveX) || !std::isfinite(Event.UdpMove.MoveY))
    {
        return;
    }

    Player->UdpRemoteAddress = Event.UdpRemoteAddress;
    Player->UdpRemoteAddressLength = Event.UdpRemoteAddressLength;
    if (!Player->bUdpReady)
    {
        Player->bUdpReady = true;
        if (Config.bVerboseConnectionLogs)
        {
            Logger.Log("UDP movement ready  Player=" + std::to_string(Player->PlayerId));
        }
    }

    ApplyMoveInput(*Player, Event.UdpMove.Sequence, Event.UdpMove.MoveX, Event.UdpMove.MoveY);
}

bool FDedicatedServer::ApplyMoveInput(FServerPlayer& Player, std::uint32_t Sequence, float MoveX, float MoveY)
{
    if (!std::isfinite(MoveX) || !std::isfinite(MoveY) ||
        std::abs(MoveX) > 1.001f || std::abs(MoveY) > 1.001f)
    {
        return false;
    }

    // UDP 순서 역전뿐 아니라 TCP/UDP 공통 중복 sequence도 무시한다.
    if (!Network::IsSequenceNewer(Sequence, Player.LastInputSequence)) return true;
    Player.LastInputSequence = Sequence;
    Player.MoveX = MoveX;
    Player.MoveY = MoveY;
    Player.InputSilenceSeconds = 0.0f;
    return true;
}

void FDedicatedServer::HandleHello(Network::FSessionId SessionId, const Network::FPacket& Packet)
{
    if (PlayersBySession.contains(SessionId))
    {
        DisconnectSession(SessionId);
        return;
    }
    Network::FC2SHello Hello;
    if (!Network::Deserialize(Packet, Hello) || Hello.Magic != Network::ProtocolMagic || Hello.Version != Network::ProtocolVersion)
    {
        Logger.Log("Protocol mismatch Session=" + std::to_string(SessionId));
        DisconnectSession(SessionId);
        return;
    }

    FServerPlayer NewPlayer;
    NewPlayer.SessionId = SessionId;
    NewPlayer.PlayerId = NextPlayerId++;
    NextUdpSessionToken += 0x9e3779b97f4a7c15ULL;
    NewPlayer.UdpSessionToken = NextUdpSessionToken ^
        (static_cast<std::uint64_t>(SessionId) << 32) ^ static_cast<std::uint64_t>(NewPlayer.PlayerId);
    if (NewPlayer.UdpSessionToken == 0) NewPlayer.UdpSessionToken = 1;
    NewPlayer.bUseUdpMovement = Network::HasOption(
        Hello.ClientOptions, Network::EClientNetworkOption::UdpMovement);
    const float Slot = static_cast<float>((NewPlayer.PlayerId - 1001) % 8);
    NewPlayer.Position = {Slot * 2.0f - 7.0f, 0.0f, 1.0f};

    const std::uint16_t EffectiveLatencyMs = Config.bNetworkSimulationEnabled ? Config.ArtificialLatencyMs : 0;
    const std::uint16_t EffectiveLossBasisPoints = Config.bNetworkSimulationEnabled
        ? static_cast<std::uint16_t>(std::lround(Config.PacketLossPercent * 100.0f))
        : 0;
    const std::uint16_t AcceptedClientOptions = NewPlayer.bUseUdpMovement
        ? Network::ToOptionBits(Network::EClientNetworkOption::UdpMovement)
        : Network::ToOptionBits(Network::EClientNetworkOption::None);
    std::vector<std::uint8_t> InitialState = Network::Serialize(Network::FS2CConnected{
        NewPlayer.PlayerId, static_cast<std::uint16_t>(Config.TickRate),
        NewPlayer.bUseUdpMovement ? Config.UdpPort : static_cast<std::uint16_t>(0),
        NewPlayer.bUseUdpMovement ? NewPlayer.UdpSessionToken : static_cast<std::uint64_t>(0),
        EffectiveLatencyMs, EffectiveLossBasisPoints, AcceptedClientOptions});
    std::size_t InitialPacketCount = 1;
    for (const auto& Pair : PlayersBySession)
    {
        const FServerPlayer& Existing = Pair.second;
        std::vector<std::uint8_t> Spawn = Network::Serialize(
            Network::FS2CPlayerSpawn{Existing.PlayerId, Existing.Position, Existing.Yaw});
        InitialState.insert(InitialState.end(), Spawn.begin(), Spawn.end());
        ++InitialPacketCount;
    }
    // TCP is a byte stream and each serialized message is self-framed. Sending the
    // connected packet plus the initial spawn list as one command avoids an O(N^2)
    // command-queue burst while preserving every protocol packet and its order.
    SendTo(SessionId, std::move(InitialState), InitialPacketCount);
    PlayersBySession.emplace(SessionId, NewPlayer);
    Broadcast(Network::Serialize(Network::FS2CPlayerSpawn{NewPlayer.PlayerId, NewPlayer.Position, NewPlayer.Yaw}));

    if (Config.bVerboseConnectionLogs)
    {
        Logger.Log("Handshake complete  Session=" + std::to_string(SessionId) +
            "  Player=" + std::to_string(NewPlayer.PlayerId) +
            "  Movement=" + (NewPlayer.bUseUdpMovement ? "UDP" : "TCP"));
    }
}

void FDedicatedServer::HandleDisconnect(Network::FSessionId SessionId, int ErrorCode)
{
    const auto It = PlayersBySession.find(SessionId);
    if (It != PlayersBySession.end())
    {
        const Network::FNetworkEntityId PlayerId = It->second.PlayerId;
        PlayersBySession.erase(It);
        Broadcast(Network::Serialize(Network::FS2CPlayerDespawn{PlayerId}));
        if (Config.bVerboseConnectionLogs)
        {
            Logger.Log("Client Disconnected  Session=" + std::to_string(SessionId) + "  Player=" + std::to_string(PlayerId) + "  Error=" + std::to_string(ErrorCode));
        }
    }
    else
    {
        if (Config.bVerboseConnectionLogs)
        {
            Logger.Log("Client Disconnected  Session=" + std::to_string(SessionId) + "  Error=" + std::to_string(ErrorCode));
        }
    }
}

void FDedicatedServer::TickServer(float FixedDeltaSeconds)
{
    ++ServerTick;
    for (auto& Pair : PlayersBySession)
    {
        FServerPlayer& Player = Pair.second;
        Player.InputSilenceSeconds += FixedDeltaSeconds;
        if (Player.InputSilenceSeconds > 0.25f)
        {
            Player.MoveX = 0.0f;
            Player.MoveY = 0.0f;
        }
        float X = Player.MoveX;
        float Y = Player.MoveY;
        const float Length = std::sqrt(X * X + Y * Y);
        if (Length > 1.0f) { X /= Length; Y /= Length; }
        Player.Position.X += X * Config.MoveSpeed * FixedDeltaSeconds;
        Player.Position.Y += Y * Config.MoveSpeed * FixedDeltaSeconds;
        if (Length > 0.001f) Player.Yaw = std::atan2(Y, X) * (180.0f / 3.14159265358979323846f);

    }

    for (const auto& RecipientPair : PlayersBySession)
    {
        const FServerPlayer& Recipient = RecipientPair.second;
        // A load-test ping client negotiates UDP movement so TCP snapshots do not
        // contaminate RTT results, but it never registers a UDP endpoint. There is
        // no destination to send to in that state, so skip the inner player walk.
        if (Recipient.bUseUdpMovement && !Recipient.bUdpReady) continue;
        for (const auto& StatePair : PlayersBySession)
        {
            const FServerPlayer& Player = StatePair.second;
            Network::FUdpPlayerState State;
            State.PlayerId = Player.PlayerId;
            State.ServerTick = ServerTick;
            State.Position = Player.Position;
            State.Yaw = Player.Yaw;
            State.LastProcessedInput = Player.LastInputSequence;
            if (Recipient.bUseUdpMovement)
            {
                SendUdpTo(Recipient, Network::SerializeUdp(State));
            }
            else
            {
                Network::FS2CPlayerState TcpState;
                TcpState.PlayerId = State.PlayerId;
                TcpState.ServerTick = State.ServerTick;
                TcpState.Position = State.Position;
                TcpState.Yaw = State.Yaw;
                TcpState.LastProcessedInput = State.LastProcessedInput;
                SendTo(Recipient.SessionId, Network::Serialize(TcpState));
            }
        }
    }
}

void FDedicatedServer::SendTo(Network::FSessionId SessionId, std::vector<std::uint8_t> Bytes, std::size_t PacketCount)
{
    FSendCommand Command{SessionId, false, false, {}, std::move(Bytes)};
    Command.PacketCount = PacketCount;
    if (!OutgoingCommands.TryPush(std::move(Command)))
    {
        PerformanceStats.RecordQueueOverflow();
        DisconnectSession(SessionId);
    }
}

void FDedicatedServer::Broadcast(std::vector<std::uint8_t> Bytes)
{
    FSendCommand Command;
    Command.bBroadcast = true;
    Command.BroadcastRecipients.reserve(PlayersBySession.size());
    for (const auto& Pair : PlayersBySession)
    {
        Command.BroadcastRecipients.push_back(Pair.first);
    }
    Command.Bytes = std::move(Bytes);
    if (!OutgoingCommands.TryPush(std::move(Command)))
    {
        PerformanceStats.RecordQueueOverflow();
        Logger.Log("Outgoing broadcast queue overflow");
    }
}

void FDedicatedServer::DisconnectSession(Network::FSessionId SessionId)
{
    if (!OutgoingCommands.TryPush({SessionId, false, true, {}, {}}))
    {
        PerformanceStats.RecordQueueOverflow();
    }
}

void FDedicatedServer::SendUdpTo(const FServerPlayer& Recipient, std::vector<std::uint8_t> Bytes)
{
    if (!Recipient.bUdpReady || Recipient.UdpRemoteAddressLength <= 0 || Bytes.empty()) return;
    FUdpSendCommand Command;
    Command.RemoteAddress = Recipient.UdpRemoteAddress;
    Command.RemoteAddressLength = Recipient.UdpRemoteAddressLength;
    Command.Bytes = std::move(Bytes);
    if (!OutgoingUdpCommands.TryPush(std::move(Command)))
    {
        PerformanceStats.RecordQueueOverflow();
        Logger.Log("Outgoing UDP snapshot queue overflow");
    }
}
