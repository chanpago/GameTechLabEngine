#include "pch.h"
#include "NetworkManager.h"

#include "NetworkPlayerActor.h"
#include "World.h"

#include "../../../../NetworkShared/Buffer/SendBuffer.h"

#include <algorithm>
#include <limits>

namespace
{
    bool SetNonBlocking(SOCKET Socket)
    {
        u_long Enabled = 1;
        return ioctlsocket(Socket, FIONBIO, &Enabled) == 0;
    }

    void DisableUdpConnectionReset(SOCKET Socket)
    {
        BOOL Disabled = FALSE;
        DWORD BytesReturned = 0;
        constexpr DWORD UdpConnectionResetControl = _WSAIOW(IOC_VENDOR, 12);
        WSAIoctl(Socket, UdpConnectionResetControl, &Disabled, sizeof(Disabled), nullptr, 0, &BytesReturned, nullptr, nullptr);
    }

    bool SetSocketAddressPort(sockaddr_storage& Address, std::uint16_t Port)
    {
        if (Address.ss_family == AF_INET)
        {
            reinterpret_cast<sockaddr_in*>(&Address)->sin_port = htons(Port);
            return true;
        }
        if (Address.ss_family == AF_INET6)
        {
            reinterpret_cast<sockaddr_in6*>(&Address)->sin6_port = htons(Port);
            return true;
        }
        return false;
    }

    const char* ConnectionStateToString(ENetworkConnectionState State)
    {
        switch (State)
        {
        case ENetworkConnectionState::Connecting: return "Connecting";
        case ENetworkConnectionState::Connected: return "Connected";
        case ENetworkConnectionState::ShuttingDown: return "Shutting Down";
        default: return "Disconnected";
        }
    }
}

FNetworkManager::FNetworkManager() = default;

FNetworkManager::~FNetworkManager()
{
    Disconnect();
}

bool FNetworkManager::Initialize()
{
    if (bWinsockInitialized) return true;
    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        LastSocketError = WSAGetLastError();
        return false;
    }
    bWinsockInitialized = true;
    return true;
}

void FNetworkManager::ConfigureMovement(bool bInUseUdpMovement, bool bInUseServerReconciliation)
{
    if (bNetworkThreadRunning) return;
    bUseUdpMovement = bInUseUdpMovement;
    bServerReconciliationEnabled = bInUseServerReconciliation;
}

