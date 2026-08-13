#include "LoadTestStats.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
    struct FPercentiles
    {
        double P50 = 0.0;
        double P95 = 0.0;
        double P99 = 0.0;
        double Max = 0.0;
        std::size_t Count = 0;
    };

    FPercentiles CalculatePercentiles(std::vector<double> Values)
    {
        FPercentiles Result;
        Result.Count = Values.size();
        if (Values.empty()) return Result;
        std::sort(Values.begin(), Values.end());
        const auto Pick = [&Values](double Quantile)
        {
            const double Position = Quantile * static_cast<double>(Values.size() - 1);
            const std::size_t Low = static_cast<std::size_t>(Position);
            const std::size_t High = std::min(Low + 1, Values.size() - 1);
            const double Fraction = Position - static_cast<double>(Low);
            return Values[Low] + (Values[High] - Values[Low]) * Fraction;
        };
        Result.P50 = Pick(0.50);
        Result.P95 = Pick(0.95);
        Result.P99 = Pick(0.99);
        Result.Max = Values.back();
        return Result;
    }

    std::uint64_t FileTimeToUInt64(const FILETIME& Value)
    {
        ULARGE_INTEGER Combined{};
        Combined.LowPart = Value.dwLowDateTime;
        Combined.HighPart = Value.dwHighDateTime;
        return Combined.QuadPart;
    }

    std::uint64_t Difference(std::uint64_t Current, std::uint64_t Previous)
    {
        return Current >= Previous ? Current - Previous : 0;
    }

    std::string DefaultCsvPath(ELoadTestScenario Scenario)
    {
        SYSTEMTIME Time{};
        GetLocalTime(&Time);
        std::ostringstream Name;
        Name << "Logs/LoadTest_"
             << std::setfill('0') << std::setw(4) << Time.wYear
             << std::setw(2) << Time.wMonth << std::setw(2) << Time.wDay << '_'
             << std::setw(2) << Time.wHour << std::setw(2) << Time.wMinute << std::setw(2) << Time.wSecond
             << '_' << ToString(Scenario) << ".csv";
        return Name.str();
    }
}

void FLoadTestStats::RecordConnectionAttempt() { ConnectionAttempts.fetch_add(1, std::memory_order_relaxed); }
void FLoadTestStats::RecordConnected(double Value)
{
    Connected.fetch_add(1, std::memory_order_relaxed);
    Active.fetch_add(1, std::memory_order_relaxed);
    std::scoped_lock Lock(LatencyMutex);
    IntervalConnectLatencyMs.push_back(Value);
    AllConnectLatencyMs.push_back(Value);
}
void FLoadTestStats::RecordConnectionFailure() { ConnectionFailures.fetch_add(1, std::memory_order_relaxed); }
void FLoadTestStats::RecordDisconnected()
{
    Active.fetch_sub(1, std::memory_order_relaxed);
    Disconnected.fetch_add(1, std::memory_order_relaxed);
}
void FLoadTestStats::RecordTcpPacketSent(std::size_t) { TcpPacketsSent.fetch_add(1, std::memory_order_relaxed); }
void FLoadTestStats::RecordTcpPacketReceived() { TcpPacketsReceived.fetch_add(1, std::memory_order_relaxed); }
void FLoadTestStats::RecordTcpBytesSent(std::size_t Value) { TcpBytesSent.fetch_add(Value, std::memory_order_relaxed); }
void FLoadTestStats::RecordTcpBytesReceived(std::size_t Value) { TcpBytesReceived.fetch_add(Value, std::memory_order_relaxed); }
void FLoadTestStats::RecordUdpPacketSent(std::size_t Value)
{
    UdpPacketsSent.fetch_add(1, std::memory_order_relaxed);
    UdpBytesSent.fetch_add(Value, std::memory_order_relaxed);
}
void FLoadTestStats::RecordUdpPacketReceived(std::size_t Value)
{
    UdpPacketsReceived.fetch_add(1, std::memory_order_relaxed);
    UdpBytesReceived.fetch_add(Value, std::memory_order_relaxed);
}
void FLoadTestStats::RecordUdpMissing(std::uint64_t Value) { UdpEstimatedMissing.fetch_add(Value, std::memory_order_relaxed); }
void FLoadTestStats::RecordUdpOutOfOrder() { UdpOutOfOrder.fetch_add(1, std::memory_order_relaxed); }
void FLoadTestStats::RecordLocalSendDrop() { LocalSendDrops.fetch_add(1, std::memory_order_relaxed); }
void FLoadTestStats::RecordInvalidPacket() { InvalidPackets.fetch_add(1, std::memory_order_relaxed); }
void FLoadTestStats::RecordPingRtt(double Value)
{
    std::scoped_lock Lock(LatencyMutex);
    IntervalPingRttMs.push_back(Value);
    AllPingRttMs.push_back(Value);
}
void FLoadTestStats::RecordMovementAck(double Value)
{
    std::scoped_lock Lock(LatencyMutex);
    IntervalMovementAckMs.push_back(Value);
    AllMovementAckMs.push_back(Value);
}

