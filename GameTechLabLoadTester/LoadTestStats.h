#pragma once

#include "LoadTestConfig.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

struct FLoadCounterSnapshot
{
    std::uint64_t ConnectionAttempts = 0;
    std::uint64_t Connected = 0;
    std::uint64_t ConnectionFailures = 0;
    std::uint64_t Active = 0;
    std::uint64_t Disconnected = 0;
    std::uint64_t TcpPacketsSent = 0;
    std::uint64_t TcpPacketsReceived = 0;
    std::uint64_t TcpBytesSent = 0;
    std::uint64_t TcpBytesReceived = 0;
    std::uint64_t UdpPacketsSent = 0;
    std::uint64_t UdpPacketsReceived = 0;
    std::uint64_t UdpBytesSent = 0;
    std::uint64_t UdpBytesReceived = 0;
    std::uint64_t UdpEstimatedMissing = 0;
    std::uint64_t UdpOutOfOrder = 0;
    std::uint64_t LocalSendDrops = 0;
    std::uint64_t InvalidPackets = 0;
};

struct FLoadIntervalSnapshot
{
    FLoadCounterSnapshot Counters;
    std::vector<double> ConnectLatencyMs;
    std::vector<double> PingRttMs;
    std::vector<double> MovementAckMs;
};

class FLoadTestStats
{
public:
    void RecordConnectionAttempt();
    void RecordConnected(double HandshakeLatencyMs);
    void RecordConnectionFailure();
    void RecordDisconnected();
    void RecordTcpPacketSent(std::size_t Bytes);
    void RecordTcpPacketReceived();
    void RecordTcpBytesSent(std::size_t Bytes);
    void RecordTcpBytesReceived(std::size_t Bytes);
    void RecordUdpPacketSent(std::size_t Bytes);
    void RecordUdpPacketReceived(std::size_t Bytes);
    void RecordUdpMissing(std::uint64_t Count);
    void RecordUdpOutOfOrder();
    void RecordLocalSendDrop();
    void RecordInvalidPacket();
    void RecordPingRtt(double Milliseconds);
    void RecordMovementAck(double Milliseconds);

    FLoadIntervalSnapshot CaptureInterval();
    FLoadCounterSnapshot CaptureCounters() const;
    void CaptureAllLatency(std::vector<double>& OutConnect, std::vector<double>& OutPing, std::vector<double>& OutMovement) const;

private:
    std::atomic<std::uint64_t> ConnectionAttempts{0};
    std::atomic<std::uint64_t> Connected{0};
    std::atomic<std::uint64_t> ConnectionFailures{0};
    std::atomic<std::uint64_t> Active{0};
    std::atomic<std::uint64_t> Disconnected{0};
    std::atomic<std::uint64_t> TcpPacketsSent{0};
    std::atomic<std::uint64_t> TcpPacketsReceived{0};
    std::atomic<std::uint64_t> TcpBytesSent{0};
    std::atomic<std::uint64_t> TcpBytesReceived{0};
    std::atomic<std::uint64_t> UdpPacketsSent{0};
    std::atomic<std::uint64_t> UdpPacketsReceived{0};
    std::atomic<std::uint64_t> UdpBytesSent{0};
    std::atomic<std::uint64_t> UdpBytesReceived{0};
    std::atomic<std::uint64_t> UdpEstimatedMissing{0};
    std::atomic<std::uint64_t> UdpOutOfOrder{0};
    std::atomic<std::uint64_t> LocalSendDrops{0};
    std::atomic<std::uint64_t> InvalidPackets{0};

    mutable std::mutex LatencyMutex;
    std::vector<double> IntervalConnectLatencyMs;
    std::vector<double> IntervalPingRttMs;
    std::vector<double> IntervalMovementAckMs;
    std::vector<double> AllConnectLatencyMs;
    std::vector<double> AllPingRttMs;
    std::vector<double> AllMovementAckMs;
};

class FLoadTestReporter
{
public:
    FLoadTestReporter(const FLoadTestConfig& InConfig, FLoadTestStats& InStats);

    bool Initialize(std::string& OutError);
    void Report(double ElapsedSeconds);
    void PrintFinal(double ElapsedSeconds);
    const std::string& GetCsvPath() const { return CsvPath; }

private:
    double SampleProcessCpuPercent();

    const FLoadTestConfig& Config;
    FLoadTestStats& Stats;
    FLoadCounterSnapshot PreviousCounters;
    std::chrono::steady_clock::time_point PreviousReportTime;
    std::uint64_t PreviousProcessTime100ns = 0;
    std::chrono::steady_clock::time_point PreviousCpuWallTime;
    unsigned LogicalProcessorCount = 1;
    std::ofstream Csv;
    std::string CsvPath;
    std::uint32_t ReportLine = 0;
};
