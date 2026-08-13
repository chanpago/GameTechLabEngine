#include "LoadTestRunner.h"

#include "LoadTestStats.h"
#include "../NetworkShared/Buffer/ReceiveBuffer.h"
#include "../NetworkShared/Buffer/SendBuffer.h"
#include "../NetworkShared/Protocol/NetworkProtocol.h"
#include "../NetworkShared/Protocol/UdpProtocol.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using FClock = std::chrono::steady_clock;

    enum class EClientState
    {
        NotStarted,
        Connecting,
        Handshaking,
        Active,
        Failed,
    };

    std::uint64_t NowMicros()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(FClock::now().time_since_epoch()).count());
    }

    bool SetNonBlocking(SOCKET Socket)
    {
        u_long Enabled = 1;
        return ioctlsocket(Socket, FIONBIO, &Enabled) == 0;
    }

    bool IsWouldBlock(int Error)
    {
        return Error == WSAEWOULDBLOCK || Error == WSAEINPROGRESS || Error == WSAEALREADY;
    }

    void CloseSocket(SOCKET& Socket)
    {
        if (Socket != INVALID_SOCKET)
        {
            closesocket(Socket);
            Socket = INVALID_SOCKET;
        }
    }

    struct FVirtualClient
    {
        std::size_t GlobalIndex = 0;
        EClientState State = EClientState::NotStarted;
        SOCKET TcpSocket = INVALID_SOCKET;
        SOCKET UdpSocket = INVALID_SOCKET;
        Network::FReceiveBuffer ReceiveBuffer;
        Network::FSendBuffer SendBuffer;
        Network::FNetworkEntityId PlayerId = 0;
        std::uint64_t UdpSessionToken = 0;
        std::uint32_t NextInputSequence = 1;
        std::uint32_t LastAcknowledgedInput = 0;
        FClock::time_point ScheduledConnect;
        FClock::time_point ConnectStarted;
        FClock::time_point NextPacketTime;
        std::deque<std::pair<std::uint32_t, std::uint64_t>> PendingInputs;
        std::unordered_map<Network::FNetworkEntityId, std::uint32_t> LastServerTicks;
    };

    struct FPollTarget
    {
        std::size_t ClientIndex = 0;
        bool bUdp = false;
    };
}

class FLoadTestRunner::FImpl
{
public:
    explicit FImpl(FLoadTestConfig InConfig)
        : Config(std::move(InConfig))
    {
    }