FLoadCounterSnapshot FLoadTestStats::CaptureCounters() const
{
    FLoadCounterSnapshot Result;
    Result.ConnectionAttempts = ConnectionAttempts.load(std::memory_order_relaxed);
    Result.Connected = Connected.load(std::memory_order_relaxed);
    Result.ConnectionFailures = ConnectionFailures.load(std::memory_order_relaxed);
    Result.Active = Active.load(std::memory_order_relaxed);
    Result.Disconnected = Disconnected.load(std::memory_order_relaxed);
    Result.TcpPacketsSent = TcpPacketsSent.load(std::memory_order_relaxed);
    Result.TcpPacketsReceived = TcpPacketsReceived.load(std::memory_order_relaxed);
    Result.TcpBytesSent = TcpBytesSent.load(std::memory_order_relaxed);
    Result.TcpBytesReceived = TcpBytesReceived.load(std::memory_order_relaxed);
    Result.UdpPacketsSent = UdpPacketsSent.load(std::memory_order_relaxed);
    Result.UdpPacketsReceived = UdpPacketsReceived.load(std::memory_order_relaxed);
    Result.UdpBytesSent = UdpBytesSent.load(std::memory_order_relaxed);
    Result.UdpBytesReceived = UdpBytesReceived.load(std::memory_order_relaxed);
    Result.UdpEstimatedMissing = UdpEstimatedMissing.load(std::memory_order_relaxed);
    Result.UdpOutOfOrder = UdpOutOfOrder.load(std::memory_order_relaxed);
    Result.LocalSendDrops = LocalSendDrops.load(std::memory_order_relaxed);
    Result.InvalidPackets = InvalidPackets.load(std::memory_order_relaxed);
    return Result;
}

FLoadIntervalSnapshot FLoadTestStats::CaptureInterval()
{
    FLoadIntervalSnapshot Result;
    Result.Counters = CaptureCounters();
    std::scoped_lock Lock(LatencyMutex);
    Result.ConnectLatencyMs.swap(IntervalConnectLatencyMs);
    Result.PingRttMs.swap(IntervalPingRttMs);
    Result.MovementAckMs.swap(IntervalMovementAckMs);
    return Result;
}

void FLoadTestStats::CaptureAllLatency(std::vector<double>& OutConnect, std::vector<double>& OutPing, std::vector<double>& OutMovement) const
{
    std::scoped_lock Lock(LatencyMutex);
    OutConnect = AllConnectLatencyMs;
    OutPing = AllPingRttMs;
    OutMovement = AllMovementAckMs;
}

FLoadTestReporter::FLoadTestReporter(const FLoadTestConfig& InConfig, FLoadTestStats& InStats)
    : Config(InConfig), Stats(InStats), PreviousReportTime(std::chrono::steady_clock::now()), PreviousCpuWallTime(PreviousReportTime)
{
    SYSTEM_INFO Info{};
    GetSystemInfo(&Info);
    LogicalProcessorCount = std::max(1u, static_cast<unsigned>(Info.dwNumberOfProcessors));
}

bool FLoadTestReporter::Initialize(std::string& OutError)
{
    SampleProcessCpuPercent();
    PreviousCounters = Stats.CaptureCounters();
    PreviousReportTime = std::chrono::steady_clock::now();
    if (!Config.bCsvEnabled) return true;

    CsvPath = Config.CsvPath.empty() ? DefaultCsvPath(Config.Scenario) : Config.CsvPath;
    const std::filesystem::path Path(CsvPath);
    std::error_code Error;
    if (Path.has_parent_path()) std::filesystem::create_directories(Path.parent_path(), Error);
    if (Error)
    {
        OutError = "could not create CSV directory: " + Error.message();
        return false;
    }
    Csv.open(Path, std::ios::out | std::ios::trunc);
    if (!Csv)
    {
        OutError = "could not open CSV file: " + CsvPath;
        return false;
    }
    Csv << "elapsed_s,scenario,target_clients,attempted,connected,failed,incomplete,active,disconnected,"
           "tcp_tx_pps,tcp_rx_pps,tcp_tx_kib_s,tcp_rx_kib_s,udp_tx_pps,udp_rx_pps,udp_tx_kib_s,udp_rx_kib_s,"
           "connect_p50_ms,connect_p95_ms,ping_p50_ms,ping_p95_ms,ping_p99_ms,move_ack_p50_ms,move_ack_p95_ms,move_ack_p99_ms,"
           "udp_estimated_missing,udp_out_of_order,local_send_drops,invalid_packets,generator_cpu_pct\n";
    return true;
}