bool FNetworkManager::Connect(const std::string& Address, std::uint16_t Port)
{
    Disconnect();
    if (!Initialize()) return false;

    ConnectionState = ENetworkConnectionState::Connecting;
    ServerAddress = Address;
    ServerPort = Port;
    LastSocketError = 0;
    LastUdpSocketError = 0;
    UdpServerPort = 0;
    UdpSessionToken = 0;
    SimulatedLatencyMs = 0;
    SimulatedPacketLossPercent = 0.0f;
    LastUdpTickByPlayer.clear();
    bUdpActive = false;
    BytesSent = 0;
    BytesReceived = 0;
    PacketsSent = 0;
    PacketsReceived = 0;
    UdpPacketsSent = 0;
    UdpPacketsReceived = 0;
    UdpPacketsDropped = 0;

    addrinfo Hints{};
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_STREAM;
    Hints.ai_protocol = IPPROTO_TCP;
    addrinfo* Results = nullptr;
    const std::string PortString = std::to_string(Port);
    const int ResolveResult = getaddrinfo(Address.c_str(), PortString.c_str(), &Hints, &Results);
    if (ResolveResult != 0)
    {
        LastSocketError = ResolveResult;
        ConnectionState = ENetworkConnectionState::Disconnected;
        return false;
    }

    for (addrinfo* Result = Results; Result; Result = Result->ai_next)
    {
        SOCKET Candidate = socket(Result->ai_family, Result->ai_socktype, Result->ai_protocol);
        if (Candidate == INVALID_SOCKET) continue;
        if (connect(Candidate, Result->ai_addr, static_cast<int>(Result->ai_addrlen)) == 0)
        {
            SOCKET UdpCandidate = socket(Result->ai_family, SOCK_DGRAM, IPPROTO_UDP);
            if (UdpCandidate == INVALID_SOCKET || Result->ai_addrlen > sizeof(UdpServerAddress))
            {
                if (UdpCandidate != INVALID_SOCKET) closesocket(UdpCandidate);
                closesocket(Candidate);
                continue;
            }
            Socket = Candidate;
            UdpSocket = UdpCandidate;
            std::memcpy(&UdpServerAddress, Result->ai_addr, Result->ai_addrlen);
            UdpServerAddressLength = static_cast<int>(Result->ai_addrlen);
            break;
        }
        LastSocketError = WSAGetLastError();
        closesocket(Candidate);
    }
    freeaddrinfo(Results);

    if (Socket == INVALID_SOCKET || UdpSocket == INVALID_SOCKET || !SetNonBlocking(Socket) || !SetNonBlocking(UdpSocket))
    {
        if (Socket != INVALID_SOCKET) closesocket(Socket);
        if (UdpSocket != INVALID_SOCKET) closesocket(UdpSocket);
        Socket = INVALID_SOCKET;
        UdpSocket = INVALID_SOCKET;
        ConnectionState = ENetworkConnectionState::Disconnected;
        return false;
    }

    BOOL NoDelay = TRUE;
    setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay));
    DisableUdpConnectionReset(UdpSocket);
    bDisconnectHandledOnMainThread = false;
    bNetworkThreadRunning = true;
    NetworkThread = std::thread(&FNetworkManager::NetworkLoop, this);
    Network::FC2SHello Hello;
    Hello.ClientOptions = bUseUdpMovement
        ? Network::ToOptionBits(Network::EClientNetworkOption::UdpMovement)
        : Network::ToOptionBits(Network::EClientNetworkOption::None);
    if (!QueuePacket(Network::Serialize(Hello)))
    {
        Disconnect();
        return false;
    }
    UE_LOG("[Network] Connected transport to %s:%u; waiting for handshake", Address.c_str(), Port);
    return true;
}

void FNetworkManager::Disconnect()
{
    const bool bHadSocket = Socket != INVALID_SOCKET;
    ConnectionState = ENetworkConnectionState::ShuttingDown;
    bNetworkThreadRunning = false;
    if (bHadSocket) shutdown(Socket, SD_BOTH);
    if (NetworkThread.joinable()) NetworkThread.join();
    if (Socket != INVALID_SOCKET)
    {
        closesocket(Socket);
        Socket = INVALID_SOCKET;
    }
    if (UdpSocket != INVALID_SOCKET)
    {
        closesocket(UdpSocket);
        UdpSocket = INVALID_SOCKET;
    }
    IncomingPackets.Clear();
    OutgoingPackets.Clear();
    IncomingUdpStates.Clear();
    OutgoingUdpDatagrams.Clear();
    UdpServerPort = 0;
    UdpSessionToken = 0;
    SimulatedLatencyMs = 0;
    SimulatedPacketLossPercent = 0.0f;
    LastUdpTickByPlayer.clear();
    bUdpActive = false;
    if (bWinsockInitialized)
    {
        WSACleanup();
        bWinsockInitialized = false;
    }
    ConnectionState = ENetworkConnectionState::Disconnected;
}