    int Run(const std::atomic<bool>& ExternalStopRequested)
    {
        std::string Error;
        if (!ResolveServerAddress(Error))
        {
            std::cerr << "Load tester initialization failed: " << Error << '\n';
            return 1;
        }

        FLoadTestReporter Reporter(Config, Stats);
        if (!Reporter.Initialize(Error))
        {
            std::cerr << "Load tester initialization failed: " << Error << '\n';
            return 1;
        }

        StartTime = FClock::now();
        Deadline = StartTime + std::chrono::seconds(Config.DurationSeconds);
        std::cout << "GameTechLab load test starting\n"
                  << "  target=" << Config.ServerAddress << ':' << Config.ServerPort
                  << "  scenario=" << ToString(Config.Scenario)
                  << "  clients=" << Config.ClientCount
                  << "  duration=" << Config.DurationSeconds << "s"
                  << "  connect_rate=" << Config.ConnectRatePerSecond << "/s"
                  << "  packet_rate=" << Config.PacketRatePerClient << "/client/s"
                  << "  workers=" << Config.WorkerCount << '\n';
        if (Config.Scenario == ELoadTestScenario::Movement && Config.ClientCount >= 200)
        {
            std::cout << "  note: movement broadcasts player states to every UDP-ready client; this intentionally stresses O(N^2) fan-out.\n";
        }

        Workers.reserve(Config.WorkerCount);
        for (std::uint32_t WorkerIndex = 0; WorkerIndex < Config.WorkerCount; ++WorkerIndex)
        {
            Workers.emplace_back([this, WorkerIndex]() { WorkerMain(WorkerIndex); });
        }

        auto NextReport = StartTime + std::chrono::seconds(1);
        while (!ExternalStopRequested.load(std::memory_order_relaxed) && FClock::now() < Deadline)
        {
            const auto Now = FClock::now();
            if (Now >= NextReport)
            {
                Reporter.Report(std::chrono::duration<double>(Now - StartTime).count());
                do { NextReport += std::chrono::seconds(1); } while (NextReport <= Now);
            }
            Sleep(10);
        }

        StopRequested.store(true, std::memory_order_relaxed);
        for (std::thread& Worker : Workers)
        {
            if (Worker.joinable()) Worker.join();
        }
        const double ElapsedSeconds = std::chrono::duration<double>(FClock::now() - StartTime).count();
        Reporter.PrintFinal(ElapsedSeconds);
        return Stats.CaptureCounters().Connected > 0 ? 0 : 2;
    }

private:
    bool ResolveServerAddress(std::string& OutError)
    {
        addrinfo Hints{};
        Hints.ai_family = AF_UNSPEC;
        Hints.ai_socktype = SOCK_STREAM;
        Hints.ai_protocol = IPPROTO_TCP;
        addrinfo* Results = nullptr;
        const std::string Port = std::to_string(Config.ServerPort);
        const int Result = getaddrinfo(Config.ServerAddress.c_str(), Port.c_str(), &Hints, &Results);
        if (Result != 0 || !Results)
        {
            OutError = "getaddrinfo failed for " + Config.ServerAddress + ": " + std::to_string(Result);
            return false;
        }

        bool bFound = false;
        for (addrinfo* Entry = Results; Entry; Entry = Entry->ai_next)
        {
            if ((Entry->ai_family == AF_INET || Entry->ai_family == AF_INET6) &&
                Entry->ai_addrlen <= static_cast<int>(sizeof(ServerAddress)))
            {
                std::memcpy(&ServerAddress, Entry->ai_addr, Entry->ai_addrlen);
                ServerAddressLength = static_cast<int>(Entry->ai_addrlen);
                bFound = true;
                break;
            }
        }
        freeaddrinfo(Results);
        if (!bFound) OutError = "no IPv4 or IPv6 address was resolved";
        return bFound;
    }