void FLoadTestReporter::Report(double ElapsedSeconds)
{
    const auto Now = std::chrono::steady_clock::now();
    const double IntervalSeconds = std::max(0.001, std::chrono::duration<double>(Now - PreviousReportTime).count());
    FLoadIntervalSnapshot Snapshot = Stats.CaptureInterval();
    const FPercentiles Connect = CalculatePercentiles(std::move(Snapshot.ConnectLatencyMs));
    const FPercentiles Ping = CalculatePercentiles(std::move(Snapshot.PingRttMs));
    const FPercentiles Move = CalculatePercentiles(std::move(Snapshot.MovementAckMs));
    const auto Rate = [IntervalSeconds](std::uint64_t Current, std::uint64_t Previous)
    { return static_cast<double>(Difference(Current, Previous)) / IntervalSeconds; };
    const double TcpTxPps = Rate(Snapshot.Counters.TcpPacketsSent, PreviousCounters.TcpPacketsSent);
    const double TcpRxPps = Rate(Snapshot.Counters.TcpPacketsReceived, PreviousCounters.TcpPacketsReceived);
    const double TcpTxKib = Rate(Snapshot.Counters.TcpBytesSent, PreviousCounters.TcpBytesSent) / 1024.0;
    const double TcpRxKib = Rate(Snapshot.Counters.TcpBytesReceived, PreviousCounters.TcpBytesReceived) / 1024.0;
    const double UdpTxPps = Rate(Snapshot.Counters.UdpPacketsSent, PreviousCounters.UdpPacketsSent);
    const double UdpRxPps = Rate(Snapshot.Counters.UdpPacketsReceived, PreviousCounters.UdpPacketsReceived);
    const double UdpTxKib = Rate(Snapshot.Counters.UdpBytesSent, PreviousCounters.UdpBytesSent) / 1024.0;
    const double UdpRxKib = Rate(Snapshot.Counters.UdpBytesReceived, PreviousCounters.UdpBytesReceived) / 1024.0;
    const double CpuPercent = SampleProcessCpuPercent();

    if (ReportLine++ % 20 == 0)
    {
        std::cout << "\n sec  active  ok/fail   TCP tx/rx pps   UDP tx/rx pps   p95 connect/ping/move ms   gaps  CPU\n";
    }
    std::cout << std::fixed << std::setprecision(1)
              << std::setw(4) << ElapsedSeconds << "  "
              << std::setw(6) << Snapshot.Counters.Active << "  "
              << Snapshot.Counters.Connected << '/' << Snapshot.Counters.ConnectionFailures << "      "
              << static_cast<std::uint64_t>(TcpTxPps) << '/' << static_cast<std::uint64_t>(TcpRxPps) << "          "
              << static_cast<std::uint64_t>(UdpTxPps) << '/' << static_cast<std::uint64_t>(UdpRxPps) << "          "
              << Connect.P95 << '/' << Ping.P95 << '/' << Move.P95 << "                 "
              << Snapshot.Counters.UdpEstimatedMissing << "  " << CpuPercent << "%\n";

    if (Csv)
    {
        const std::uint64_t CompletedConnections = Snapshot.Counters.Connected + Snapshot.Counters.ConnectionFailures;
        const std::uint64_t IncompleteConnections = Snapshot.Counters.ConnectionAttempts > CompletedConnections
            ? Snapshot.Counters.ConnectionAttempts - CompletedConnections : 0;
        Csv << std::fixed << std::setprecision(3)
            << ElapsedSeconds << ',' << ToString(Config.Scenario) << ',' << Config.ClientCount << ','
            << Snapshot.Counters.ConnectionAttempts << ',' << Snapshot.Counters.Connected << ',' << Snapshot.Counters.ConnectionFailures << ','
            << IncompleteConnections << ',' << Snapshot.Counters.Active << ',' << Snapshot.Counters.Disconnected << ','
            << TcpTxPps << ',' << TcpRxPps << ',' << TcpTxKib << ',' << TcpRxKib << ','
            << UdpTxPps << ',' << UdpRxPps << ',' << UdpTxKib << ',' << UdpRxKib << ','
            << Connect.P50 << ',' << Connect.P95 << ',' << Ping.P50 << ',' << Ping.P95 << ',' << Ping.P99 << ','
            << Move.P50 << ',' << Move.P95 << ',' << Move.P99 << ','
            << Snapshot.Counters.UdpEstimatedMissing << ',' << Snapshot.Counters.UdpOutOfOrder << ','
            << Snapshot.Counters.LocalSendDrops << ',' << Snapshot.Counters.InvalidPackets << ',' << CpuPercent << '\n';
        Csv.flush();
    }

    PreviousCounters = Snapshot.Counters;
    PreviousReportTime = Now;
}