void FNetworkManager::NetworkLoop()
{
    Network::FReceiveBuffer ReceiveBuffer;
    Network::FSendBuffer SendBuffer;
    std::uint8_t TemporaryBuffer[8192];
    bool bUdpSocketConnected = false;

    while (bNetworkThreadRunning)
    {
        for (std::vector<std::uint8_t>& Bytes : OutgoingPackets.Drain())
        {
            if (!SendBuffer.Enqueue(std::move(Bytes)))
            {
                LastSocketError = WSAENOBUFS;
                bNetworkThreadRunning = false;
                break;
            }
        }

        if (!bUdpSocketConnected && UdpServerPort.load() != 0)
        {
            sockaddr_storage Address = UdpServerAddress;
            if (!SetSocketAddressPort(Address, UdpServerPort.load()) ||
                connect(UdpSocket, reinterpret_cast<const sockaddr*>(&Address), UdpServerAddressLength) == SOCKET_ERROR)
            {
                LastUdpSocketError = WSAGetLastError();
            }
            else
            {
                bUdpSocketConnected = true;
            }
        }

        for (std::vector<std::uint8_t>& Bytes : OutgoingUdpDatagrams.Drain())
        {
            if (!bUdpSocketConnected || Bytes.empty())
            {
                UdpPacketsDropped.fetch_add(1);
                continue;
            }
            const int Sent = send(UdpSocket, reinterpret_cast<const char*>(Bytes.data()), static_cast<int>(Bytes.size()), 0);
            if (Sent == static_cast<int>(Bytes.size()))
            {
                BytesSent.fetch_add(static_cast<std::uint64_t>(Sent));
                PacketsSent.fetch_add(1);
                UdpPacketsSent.fetch_add(1);
            }
            else
            {
                const int Error = WSAGetLastError();
                if (Error != WSAEWOULDBLOCK) LastUdpSocketError = Error;
                UdpPacketsDropped.fetch_add(1);
            }
        }

        WSAPOLLFD Descriptors[2]{};
        Descriptors[0].fd = Socket;
        Descriptors[0].events = POLLRDNORM;
        if (!SendBuffer.Empty()) Descriptors[0].events |= POLLWRNORM;
        Descriptors[1].fd = UdpSocket;
        Descriptors[1].events = POLLRDNORM;
        const int PollResult = WSAPoll(Descriptors, 2, 10);
        if (PollResult == SOCKET_ERROR)
        {
            LastSocketError = WSAGetLastError();
            break;
        }
        if (PollResult == 0) continue;
        if ((Descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
            LastSocketError = WSAECONNRESET;
            break;
        }

        if ((Descriptors[0].revents & POLLRDNORM) != 0)
        {
            for (;;)
            {
                const int Received = recv(Socket, reinterpret_cast<char*>(TemporaryBuffer), sizeof(TemporaryBuffer), 0);
                if (Received > 0)
                {
                    BytesReceived.fetch_add(static_cast<std::uint64_t>(Received));
                    if (!ReceiveBuffer.Append(TemporaryBuffer, static_cast<std::size_t>(Received)))
                    {
                        LastSocketError = WSAENOBUFS;
                        bNetworkThreadRunning = false;
                        break;
                    }
                    std::vector<Network::FPacket> Packets;
                    std::string ParseError;
                    if (!ReceiveBuffer.ExtractPackets(Packets, ParseError))
                    {
                        UE_LOG("[Network] Protocol error: %s", ParseError.c_str());
                        LastSocketError = WSAEPROTONOSUPPORT;
                        bNetworkThreadRunning = false;
                        break;
                    }
                    for (Network::FPacket& Packet : Packets)
                    {
                        if (!IncomingPackets.TryPush(std::move(Packet)))
                        {
                            LastSocketError = WSAENOBUFS;
                            bNetworkThreadRunning = false;
                            break;
                        }
                        PacketsReceived.fetch_add(1);
                    }
                    continue;
                }
                if (Received == 0)
                {
                    LastSocketError = 0;
                    bNetworkThreadRunning = false;
                }
                else
                {
                    const int Error = WSAGetLastError();
                    if (Error != WSAEWOULDBLOCK)
                    {
                        LastSocketError = Error;
                        bNetworkThreadRunning = false;
                    }
                }
                break;
            }
        }

        if ((Descriptors[0].revents & POLLWRNORM) != 0)
        {
            while (!SendBuffer.Empty())
            {
                const int ByteCount = static_cast<int>(std::min<std::size_t>(SendBuffer.Size(), std::numeric_limits<int>::max()));
                const int Sent = send(Socket, reinterpret_cast<const char*>(SendBuffer.Data()), ByteCount, 0);
                if (Sent > 0)
                {
                    SendBuffer.Consume(static_cast<std::size_t>(Sent));
                    BytesSent.fetch_add(static_cast<std::uint64_t>(Sent));
                }
                else
                {
                    const int Error = WSAGetLastError();
                    if (Error != WSAEWOULDBLOCK)
                    {
                        LastSocketError = Error;
                        bNetworkThreadRunning = false;
                    }
                    break;
                }
            }
        }

        if ((Descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
            LastUdpSocketError = WSAECONNRESET;
        }
        if ((Descriptors[1].revents & POLLRDNORM) != 0)
        {
            std::uint8_t Datagram[Network::MaxUdpDatagramSize];
            for (;;)
            {
                const int Received = recv(UdpSocket, reinterpret_cast<char*>(Datagram), sizeof(Datagram), 0);
                if (Received > 0)
                {
                    Network::FUdpPlayerState State;
                    if (Network::DeserializeUdp(Datagram, static_cast<std::size_t>(Received), State) &&
                        IncomingUdpStates.TryPush(State))
                    {
                        BytesReceived.fetch_add(static_cast<std::uint64_t>(Received));
                        PacketsReceived.fetch_add(1);
                        UdpPacketsReceived.fetch_add(1);
                        bUdpActive = true;
                    }
                    else
                    {
                        UdpPacketsDropped.fetch_add(1);
                    }
                    continue;
                }
                const int Error = WSAGetLastError();
                if (Error != WSAEWOULDBLOCK && Error != WSAECONNRESET) LastUdpSocketError = Error;
                break;
            }
        }
    }

    if (ConnectionState != ENetworkConnectionState::ShuttingDown)
    {
        ConnectionState = ENetworkConnectionState::Disconnected;
    }
}

void FNetworkManager::Tick(UWorld* World, float DeltaSeconds)
{
    if (!World) return;
    for (Network::FPacket& Packet : IncomingPackets.Drain(2048))
    {
        HandlePacket(World, Packet);
    }
    for (const Network::FUdpPlayerState& State : IncomingUdpStates.Drain(4096))
    {
        HandlePlayerState(World, State, true);
    }

    if (ConnectionState == ENetworkConnectionState::Disconnected && !bDisconnectHandledOnMainThread)
    {
        HandleTransportDisconnect(World);
    }

    if (ConnectionState == ENetworkConnectionState::Connected)
    {
        PingAccumulator += DeltaSeconds;
        if (PingAccumulator >= 1.0f)
        {
            PingAccumulator = std::fmod(PingAccumulator, 1.0f);
            QueuePacket(Network::Serialize(Network::FC2SPing{NowMicros()}));
        }
    }

    RateAccumulator += DeltaSeconds;
    if (RateAccumulator >= 1.0f)
    {
        const std::uint64_t CurrentSent = BytesSent.load();
        const std::uint64_t CurrentReceived = BytesReceived.load();
        SendBytesPerSecond = static_cast<double>(CurrentSent - RateBytesSentBaseline) / RateAccumulator;
        ReceiveBytesPerSecond = static_cast<double>(CurrentReceived - RateBytesReceivedBaseline) / RateAccumulator;
        RateBytesSentBaseline = CurrentSent;
        RateBytesReceivedBaseline = CurrentReceived;
        RateAccumulator = 0.0f;
    }
}

bool FNetworkManager::SendMoveInput(std::uint32_t Sequence, float ClientDeltaTime, float MoveX, float MoveY)
{
    if (ConnectionState != ENetworkConnectionState::Connected || LocalPlayerId == 0 ||
        (bUseUdpMovement && UdpSessionToken == 0)) return false;
    Network::FUdpMoveInput Message;
    Message.PlayerId = LocalPlayerId;
    Message.SessionToken = UdpSessionToken;
    Message.Sequence = Sequence;
    Message.MoveX = FMath::Clamp(MoveX, -1.0f, 1.0f);
    Message.MoveY = FMath::Clamp(MoveY, -1.0f, 1.0f);
    if (bUseUdpMovement) return QueueUdpDatagram(Network::SerializeUdp(Message));

    Network::FC2SMoveInput TcpMessage;
    TcpMessage.Sequence = Sequence;
    TcpMessage.ClientDeltaTime = ClientDeltaTime;
    TcpMessage.MoveX = Message.MoveX;
    TcpMessage.MoveY = Message.MoveY;
    return QueuePacket(Network::Serialize(TcpMessage));
}

bool FNetworkManager::QueuePacket(std::vector<std::uint8_t> Bytes)
{
    if (Bytes.empty() || !bNetworkThreadRunning) return false;
    if (!OutgoingPackets.TryPush(std::move(Bytes)))
    {
        LastSocketError = WSAENOBUFS;
        return false;
    }
    PacketsSent.fetch_add(1);
    return true;
}

bool FNetworkManager::QueueUdpDatagram(std::vector<std::uint8_t> Bytes)
{
    if (Bytes.empty() || !bNetworkThreadRunning || UdpServerPort.load() == 0) return false;
    if (!OutgoingUdpDatagrams.TryPush(std::move(Bytes)))
    {
        UdpPacketsDropped.fetch_add(1);
        return false;
    }
    return true;
}

void FNetworkManager::HandlePacket(UWorld* World, const Network::FPacket& Packet)
{
    switch (Packet.Type)
    {
    case Network::EPacketType::S2C_Connected:
    {
        Network::FS2CConnected Message;
        if (!Network::Deserialize(Packet, Message)) break;
        LocalPlayerId = Message.PlayerId;
        ServerTickRate = Message.ServerTickRate;
        UdpServerPort = Message.UdpPort;
        UdpSessionToken = Message.UdpSessionToken;
        SimulatedLatencyMs = Message.SimulatedLatencyMs;
        SimulatedPacketLossPercent = static_cast<float>(Message.SimulatedPacketLossBasisPoints) / 100.0f;
        bUseUdpMovement = Network::HasOption(
            Message.AcceptedClientOptions, Network::EClientNetworkOption::UdpMovement);
        if (!bUseUdpMovement)
        {
            UdpServerPort = 0;
            UdpSessionToken = 0;
        }
        ConnectionState = ENetworkConnectionState::Connected;
        UE_LOG("[Network] Handshake complete. PlayerId=%u TickRate=%u Movement=%s NetSim=%ums/%.1f%% Reconciliation=%s",
            LocalPlayerId, ServerTickRate, bUseUdpMovement ? "UDP" : "TCP", SimulatedLatencyMs,
            SimulatedPacketLossPercent, bServerReconciliationEnabled ? "On" : "Off");
        break;
    }
    case Network::EPacketType::S2C_PlayerSpawn:
    {
        Network::FS2CPlayerSpawn Message;
        if (Network::Deserialize(Packet, Message)) SpawnOrUpdatePlayer(World, Message);
        break;
    }
    case Network::EPacketType::S2C_PlayerDespawn:
    {
        Network::FS2CPlayerDespawn Message;
        if (!Network::Deserialize(Packet, Message)) break;
        const auto It = NetworkPlayers.find(Message.PlayerId);
        if (It != NetworkPlayers.end())
        {
            if (It->second) It->second->Destroy();
            NetworkPlayers.erase(It);
            LastUdpTickByPlayer.erase(Message.PlayerId);
        }
        break;
    }
    case Network::EPacketType::S2C_PlayerState:
    {
        Network::FS2CPlayerState Message;
        if (!Network::Deserialize(Packet, Message)) break;
        Network::FUdpPlayerState State;
        State.PlayerId = Message.PlayerId;
        State.ServerTick = Message.ServerTick;
        State.Position = Message.Position;
        State.Yaw = Message.Yaw;
        State.LastProcessedInput = Message.LastProcessedInput;
        HandlePlayerState(World, State, false);
        break;
    }
    case Network::EPacketType::S2C_Pong:
    {
        Network::FS2CPong Message;
        if (Network::Deserialize(Packet, Message))
        {
            const std::uint64_t Now = NowMicros();
            if (Now >= Message.ClientTimestampMicros) RTTMilliseconds = static_cast<double>(Now - Message.ClientTimestampMicros) / 1000.0;
        }
        break;
    }
    default:
        UE_LOG("[Network] Unexpected server packet: %s", Network::ToString(Packet.Type));
        break;
    }
}

void FNetworkManager::HandlePlayerState(UWorld* World, const Network::FUdpPlayerState& Message, bool bFromUdp)
{
    if (!World) return;
    const std::uint32_t CurrentLastTick = LastServerTick.load();
    if (Network::IsSequenceNewer(Message.ServerTick, CurrentLastTick)) LastServerTick = Message.ServerTick;

    const auto It = NetworkPlayers.find(Message.PlayerId);
    if (It == NetworkPlayers.end() || !It->second)
    {
        if (bFromUdp) UdpPacketsDropped.fetch_add(1);
        return;
    }

    if (bFromUdp)
    {
        std::uint32_t& PreviousTick = LastUdpTickByPlayer[Message.PlayerId];
        if (PreviousTick == 0)
        {
            PreviousTick = Message.ServerTick;
        }
        else if (Network::IsSequenceNewer(Message.ServerTick, PreviousTick))
        {
            const std::uint32_t TickGap = Message.ServerTick - PreviousTick;
            if (TickGap > 1) UdpPacketsDropped.fetch_add(static_cast<std::uint64_t>(TickGap - 1));
            PreviousTick = Message.ServerTick;
        }
    }
    if (!It->second->ApplyServerState(
        FVector(Message.Position.X, Message.Position.Y, Message.Position.Z), Message.Yaw,
        Message.ServerTick, Message.LastProcessedInput))
    {
        // UDP에서 순서가 뒤바뀐 오래된 snapshot만 drop 통계에 포함한다.
        if (bFromUdp) UdpPacketsDropped.fetch_add(1);
    }
}

void FNetworkManager::SpawnOrUpdatePlayer(UWorld* World, const Network::FS2CPlayerSpawn& Message)
{
    const FVector Position(Message.Position.X, Message.Position.Y, Message.Position.Z);
    const auto Existing = NetworkPlayers.find(Message.PlayerId);
    if (Existing != NetworkPlayers.end() && Existing->second)
    {
        Existing->second->ApplyServerState(Position, Message.Yaw, LastServerTick.load(), 0);
        return;
    }

    ANetworkPlayerActor* Actor = World->SpawnActor<ANetworkPlayerActor>(
        FTransform(Position, FQuat::Identity(), FVector(1.0f, 1.0f, 1.0f)));
    if (!Actor) return;
    Actor->InitializeNetworkPlayer(Message.PlayerId, Message.PlayerId == LocalPlayerId, Position, Message.Yaw,
        bServerReconciliationEnabled);
    NetworkPlayers[Message.PlayerId] = Actor;
}

void FNetworkManager::HandleTransportDisconnect(UWorld* World)
{
    bDisconnectHandledOnMainThread = true;
    for (auto& Pair : NetworkPlayers)
    {
        if (Pair.second && !Pair.second->IsPendingDestroy()) Pair.second->Destroy();
    }
    NetworkPlayers.clear();
    LastUdpTickByPlayer.clear();
    LocalPlayerId = 0;
    UdpSessionToken = 0;
    UdpServerPort = 0;
    SimulatedLatencyMs = 0;
    SimulatedPacketLossPercent = 0.0f;
    bUdpActive = false;
    UE_LOG("[Network] Disconnected from server. WSAError=%d", LastSocketError.load());
}

std::size_t FNetworkManager::GetRemotePlayerCount() const
{
    const std::size_t Count = NetworkPlayers.size();
    return LocalPlayerId != 0 && Count > 0 ? Count - 1 : Count;
}

FNetworkClientStats FNetworkManager::GetStats() const
{
    FNetworkClientStats Result;
    Result.BytesSent = BytesSent.load();
    Result.BytesReceived = BytesReceived.load();
    Result.PacketsSent = PacketsSent.load();
    Result.PacketsReceived = PacketsReceived.load();
    Result.UdpPacketsSent = UdpPacketsSent.load();
    Result.UdpPacketsReceived = UdpPacketsReceived.load();
    Result.UdpPacketsDropped = UdpPacketsDropped.load();
    Result.SendBytesPerSecond = SendBytesPerSecond;
    Result.ReceiveBytesPerSecond = ReceiveBytesPerSecond;
    Result.RTTMilliseconds = RTTMilliseconds.load();
    Result.LastServerTick = LastServerTick.load();
    Result.SimulatedLatencyMs = SimulatedLatencyMs;
    Result.SimulatedPacketLossPercent = SimulatedPacketLossPercent;
    Result.bUseUdpMovement = bUseUdpMovement;
    Result.bServerReconciliationEnabled = bServerReconciliationEnabled;
    Result.bUdpActive = bUdpActive.load();
    return Result;
}

void FNetworkManager::DrawDebugHUD() const
{
    const FNetworkClientStats Stats = GetStats();
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::Begin("NETWORK", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("TCP State      %s", ConnectionStateToString(GetConnectionState()));
    ImGui::Text("TCP Server     %s:%u", ServerAddress.c_str(), ServerPort);
    ImGui::Text("Movement       %s", Stats.bUseUdpMovement ? "UDP" : "TCP");
    if (Stats.bUseUdpMovement)
    {
        ImGui::Text("UDP State      %s (%u)", Stats.bUdpActive ? "Active" : "Waiting", UdpServerPort.load());
    }
    ImGui::Text("Reconciliation %s", Stats.bServerReconciliationEnabled ? "On" : "Off");
    if (Stats.bUseUdpMovement && (Stats.SimulatedLatencyMs > 0 || Stats.SimulatedPacketLossPercent > 0.0f))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "UDP Net Sim    %u ms one-way / %.1f%% loss",
            Stats.SimulatedLatencyMs, Stats.SimulatedPacketLossPercent);
    }
    else if (Stats.bUseUdpMovement)
    {
        ImGui::Text("UDP Net Sim    Off");
    }
    else
    {
        ImGui::Text("UDP Net Sim    Bypassed (TCP movement)");
    }
    ImGui::Text("Player ID      %u", LocalPlayerId);
    ImGui::Text("Ping           %.1f ms", Stats.RTTMilliseconds);
    ImGui::Text("Server Tick    %u Hz / #%u", ServerTickRate, Stats.LastServerTick);
    ImGui::Separator();
    ImGui::Text("Sent           %.1f KB/s", Stats.SendBytesPerSecond / 1024.0);
    ImGui::Text("Received       %.1f KB/s", Stats.ReceiveBytesPerSecond / 1024.0);
    ImGui::Text("Packets Sent   %llu", static_cast<unsigned long long>(Stats.PacketsSent));
    ImGui::Text("Packets Recv   %llu", static_cast<unsigned long long>(Stats.PacketsReceived));
    ImGui::Text("UDP Sent/Recv  %llu / %llu", static_cast<unsigned long long>(Stats.UdpPacketsSent),
        static_cast<unsigned long long>(Stats.UdpPacketsReceived));
    ImGui::Text("UDP Dropped    %llu", static_cast<unsigned long long>(Stats.UdpPacketsDropped));
    ImGui::Text("Remote Players %zu", GetRemotePlayerCount());
    if (GetConnectionState() == ENetworkConnectionState::Disconnected)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.2f, 1.0f), "WSA Error      %d", LastSocketError.load());
    }
    ImGui::End();
}

std::uint64_t FNetworkManager::NowMicros()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