    void WorkerMain(std::uint32_t WorkerIndex)
    {
        std::vector<FVirtualClient> Clients;
        Clients.reserve((Config.ClientCount + Config.WorkerCount - 1) / Config.WorkerCount);
        for (std::size_t GlobalIndex = WorkerIndex; GlobalIndex < Config.ClientCount; GlobalIndex += Config.WorkerCount)
        {
            FVirtualClient Client;
            Client.GlobalIndex = GlobalIndex;
            const double DelaySeconds = static_cast<double>(GlobalIndex) / static_cast<double>(Config.ConnectRatePerSecond);
            Client.ScheduledConnect = StartTime + std::chrono::duration_cast<FClock::duration>(std::chrono::duration<double>(DelaySeconds));
            Clients.push_back(std::move(Client));
        }

        std::vector<WSAPOLLFD> PollDescriptors;
        std::vector<FPollTarget> PollTargets;
        PollDescriptors.reserve(Clients.size() * 2);
        PollTargets.reserve(Clients.size() * 2);

        while (!StopRequested.load(std::memory_order_relaxed) && FClock::now() < Deadline)
        {
            const auto Now = FClock::now();
            for (FVirtualClient& Client : Clients)
            {
                if (Client.State == EClientState::NotStarted && Now >= Client.ScheduledConnect)
                {
                    StartConnection(Client);
                }
                else if ((Client.State == EClientState::Connecting || Client.State == EClientState::Handshaking) &&
                         Now - Client.ConnectStarted > std::chrono::seconds(10))
                {
                    FailClient(Client);
                }
                if (Client.State == EClientState::Active) ScheduleTraffic(Client, Now);
            }

            PollDescriptors.clear();
            PollTargets.clear();
            for (std::size_t ClientIndex = 0; ClientIndex < Clients.size(); ++ClientIndex)
            {
                FVirtualClient& Client = Clients[ClientIndex];
                if (Client.TcpSocket != INVALID_SOCKET && Client.State != EClientState::Failed)
                {
                    WSAPOLLFD Descriptor{};
                    Descriptor.fd = Client.TcpSocket;
                    Descriptor.events = POLLRDNORM;
                    if (Client.State == EClientState::Connecting || !Client.SendBuffer.Empty()) Descriptor.events |= POLLWRNORM;
                    PollDescriptors.push_back(Descriptor);
                    PollTargets.push_back({ClientIndex, false});
                }
                if (Client.UdpSocket != INVALID_SOCKET && Client.State == EClientState::Active)
                {
                    WSAPOLLFD Descriptor{};
                    Descriptor.fd = Client.UdpSocket;
                    Descriptor.events = POLLRDNORM;
                    PollDescriptors.push_back(Descriptor);
                    PollTargets.push_back({ClientIndex, true});
                }
            }

            if (PollDescriptors.empty())
            {
                Sleep(1);
                continue;
            }
            const int PollResult = WSAPoll(PollDescriptors.data(), static_cast<ULONG>(PollDescriptors.size()), 5);
            if (PollResult == SOCKET_ERROR)
            {
                Sleep(1);
                continue;
            }
            if (PollResult <= 0) continue;

            for (std::size_t PollIndex = 0; PollIndex < PollDescriptors.size(); ++PollIndex)
            {
                const short Events = PollDescriptors[PollIndex].revents;
                if (Events == 0) continue;
                FVirtualClient& Client = Clients[PollTargets[PollIndex].ClientIndex];
                if (Client.State == EClientState::Failed) continue;

                if (PollTargets[PollIndex].bUdp)
                {
                    if ((Events & (POLLERR | POLLHUP | POLLNVAL)) != 0) FailClient(Client);
                    else if ((Events & POLLRDNORM) != 0) ReceiveUdp(Client);
                    continue;
                }

                if (Client.State == EClientState::Connecting)
                {
                    if ((Events & (POLLWRNORM | POLLERR | POLLHUP | POLLNVAL)) != 0) FinishConnection(Client);
                    if (Client.State == EClientState::Failed || Client.State == EClientState::Connecting) continue;
                }
                if ((Events & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                {
                    FailClient(Client);
                    continue;
                }
                if ((Events & POLLWRNORM) != 0) FlushTcp(Client);
                if (Client.State != EClientState::Failed && (Events & POLLRDNORM) != 0) ReceiveTcp(Client);
            }
        }

        for (FVirtualClient& Client : Clients)
        {
            CloseSocket(Client.UdpSocket);
            CloseSocket(Client.TcpSocket);
        }
    }

    void StartConnection(FVirtualClient& Client)
    {
        Stats.RecordConnectionAttempt();
        Client.ConnectStarted = FClock::now();
        Client.State = EClientState::Connecting;
        Client.TcpSocket = socket(ServerAddress.ss_family, SOCK_STREAM, IPPROTO_TCP);
        if (Client.TcpSocket == INVALID_SOCKET || !SetNonBlocking(Client.TcpSocket))
        {
            FailClient(Client);
            return;
        }
        BOOL NoDelay = TRUE;
        setsockopt(Client.TcpSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay));

        const int Result = connect(Client.TcpSocket, reinterpret_cast<const sockaddr*>(&ServerAddress), ServerAddressLength);
        if (Result == 0)
        {
            Client.State = EClientState::Handshaking;
            QueueHello(Client);
        }
        else if (IsWouldBlock(WSAGetLastError()))
        {
            Client.State = EClientState::Connecting;
        }
        else
        {
            FailClient(Client);
        }
    }

    void FinishConnection(FVirtualClient& Client)
    {
        int SocketError = 0;
        int ErrorLength = sizeof(SocketError);
        if (getsockopt(Client.TcpSocket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&SocketError), &ErrorLength) != 0 || SocketError != 0)
        {
            FailClient(Client);
            return;
        }
        Client.State = EClientState::Handshaking;
        QueueHello(Client);
    }