void FLoadTestReporter::PrintFinal(double ElapsedSeconds)
{
    const FLoadCounterSnapshot Counters = Stats.CaptureCounters();
    std::vector<double> ConnectValues;
    std::vector<double> PingValues;
    std::vector<double> MoveValues;
    Stats.CaptureAllLatency(ConnectValues, PingValues, MoveValues);
    const FPercentiles Connect = CalculatePercentiles(std::move(ConnectValues));
    const FPercentiles Ping = CalculatePercentiles(std::move(PingValues));
    const FPercentiles Move = CalculatePercentiles(std::move(MoveValues));
    const std::uint64_t CompletedConnections = Counters.Connected + Counters.ConnectionFailures;
    const std::uint64_t IncompleteConnections = Counters.ConnectionAttempts > CompletedConnections
        ? Counters.ConnectionAttempts - CompletedConnections : 0;

    std::cout << "\n=== Load test complete ===\n"
              << "Scenario: " << ToString(Config.Scenario) << ", elapsed: " << std::fixed << std::setprecision(2) << ElapsedSeconds << " sec\n"
              << "Connections: attempted=" << Counters.ConnectionAttempts << ", connected=" << Counters.Connected
              << ", failed=" << Counters.ConnectionFailures << ", incomplete=" << IncompleteConnections
              << ", unexpected_disconnects=" << Counters.Disconnected << "\n"
              << "Handshake ms: samples=" << Connect.Count << ", p50=" << Connect.P50 << ", p95=" << Connect.P95 << ", p99=" << Connect.P99 << ", max=" << Connect.Max << "\n"
              << "Ping RTT ms: samples=" << Ping.Count << ", p50=" << Ping.P50 << ", p95=" << Ping.P95 << ", p99=" << Ping.P99 << ", max=" << Ping.Max << "\n"
              << "Movement ACK ms: samples=" << Move.Count << ", p50=" << Move.P50 << ", p95=" << Move.P95 << ", p99=" << Move.P99 << ", max=" << Move.Max << "\n"
              << "UDP state: received=" << Counters.UdpPacketsReceived << ", estimated_missing=" << Counters.UdpEstimatedMissing
              << ", out_of_order=" << Counters.UdpOutOfOrder << ", local_send_drops=" << Counters.LocalSendDrops << "\n"
              << "Invalid packets: " << Counters.InvalidPackets << '\n';
    if (!CsvPath.empty()) std::cout << "CSV: " << std::filesystem::absolute(CsvPath).string() << '\n';
}

double FLoadTestReporter::SampleProcessCpuPercent()
{
    FILETIME Creation{}, Exit{}, Kernel{}, User{};
    if (!GetProcessTimes(GetCurrentProcess(), &Creation, &Exit, &Kernel, &User)) return 0.0;
    const std::uint64_t ProcessTime = FileTimeToUInt64(Kernel) + FileTimeToUInt64(User);
    const auto Now = std::chrono::steady_clock::now();
    if (PreviousProcessTime100ns == 0)
    {
        PreviousProcessTime100ns = ProcessTime;
        PreviousCpuWallTime = Now;
        return 0.0;
    }
    const double WallSeconds = std::chrono::duration<double>(Now - PreviousCpuWallTime).count();
    const double CpuSeconds = static_cast<double>(Difference(ProcessTime, PreviousProcessTime100ns)) / 10000000.0;
    PreviousProcessTime100ns = ProcessTime;
    PreviousCpuWallTime = Now;
    return WallSeconds > 0.0 ? 100.0 * CpuSeconds / (WallSeconds * static_cast<double>(LogicalProcessorCount)) : 0.0;
}
