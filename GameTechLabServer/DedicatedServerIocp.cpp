#include "DedicatedServer.h"

#include "../NetworkShared/Buffer/ReceiveBuffer.h"
#include "../NetworkShared/Buffer/SendBuffer.h"
#include "../NetworkShared/Protocol/UdpProtocol.h"

#include <MSWSock.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>

void FDedicatedServer::IocpNetworkLoop()
{
    using Clock = std::chrono::steady_clock;

    SYSTEM_INFO SystemInfo{};
    GetSystemInfo(&SystemInfo);
    const std::uint32_t LogicalProcessorCount =
        std::max<std::uint32_t>(1, SystemInfo.dwNumberOfProcessors);
    const std::uint32_t WorkerCount = Config.IocpWorkerThreads == 0
        ? std::max<std::uint32_t>(1, LogicalProcessorCount - 1)
        : Config.IocpWorkerThreads;

    HANDLE CompletionPort = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE, nullptr, 0, static_cast<DWORD>(WorkerCount));
    if (!CompletionPort)
    {
        Logger.Log("CreateIoCompletionPort failed: " + std::to_string(GetLastError()));
        bRunning = false;
        return;
    }

    auto AssociateSocket = [&](SOCKET Socket)
    {
        return CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(Socket), CompletionPort, 0, 0) == CompletionPort;
    };

    if (!AssociateSocket(ListenSocket) || !AssociateSocket(UdpSocket))
    {
        Logger.Log("IOCP socket association failed: " + std::to_string(GetLastError()));
        CloseHandle(CompletionPort);
        bRunning = false;
        return;
    }

    LPFN_ACCEPTEX AcceptExFunction = nullptr;
    GUID AcceptExGuid = WSAID_ACCEPTEX;
    DWORD ExtensionBytes = 0;
    if (WSAIoctl(ListenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &AcceptExGuid, sizeof(AcceptExGuid),
        &AcceptExFunction, sizeof(AcceptExFunction),
        &ExtensionBytes, nullptr, nullptr) == SOCKET_ERROR || !AcceptExFunction)
    {
        Logger.Log("AcceptEx lookup failed: " + std::to_string(WSAGetLastError()));
        CloseHandle(CompletionPort);
        bRunning = false;
        return;
    }

    struct FIocpConnection
    {
        SOCKET Socket = INVALID_SOCKET;
        Network::FSessionId SessionId = 0;
        std::atomic<bool> bClosed{false};
        std::mutex SocketMutex;
        std::mutex ReceiveMutex;
        std::mutex SendMutex;
        Network::FReceiveBuffer ReceiveBuffer;
        Network::FSendBuffer SendBuffer;
        bool bSendInFlight = false;
    };

    enum class EIoOperation
    {
        Accept,
        TcpReceive,
        TcpSend,
        UdpReceive,
        UdpSend
    };

    struct FIocpOperation : OVERLAPPED
    {
        explicit FIocpOperation(EIoOperation InType)
            : OVERLAPPED{}, Type(InType)
        {
        }

        EIoOperation Type;
        std::shared_ptr<FIocpConnection> Connection;
        SOCKET AcceptSocket = INVALID_SOCKET;
        std::array<std::uint8_t, 8192> FixedBuffer{};
        std::vector<std::uint8_t> OwnedBytes;
        WSABUF Buffer{};
        sockaddr_storage RemoteAddress{};
        int RemoteAddressLength = sizeof(sockaddr_storage);
        DWORD Flags = 0;
        DWORD ImmediateBytes = 0;
    };

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

    std::mutex ListenSocketMutex;
    std::mutex UdpSocketMutex;
    std::mutex ConnectionsMutex;
    std::unordered_map<Network::FSessionId, std::shared_ptr<FIocpConnection>> Connections;
    std::atomic<Network::FSessionId> NextSessionId{1};
    std::atomic<std::uint64_t> OutstandingIo{0};
    Network::TThreadSafeQueue<FNetworkEvent> CompletedUdpInputs{65536};

    std::function<void(const std::shared_ptr<FIocpConnection>&, int)> Disconnect;
    std::function<void(const std::shared_ptr<FIocpConnection>&)> PostTcpReceive;
    std::function<void(const std::shared_ptr<FIocpConnection>&)> StartTcpSend;
    std::function<void()> PostAccept;
    std::function<void()> PostUdpReceive;
    std::function<void(FUdpSendCommand)> PostUdpSend;

    auto FindConnection = [&](Network::FSessionId SessionId)
    {
        std::lock_guard<std::mutex> Lock(ConnectionsMutex);
        const auto It = Connections.find(SessionId);
        return It == Connections.end() ? std::shared_ptr<FIocpConnection>{} : It->second;
    };

    Disconnect = [&](const std::shared_ptr<FIocpConnection>& Connection, int ErrorCode)
    {
        if (!Connection || Connection->bClosed.exchange(true)) return;

        {
            std::lock_guard<std::mutex> SocketLock(Connection->SocketMutex);
            if (Connection->Socket != INVALID_SOCKET)
            {
                shutdown(Connection->Socket, SD_BOTH);
                closesocket(Connection->Socket);
                Connection->Socket = INVALID_SOCKET;
            }
        }
        {
            std::lock_guard<std::mutex> Lock(ConnectionsMutex);
            const auto It = Connections.find(Connection->SessionId);
            if (It != Connections.end() && It->second == Connection)
            {
                Connections.erase(It);
            }
        }
        PerformanceStats.RecordConnectionClosed();
        if (bRunning && !IncomingEvents.TryPush(
            {ENetworkEventType::Disconnected, Connection->SessionId, {}, ErrorCode}))
        {
            PerformanceStats.RecordQueueOverflow();
            Logger.Log("Incoming disconnect queue overflow");
        }
    };

    PostTcpReceive = [&](const std::shared_ptr<FIocpConnection>& Connection)
    {
        if (!Connection || !bRunning) return;

        auto* Operation = new FIocpOperation(EIoOperation::TcpReceive);
        Operation->Connection = Connection;
        Operation->Buffer.buf = reinterpret_cast<char*>(Operation->FixedBuffer.data());
        Operation->Buffer.len = static_cast<ULONG>(Operation->FixedBuffer.size());

        int SubmitError = 0;
        {
            std::lock_guard<std::mutex> SocketLock(Connection->SocketMutex);
            if (Connection->bClosed || Connection->Socket == INVALID_SOCKET || !bRunning)
            {
                delete Operation;
                return;
            }

            OutstandingIo.fetch_add(1);
            PerformanceStats.RecordIoSubmitted();
            Operation->Flags = 0;
            const int Result = WSARecv(Connection->Socket, &Operation->Buffer, 1,
                nullptr, &Operation->Flags, Operation, nullptr);
            if (Result == SOCKET_ERROR)
            {
                const int Error = WSAGetLastError();
                if (Error != WSA_IO_PENDING)
                {
                    OutstandingIo.fetch_sub(1);
                    PerformanceStats.RecordIoSubmissionFailed();
                    SubmitError = Error;
                }
            }
        }

        if (SubmitError != 0)
        {
            delete Operation;
            Disconnect(Connection, SubmitError);
        }
    };

    StartTcpSend = [&](const std::shared_ptr<FIocpConnection>& Connection)
    {
        if (!Connection || !bRunning || Connection->bClosed) return;

        auto* Operation = new FIocpOperation(EIoOperation::TcpSend);
        Operation->Connection = Connection;

        {
            std::lock_guard<std::mutex> SendLock(Connection->SendMutex);
            if (Connection->bSendInFlight || Connection->SendBuffer.Empty() || Connection->bClosed)
            {
                delete Operation;
                return;
            }
            Connection->bSendInFlight = true;
            const std::uint8_t* Data = Connection->SendBuffer.Data();
            const std::size_t Size = Connection->SendBuffer.Size();
            Operation->OwnedBytes.assign(Data, Data + Size);
        }

        Operation->Buffer.buf = reinterpret_cast<char*>(Operation->OwnedBytes.data());
        Operation->Buffer.len = static_cast<ULONG>(Operation->OwnedBytes.size());

        int SubmitError = 0;
        {
            std::lock_guard<std::mutex> SocketLock(Connection->SocketMutex);
            if (Connection->bClosed || Connection->Socket == INVALID_SOCKET || !bRunning)
            {
                SubmitError = WSA_OPERATION_ABORTED;
            }
            else
            {
                OutstandingIo.fetch_add(1);
                PerformanceStats.RecordIoSubmitted();
                const int Result = WSASend(Connection->Socket, &Operation->Buffer, 1,
                    nullptr, 0, Operation, nullptr);
                if (Result == SOCKET_ERROR)
                {
                    const int Error = WSAGetLastError();
                    if (Error != WSA_IO_PENDING)
                    {
                        OutstandingIo.fetch_sub(1);
                        PerformanceStats.RecordIoSubmissionFailed();
                        SubmitError = Error;
                    }
                }
            }
        }

        if (SubmitError != 0)
        {
            {
                std::lock_guard<std::mutex> SendLock(Connection->SendMutex);
                Connection->bSendInFlight = false;
            }
            delete Operation;
            if (SubmitError != WSA_OPERATION_ABORTED) Disconnect(Connection, SubmitError);
        }
    };

    PostAccept = [&]()
    {
        if (!bRunning) return;

        auto* Operation = new FIocpOperation(EIoOperation::Accept);
        Operation->AcceptSocket = WSASocket(
            AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (Operation->AcceptSocket == INVALID_SOCKET)
        {
            Logger.Log("IOCP accept socket creation failed: " + std::to_string(WSAGetLastError()));
            delete Operation;
            return;
        }

        constexpr DWORD AddressBytes = sizeof(sockaddr_storage) + 16;
        int SubmitError = 0;
        bool bSubmitted = false;
        {
            std::lock_guard<std::mutex> ListenLock(ListenSocketMutex);
            if (bRunning && ListenSocket != INVALID_SOCKET)
            {
                OutstandingIo.fetch_add(1);
                PerformanceStats.RecordIoSubmitted();
                const BOOL Result = AcceptExFunction(
                    ListenSocket, Operation->AcceptSocket,
                    Operation->FixedBuffer.data(), 0,
                    AddressBytes, AddressBytes,
                    &Operation->ImmediateBytes, Operation);
                if (Result || WSAGetLastError() == WSA_IO_PENDING)
                {
                    bSubmitted = true;
                }
                else
                {
                    SubmitError = WSAGetLastError();
                    OutstandingIo.fetch_sub(1);
                    PerformanceStats.RecordIoSubmissionFailed();
                }
            }
        }

        if (bSubmitted) return;
        closesocket(Operation->AcceptSocket);
        delete Operation;
        if (SubmitError != 0 && bRunning)
        {
            Logger.Log("AcceptEx submit failed: " + std::to_string(SubmitError));
        }
    };

    PostUdpReceive = [&]()
    {
        if (!bRunning) return;

        auto* Operation = new FIocpOperation(EIoOperation::UdpReceive);
        Operation->Buffer.buf = reinterpret_cast<char*>(Operation->FixedBuffer.data());
        Operation->Buffer.len = static_cast<ULONG>(
            std::min<std::size_t>(Operation->FixedBuffer.size(), Network::MaxUdpDatagramSize));
        Operation->RemoteAddressLength = sizeof(Operation->RemoteAddress);
        Operation->Flags = 0;

        int SubmitError = 0;
        bool bSubmitted = false;
        {
            std::lock_guard<std::mutex> UdpLock(UdpSocketMutex);
            if (bRunning && UdpSocket != INVALID_SOCKET)
            {
                OutstandingIo.fetch_add(1);
                PerformanceStats.RecordIoSubmitted();
                const int Result = WSARecvFrom(UdpSocket, &Operation->Buffer, 1,
                    nullptr, &Operation->Flags,
                    reinterpret_cast<sockaddr*>(&Operation->RemoteAddress),
                    &Operation->RemoteAddressLength, Operation, nullptr);
                if (Result == 0 || WSAGetLastError() == WSA_IO_PENDING)
                {
                    bSubmitted = true;
                }
                else
                {
                    SubmitError = WSAGetLastError();
                    OutstandingIo.fetch_sub(1);
                    PerformanceStats.RecordIoSubmissionFailed();
                }
            }
        }

        if (bSubmitted) return;
        delete Operation;
        if (SubmitError != 0 && bRunning)
        {
            Logger.Log("WSARecvFrom submit failed: " + std::to_string(SubmitError));
        }
    };

    PostUdpSend = [&](FUdpSendCommand Command)
    {
        if (!bRunning || Command.Bytes.empty() || Command.RemoteAddressLength <= 0) return;

        auto* Operation = new FIocpOperation(EIoOperation::UdpSend);
        Operation->OwnedBytes = std::move(Command.Bytes);
        Operation->Buffer.buf = reinterpret_cast<char*>(Operation->OwnedBytes.data());
        Operation->Buffer.len = static_cast<ULONG>(Operation->OwnedBytes.size());
        Operation->RemoteAddress = Command.RemoteAddress;
        Operation->RemoteAddressLength = Command.RemoteAddressLength;

        int SubmitError = 0;
        bool bSubmitted = false;
        {
            std::lock_guard<std::mutex> UdpLock(UdpSocketMutex);
            if (bRunning && UdpSocket != INVALID_SOCKET)
            {
                OutstandingIo.fetch_add(1);
                PerformanceStats.RecordIoSubmitted();
                const int Result = WSASendTo(UdpSocket, &Operation->Buffer, 1,
                    nullptr, 0,
                    reinterpret_cast<const sockaddr*>(&Operation->RemoteAddress),
                    Operation->RemoteAddressLength, Operation, nullptr);
                if (Result == 0 || WSAGetLastError() == WSA_IO_PENDING)
                {
                    bSubmitted = true;
                }
                else
                {
                    SubmitError = WSAGetLastError();
                    OutstandingIo.fetch_sub(1);
                    PerformanceStats.RecordIoSubmissionFailed();
                }
            }
        }

        if (bSubmitted) return;
        delete Operation;
        if (SubmitError != 0 && bRunning)
        {
            Logger.Log("WSASendTo submit failed: " + std::to_string(SubmitError));
        }
    };

    auto WorkerLoop = [&]()
    {
        for (;;)
        {
            DWORD BytesTransferred = 0;
            ULONG_PTR CompletionKey = 0;
            OVERLAPPED* Overlapped = nullptr;
            const BOOL Succeeded = GetQueuedCompletionStatus(
                CompletionPort, &BytesTransferred, &CompletionKey, &Overlapped, INFINITE);
            const int CompletionError = Succeeded ? 0 : static_cast<int>(GetLastError());

            if (!Overlapped)
            {
                break;
            }

            OutstandingIo.fetch_sub(1);
            const bool bExpectedShutdownCancellation = !bRunning &&
                CompletionError == WSA_OPERATION_ABORTED;
            PerformanceStats.RecordIoCompletion(Succeeded || bExpectedShutdownCancellation);
            std::unique_ptr<FIocpOperation> Operation(
                static_cast<FIocpOperation*>(Overlapped));

            switch (Operation->Type)
            {
            case EIoOperation::Accept:
            {
                SOCKET AcceptedSocket = Operation->AcceptSocket;
                Operation->AcceptSocket = INVALID_SOCKET;
                if (!Succeeded || !bRunning)
                {
                    if (AcceptedSocket != INVALID_SOCKET) closesocket(AcceptedSocket);
                    if (bRunning && CompletionError != WSA_OPERATION_ABORTED)
                    {
                        Logger.Log("AcceptEx completion failed: " + std::to_string(CompletionError));
                    }
                    if (bRunning) PostAccept();
                    break;
                }

                bool bAcceptAllowed = false;
                {
                    std::lock_guard<std::mutex> Lock(ConnectionsMutex);
                    bAcceptAllowed = Connections.size() < Config.MaxClients;
                }

                SOCKET ListenForContext = INVALID_SOCKET;
                {
                    std::lock_guard<std::mutex> ListenLock(ListenSocketMutex);
                    ListenForContext = ListenSocket;
                    if (ListenForContext != INVALID_SOCKET)
                    {
                        setsockopt(AcceptedSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                            reinterpret_cast<const char*>(&ListenForContext), sizeof(ListenForContext));
                    }
                }

                BOOL NoDelay = TRUE;
                const bool bConfigured = ListenForContext != INVALID_SOCKET &&
                    setsockopt(AcceptedSocket, IPPROTO_TCP, TCP_NODELAY,
                        reinterpret_cast<const char*>(&NoDelay), sizeof(NoDelay)) == 0;
                if (!bAcceptAllowed || !bConfigured || !AssociateSocket(AcceptedSocket))
                {
                    closesocket(AcceptedSocket);
                    PostAccept();
                    break;
                }

                auto Connection = std::make_shared<FIocpConnection>();
                Connection->Socket = AcceptedSocket;
                Connection->SessionId = NextSessionId.fetch_add(1);
                {
                    std::lock_guard<std::mutex> Lock(ConnectionsMutex);
                    if (!bRunning || Connections.size() >= Config.MaxClients)
                    {
                        closesocket(AcceptedSocket);
                        PostAccept();
                        break;
                    }
                    Connections.emplace(Connection->SessionId, Connection);
                }
                PerformanceStats.RecordConnectionAccepted();

                if (!IncomingEvents.TryPush(
                    {ENetworkEventType::Connected, Connection->SessionId, {}, 0}))
                {
                    PerformanceStats.RecordQueueOverflow();
                    Disconnect(Connection, WSAENOBUFS);
                }
                else
                {
                    PostTcpReceive(Connection);
                }
                PostAccept();
                break;
            }
            case EIoOperation::TcpReceive:
            {
                const auto Connection = Operation->Connection;
                if (!Connection || Connection->bClosed) break;
                if (!Succeeded || BytesTransferred == 0)
                {
                    Disconnect(Connection, Succeeded ? 0 : CompletionError);
                    break;
                }
                PerformanceStats.RecordTcpReceive(BytesTransferred);

                std::vector<Network::FPacket> Packets;
                std::string ParseError;
                bool bParsed = false;
                {
                    std::lock_guard<std::mutex> ReceiveLock(Connection->ReceiveMutex);
                    bParsed = Connection->ReceiveBuffer.Append(
                        Operation->FixedBuffer.data(), BytesTransferred) &&
                        Connection->ReceiveBuffer.ExtractPackets(Packets, ParseError);
                }
                if (!bParsed)
                {
                    if (!ParseError.empty())
                    {
                        Logger.Log("Session " + std::to_string(Connection->SessionId) +
                            " protocol error: " + ParseError);
                    }
                    Disconnect(Connection, ParseError.empty() ? WSAENOBUFS : WSAEPROTONOSUPPORT);
                    break;
                }
                PerformanceStats.RecordTcpPacketsReceived(Packets.size());

                bool bQueueSucceeded = true;
                for (Network::FPacket& Packet : Packets)
                {
                    if (!IncomingEvents.TryPush(
                        {ENetworkEventType::Packet, Connection->SessionId, std::move(Packet), 0}))
                    {
                        PerformanceStats.RecordQueueOverflow();
                        bQueueSucceeded = false;
                        break;
                    }
                }
                if (!bQueueSucceeded)
                {
                    Disconnect(Connection, WSAENOBUFS);
                }
                else
                {
                    PostTcpReceive(Connection);
                }
                break;
            }
            case EIoOperation::TcpSend:
            {
                const auto Connection = Operation->Connection;
                if (!Connection || Connection->bClosed) break;
                if (!Succeeded || BytesTransferred == 0)
                {
                    Disconnect(Connection, Succeeded ? WSAECONNRESET : CompletionError);
                    break;
                }
                PerformanceStats.RecordTcpSend(BytesTransferred);
                {
                    std::lock_guard<std::mutex> SendLock(Connection->SendMutex);
                    Connection->SendBuffer.Consume(BytesTransferred);
                    Connection->bSendInFlight = false;
                }
                StartTcpSend(Connection);
                break;
            }
            case EIoOperation::UdpReceive:
            {
                bool bValidPacket = false;
                if (Succeeded && BytesTransferred > 0)
                {
                    Network::FUdpMoveInput Move;
                    if (Network::DeserializeUdp(
                        Operation->FixedBuffer.data(), BytesTransferred, Move))
                    {
                        bValidPacket = true;
                        FNetworkEvent Event;
                        Event.Type = ENetworkEventType::UdpMove;
                        Event.UdpMove = Move;
                        Event.UdpRemoteAddress = Operation->RemoteAddress;
                        Event.UdpRemoteAddressLength = Operation->RemoteAddressLength;
                        if (!CompletedUdpInputs.TryPush(std::move(Event)))
                        {
                            PerformanceStats.RecordQueueOverflow();
                            SimulatedUdpDroppedPackets.fetch_add(1);
                        }
                    }
                    PerformanceStats.RecordUdpReceive(BytesTransferred, bValidPacket);
                }
                else if (bRunning && CompletionError != WSA_OPERATION_ABORTED &&
                    CompletionError != WSAECONNRESET)
                {
                    Logger.Log("WSARecvFrom completion failed: " + std::to_string(CompletionError));
                }
                if (bRunning) PostUdpReceive();
                break;
            }
            case EIoOperation::UdpSend:
                if (Succeeded && BytesTransferred > 0)
                {
                    PerformanceStats.RecordUdpSend(BytesTransferred);
                }
                if (!Succeeded && bRunning && CompletionError != WSA_OPERATION_ABORTED &&
                    CompletionError != WSAECONNRESET)
                {
                    Logger.Log("WSASendTo completion failed: " + std::to_string(CompletionError));
                }
                break;
            }
        }
    };

    std::vector<std::thread> Workers;
    Workers.reserve(WorkerCount);
    for (std::uint32_t Index = 0; Index < WorkerCount; ++Index)
    {
        Workers.emplace_back(WorkerLoop);
    }
    PerformanceStats.SetNetworkWorkerCount(WorkerCount);

    const std::size_t AcceptOperationCount = std::min<std::size_t>(
        Config.MaxClients, std::max<std::size_t>(4, static_cast<std::size_t>(WorkerCount) * 2));
    const std::size_t UdpReceiveOperationCount = std::clamp<std::size_t>(WorkerCount, 2, 8);
    for (std::size_t Index = 0; Index < AcceptOperationCount; ++Index) PostAccept();
    for (std::size_t Index = 0; Index < UdpReceiveOperationCount; ++Index) PostUdpReceive();

    Logger.Log("IOCP ready  Workers=" + std::to_string(WorkerCount) +
        "  Concurrency=" + std::to_string(WorkerCount) +
        "  PendingAccepts=" + std::to_string(AcceptOperationCount) +
        "  PendingUdpReceives=" + std::to_string(UdpReceiveOperationCount));

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
        PostUdpSend(std::move(Command));
    };

    while (bRunning)
    {
        const auto Now = Clock::now();
        if (PerformanceStats.IsEnabled())
        {
            PerformanceStats.SampleQueueDepths(
                IncomingEvents.Size(), OutgoingCommands.Size(), OutgoingUdpCommands.Size());
        }

        for (FNetworkEvent& Event : CompletedUdpInputs.Drain())
        {
            SubmitIncomingUdp(std::move(Event), Now);
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
            PostUdpSend(std::move(DelayedUdpOutputs.front().Command));
            DelayedUdpOutputs.pop_front();
        }
        for (FUdpSendCommand& Command : OutgoingUdpCommands.Drain())
        {
            SubmitOutgoingUdp(std::move(Command), Now);
        }

        for (FSendCommand& Command : OutgoingCommands.Drain())
        {
            if (Command.bDisconnect)
            {
                Disconnect(FindConnection(Command.SessionId), 0);
                continue;
            }

            auto QueueForConnection = [&](const std::shared_ptr<FIocpConnection>& Connection,
                std::vector<std::uint8_t> Bytes)
            {
                if (!Connection || Connection->bClosed) return;
                bool bQueued = false;
                {
                    std::lock_guard<std::mutex> SendLock(Connection->SendMutex);
                    bQueued = Connection->SendBuffer.Enqueue(std::move(Bytes));
                }
                if (!bQueued)
                {
                    PerformanceStats.RecordQueueOverflow();
                    Disconnect(Connection, WSAENOBUFS);
                    return;
                }
                PerformanceStats.RecordTcpPacketQueued(Command.PacketCount);
                StartTcpSend(Connection);
            };

            if (Command.bBroadcast)
            {
                for (const Network::FSessionId SessionId : Command.BroadcastRecipients)
                {
                    QueueForConnection(FindConnection(SessionId), Command.Bytes);
                }
            }
            else
            {
                QueueForConnection(FindConnection(Command.SessionId), std::move(Command.Bytes));
            }
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

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    {
        std::lock_guard<std::mutex> ListenLock(ListenSocketMutex);
        if (ListenSocket != INVALID_SOCKET)
        {
            closesocket(ListenSocket);
            ListenSocket = INVALID_SOCKET;
        }
    }
    {
        std::lock_guard<std::mutex> UdpLock(UdpSocketMutex);
        if (UdpSocket != INVALID_SOCKET)
        {
            closesocket(UdpSocket);
            UdpSocket = INVALID_SOCKET;
        }
    }

    std::vector<std::shared_ptr<FIocpConnection>> ConnectionsToClose;
    {
        std::lock_guard<std::mutex> Lock(ConnectionsMutex);
        ConnectionsToClose.reserve(Connections.size());
        for (const auto& Pair : Connections) ConnectionsToClose.push_back(Pair.second);
    }
    for (const auto& Connection : ConnectionsToClose) Disconnect(Connection, WSA_OPERATION_ABORTED);

    while (OutstandingIo.load() != 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (std::uint32_t Index = 0; Index < WorkerCount; ++Index)
    {
        PostQueuedCompletionStatus(CompletionPort, 0, 0, nullptr);
    }
    for (std::thread& Worker : Workers)
    {
        if (Worker.joinable()) Worker.join();
    }
    CloseHandle(CompletionPort);
    Logger.Log("IOCP stopped cleanly");
}