    void QueueHello(FVirtualClient& Client)
    {
        Network::FC2SHello Hello;
        Hello.ClientOptions = Network::ToOptionBits(Network::EClientNetworkOption::UdpMovement);
        QueueTcp(Client, Network::Serialize(Hello));
    }

    void QueueTcp(FVirtualClient& Client, std::vector<std::uint8_t> Packet)
    {
        const std::size_t Size = Packet.size();
        if (!Client.SendBuffer.Enqueue(std::move(Packet)))
        {
            Stats.RecordLocalSendDrop();
            FailClient(Client);
            return;
        }
        Stats.RecordTcpPacketSent(Size);
    }

    void FlushTcp(FVirtualClient& Client)
    {
        while (!Client.SendBuffer.Empty())
        {
            const std::size_t Available = Client.SendBuffer.Size();
            const int SendSize = static_cast<int>(std::min<std::size_t>(Available, static_cast<std::size_t>(std::numeric_limits<int>::max())));
            const int Sent = send(Client.TcpSocket, reinterpret_cast<const char*>(Client.SendBuffer.Data()), SendSize, 0);
            if (Sent > 0)
            {
                Client.SendBuffer.Consume(static_cast<std::size_t>(Sent));
                Stats.RecordTcpBytesSent(static_cast<std::size_t>(Sent));
                continue;
            }
            if (Sent == SOCKET_ERROR && IsWouldBlock(WSAGetLastError())) return;
            FailClient(Client);
            return;
        }
    }

    void ReceiveTcp(FVirtualClient& Client)
    {
        std::array<std::uint8_t, 16384> Buffer{};
        while (Client.State != EClientState::Failed)
        {
            const int Received = recv(Client.TcpSocket, reinterpret_cast<char*>(Buffer.data()), static_cast<int>(Buffer.size()), 0);
            if (Received > 0)
            {
                Stats.RecordTcpBytesReceived(static_cast<std::size_t>(Received));
                if (!Client.ReceiveBuffer.Append(Buffer.data(), static_cast<std::size_t>(Received)))
                {
                    Stats.RecordInvalidPacket();
                    FailClient(Client);
                    return;
                }
                std::vector<Network::FPacket> Packets;
                std::string Error;
                if (!Client.ReceiveBuffer.ExtractPackets(Packets, Error))
                {
                    Stats.RecordInvalidPacket();
                    FailClient(Client);
                    return;
                }
                for (const Network::FPacket& Packet : Packets)
                {
                    Stats.RecordTcpPacketReceived();
                    HandleTcpPacket(Client, Packet);
                    if (Client.State == EClientState::Failed) return;
                }
                continue;
            }
            if (Received == 0)
            {
                FailClient(Client);
                return;
            }
            if (IsWouldBlock(WSAGetLastError())) return;
            FailClient(Client);
            return;
        }
    }

    void HandleTcpPacket(FVirtualClient& Client, const Network::FPacket& Packet)
    {
        if (Packet.Type == Network::EPacketType::S2C_Connected)
        {
            Network::FS2CConnected Connected;
            if (Client.State != EClientState::Handshaking || !Network::Deserialize(Packet, Connected) || Connected.PlayerId == 0)
            {
                Stats.RecordInvalidPacket();
                FailClient(Client);
                return;
            }
            Client.PlayerId = Connected.PlayerId;
            Client.UdpSessionToken = Connected.UdpSessionToken;
            Client.State = EClientState::Active;
            Client.NextPacketTime = FClock::now();
            const double HandshakeMs = std::chrono::duration<double, std::milli>(FClock::now() - Client.ConnectStarted).count();
            Stats.RecordConnected(HandshakeMs);
            if (Config.Scenario == ELoadTestScenario::Movement && !OpenUdp(Client, Connected.UdpPort))
            {
                FailClient(Client);
            }
            return;
        }
        if (Packet.Type == Network::EPacketType::S2C_Pong)
        {
            Network::FS2CPong Pong;
            if (!Network::Deserialize(Packet, Pong))
            {
                Stats.RecordInvalidPacket();
                return;
            }
            const std::uint64_t Now = NowMicros();
            if (Pong.ClientTimestampMicros <= Now)
            {
                const double RttMs = static_cast<double>(Now - Pong.ClientTimestampMicros) / 1000.0;
                if (RttMs <= 60000.0) Stats.RecordPingRtt(RttMs);
            }
        }
    }

