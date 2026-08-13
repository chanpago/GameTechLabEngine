#pragma once

#include "ServerConfig.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class FServerLogger;

class FServerPerformanceStats
{
public:
    void Initialize(const FServerConfig& Config, FServerLogger& Logger);
    void SetNetworkWorkerCount(std::uint32_t Count);

    void RecordConnectionAccepted();
    void RecordConnectionClosed();
    void SetActiveConnections(std::uint64_t Count);

    void RecordTcpReceive(std::size_t Bytes);
    void RecordTcpPacketsReceived(std::size_t Count);
    void RecordTcpSend(std::size_t Bytes);
    void RecordTcpPacketQueued(std::size_t Count = 1);
    void RecordUdpReceive(std::size_t Bytes, bool bValidPacket);
    void RecordUdpSend(std::size_t Bytes);

    void RecordIoSubmitted();
    void RecordIoSubmissionFailed();
    void RecordIoCompletion(bool bSucceeded);
    void RecordIoError();
    void RecordPollCycle(int ReadyCount);
    void RecordQueueOverflow();

    void SampleQueueDepths(std::size_t Incoming, std::size_t OutgoingTcp, std::size_t OutgoingUdp);
    void RecordTick(double WorkMilliseconds, double TickBudgetMilliseconds);
    void ReportIfDue(FServerLogger& Logger, std::size_t PlayerCount, bool bForce = false);

    bool IsEnabled() const { return bEnabled; }

private:
    static void UpdateMaximum(std::atomic<std::uint64_t>& Target, std::uint64_t Value);
    static std::uint64_t FileTimeToUInt64(const void* FileTimeValue);
    static std::uint32_t GetProcessThreadCount();
    double SampleProcessCpuPercent(double ElapsedSeconds);
    std::string MakeCsvPath() const;

    bool bEnabled = false;
    bool bCsvEnabled = false;
    EServerNetworkIoMode IoMode = EServerNetworkIoMode::Iocp;
    std::uint32_t LogIntervalSeconds = 1;
    std::uint32_t LogicalProcessorCount = 1;
    std::atomic<std::uint32_t> NetworkWorkerCount{0};
    std::chrono::steady_clock::time_point StartTime{};
    std::chrono::steady_clock::time_point LastReportTime{};
    std::uint64_t PreviousProcessTime100ns = 0;
    std::ofstream CsvFile;
    std::string CsvPath;

    std::vector<double> TickWorkSamples;
    std::uint64_t TickOverruns = 0;

    std::atomic<std::int64_t> ActiveConnections{0};
    std::atomic<std::uint64_t> AcceptedConnections{0};
    std::atomic<std::uint64_t> ClosedConnections{0};

    std::atomic<std::uint64_t> TcpReceiveOperations{0};
    std::atomic<std::uint64_t> TcpReceiveBytes{0};
    std::atomic<std::uint64_t> TcpReceivePackets{0};
    std::atomic<std::uint64_t> TcpSendOperations{0};
    std::atomic<std::uint64_t> TcpSendBytes{0};
    std::atomic<std::uint64_t> TcpQueuedPackets{0};
    std::atomic<std::uint64_t> UdpReceiveOperations{0};
    std::atomic<std::uint64_t> UdpReceiveBytes{0};
    std::atomic<std::uint64_t> UdpValidPackets{0};
    std::atomic<std::uint64_t> UdpSendOperations{0};
    std::atomic<std::uint64_t> UdpSendBytes{0};

    std::atomic<std::uint64_t> PendingIo{0};
    std::atomic<std::uint64_t> IoCompletions{0};
    std::atomic<std::uint64_t> IoErrors{0};
    std::atomic<std::uint64_t> PollCycles{0};
    std::atomic<std::uint64_t> PollReadyEvents{0};
    std::atomic<std::uint64_t> QueueOverflows{0};

    std::atomic<std::uint64_t> MaxIncomingQueueDepth{0};
    std::atomic<std::uint64_t> MaxOutgoingTcpQueueDepth{0};
    std::atomic<std::uint64_t> MaxOutgoingUdpQueueDepth{0};
};