    bool OpenUdp(FVirtualClient& Client, std::uint16_t UdpPort)
    {
        Client.UdpSocket = socket(ServerAddress.ss_family, SOCK_DGRAM, IPPROTO_UDP);
        if (Client.UdpSocket == INVALID_SOCKET || !SetNonBlocking(Client.UdpSocket)) return false;
        int ReceiveBufferBytes = 1024 * 1024;
        setsockopt(Client.UdpSocket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&ReceiveBufferBytes), sizeof(ReceiveBufferBytes));

        sockaddr_storage UdpAddress = ServerAddress;
        if (UdpAddress.ss_family == AF_INET)
        {
            reinterpret_cast<sockaddr_in*>(&UdpAddress)->sin_port = htons(UdpPort);
        }
        else
        {
            reinterpret_cast<sockaddr_in6*>(&UdpAddress)->sin6_port = htons(UdpPort);
        }
        return connect(Client.UdpSocket, reinterpret_cast<const sockaddr*>(&UdpAddress), ServerAddressLength) == 0;
    }

    void ScheduleTraffic(FVirtualClient& Client, FClock::time_point Now)
    {
        if (Config.Scenario == ELoadTestScenario::Connect || Now < Client.NextPacketTime) return;
        const auto Interval = std::chrono::duration_cast<FClock::duration>(
            std::chrono::duration<double>(1.0 / static_cast<double>(Config.PacketRatePerClient)));

        if (Config.Scenario == ELoadTestScenario::Ping)
        {
            Network::FC2SPing Ping;
            Ping.ClientTimestampMicros = NowMicros();
            QueueTcp(Client, Network::Serialize(Ping));
        }
        else if (Config.Scenario == ELoadTestScenario::Movement)
        {
            SendMovement(Client);
        }

        Client.NextPacketTime += Interval;
        if (Client.NextPacketTime + Interval < Now) Client.NextPacketTime = Now + Interval;
    }

    void SendMovement(FVirtualClient& Client)
    {
        if (Client.UdpSocket == INVALID_SOCKET) return;
        Network::FUdpMoveInput Move;
        Move.PlayerId = Client.PlayerId;
        Move.SessionToken = Client.UdpSessionToken;
        Move.Sequence = Client.NextInputSequence++;
        const std::uint32_t Phase = (Move.Sequence / std::max(1u, Config.PacketRatePerClient) + static_cast<std::uint32_t>(Client.GlobalIndex)) % 4;
        Move.MoveX = Phase == 0 ? 1.0f : Phase == 2 ? -1.0f : 0.0f;
        Move.MoveY = Phase == 1 ? 1.0f : Phase == 3 ? -1.0f : 0.0f;
        const std::vector<std::uint8_t> Bytes = Network::SerializeUdp(Move);
        const int Sent = send(Client.UdpSocket, reinterpret_cast<const char*>(Bytes.data()), static_cast<int>(Bytes.size()), 0);
        if (Sent == static_cast<int>(Bytes.size()))
        {
            Stats.RecordUdpPacketSent(Bytes.size());
            Client.PendingInputs.emplace_back(Move.Sequence, NowMicros());
            while (Client.PendingInputs.size() > 1024) Client.PendingInputs.pop_front();
        }
        else if (Sent == SOCKET_ERROR && IsWouldBlock(WSAGetLastError()))
        {
            Stats.RecordLocalSendDrop();
        }
        else
        {
            FailClient(Client);
        }
    }

    void ReceiveUdp(FVirtualClient& Client)
    {
        std::array<std::uint8_t, Network::MaxUdpDatagramSize> Buffer{};
        while (Client.State == EClientState::Active)
        {
            const int Received = recv(Client.UdpSocket, reinterpret_cast<char*>(Buffer.data()), static_cast<int>(Buffer.size()), 0);
            if (Received > 0)
            {
                Network::FUdpPlayerState State;
                if (!Network::DeserializeUdp(Buffer.data(), static_cast<std::size_t>(Received), State))
                {
                    Stats.RecordInvalidPacket();
                    continue;
                }
                Stats.RecordUdpPacketReceived(static_cast<std::size_t>(Received));
                TrackServerTick(Client, State);
                if (State.PlayerId == Client.PlayerId) TrackMovementAck(Client, State.LastProcessedInput);
                continue;
            }
            if (Received == SOCKET_ERROR && IsWouldBlock(WSAGetLastError())) return;
            FailClient(Client);
            return;
        }
    }

    void TrackServerTick(FVirtualClient& Client, const Network::FUdpPlayerState& State)
    {
        auto [It, bInserted] = Client.LastServerTicks.try_emplace(State.PlayerId, State.ServerTick);
        if (bInserted) return;
        const std::uint32_t Previous = It->second;
        if (Network::IsSequenceNewer(State.ServerTick, Previous))
        {
            const std::uint32_t Gap = State.ServerTick - Previous;
            if (Gap > 1) Stats.RecordUdpMissing(static_cast<std::uint64_t>(Gap - 1));
            It->second = State.ServerTick;
        }
        else
        {
            Stats.RecordUdpOutOfOrder();
        }
    }

    void TrackMovementAck(FVirtualClient& Client, std::uint32_t AcknowledgedSequence)
    {
        if (AcknowledgedSequence == 0 ||
            (Client.LastAcknowledgedInput != 0 && !Network::IsSequenceNewer(AcknowledgedSequence, Client.LastAcknowledgedInput)))
        {
            return;
        }
        const std::uint64_t Now = NowMicros();
        for (const auto& Pending : Client.PendingInputs)
        {
            if (Pending.first == AcknowledgedSequence && Pending.second <= Now)
            {
                Stats.RecordMovementAck(static_cast<double>(Now - Pending.second) / 1000.0);
                break;
            }
        }
        while (!Client.PendingInputs.empty() && !Network::IsSequenceNewer(Client.PendingInputs.front().first, AcknowledgedSequence))
        {
            Client.PendingInputs.pop_front();
        }
        Client.LastAcknowledgedInput = AcknowledgedSequence;
    }

    void FailClient(FVirtualClient& Client)
    {
        if (Client.State == EClientState::Failed || Client.State == EClientState::NotStarted) return;
        if (Client.State == EClientState::Active) Stats.RecordDisconnected();
        else Stats.RecordConnectionFailure();
        Client.State = EClientState::Failed;
        CloseSocket(Client.UdpSocket);
        CloseSocket(Client.TcpSocket);
        Client.SendBuffer.Clear();
        Client.ReceiveBuffer.Clear();
    }

private:
    FLoadTestConfig Config;
    FLoadTestStats Stats;
    sockaddr_storage ServerAddress{};
    int ServerAddressLength = 0;
    FClock::time_point StartTime;
    FClock::time_point Deadline;
    std::atomic<bool> StopRequested{false};
    std::vector<std::thread> Workers;
};

FLoadTestRunner::FLoadTestRunner(FLoadTestConfig InConfig)
    : Impl(std::make_unique<FImpl>(std::move(InConfig)))
{
}

FLoadTestRunner::~FLoadTestRunner() = default;

int FLoadTestRunner::Run(const std::atomic<bool>& ExternalStopRequested)
{
    return Impl->Run(ExternalStopRequested);
}
