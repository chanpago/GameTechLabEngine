#include "ServerPerformanceStats.h"

#include "ServerLogger.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace
{
    std::tm LocalTime(std::time_t Time)
    {
        std::tm Result{};
        localtime_s(&Result, &Time);
        return Result;
    }

    double Percentile(const std::vector<double>& SortedValues, double Fraction)
    {
        if (SortedValues.empty()) return 0.0;
        const std::size_t Index = std::min(
            SortedValues.size() - 1,
            static_cast<std::size_t>(std::ceil(Fraction * static_cast<double>(SortedValues.size()))) - 1);
        return SortedValues[Index];
    }
}

void FServerPerformanceStats::Initialize(const FServerConfig& Config, FServerLogger& Logger)
{
    bEnabled = Config.bPerformanceStatsEnabled;
    bCsvEnabled = Config.bPerformanceCsvEnabled;
    IoMode = Config.NetworkIoMode;
    LogIntervalSeconds = std::max<std::uint32_t>(1, Config.PerformanceLogIntervalSeconds);
    NetworkWorkerCount = IoMode == EServerNetworkIoMode::WsaPoll ? 1u : Config.IocpWorkerThreads;

    SYSTEM_INFO SystemInfo{};
    GetSystemInfo(&SystemInfo);
    LogicalProcessorCount = std::max<std::uint32_t>(1, SystemInfo.dwNumberOfProcessors);
    StartTime = std::chrono::steady_clock::now();
    LastReportTime = StartTime;

    FILETIME Creation{}, Exit{}, Kernel{}, User{};
    if (GetProcessTimes(GetCurrentProcess(), &Creation, &Exit, &Kernel, &User))
    {
        PreviousProcessTime100ns = FileTimeToUInt64(&Kernel) + FileTimeToUInt64(&User);
    }

    if (!bEnabled) return;

    TickWorkSamples.reserve(static_cast<std::size_t>(Config.TickRate) * LogIntervalSeconds + 8);
    if (bCsvEnabled)
    {
        std::filesystem::create_directories("Logs");
        CsvPath = MakeCsvPath();
        CsvFile.open(CsvPath, std::ios::out | std::ios::trunc);
        if (CsvFile.is_open())
        {
            CsvFile << "elapsed_s,io_mode,workers,active_connections,players,accepted_total,closed_total,"
                "cpu_percent,process_threads,tick_samples,tick_avg_ms,tick_p95_ms,tick_p99_ms,tick_max_ms,"
                "tick_overruns,tick_overrun_percent,tcp_rx_ops_s,tcp_rx_packets_s,tcp_rx_kib_s,"
                "tcp_tx_ops_s,tcp_tx_packets_s,tcp_tx_kib_s,udp_rx_ops_s,udp_rx_valid_packets_s,"
                "udp_rx_kib_s,udp_tx_ops_s,udp_tx_kib_s,iocp_completions_s,io_errors,"
                "poll_calls_s,poll_ready_events_s,in_queue_max,tcp_out_queue_max,udp_out_queue_max,"
                "queue_overflows,pending_io\n";
            CsvFile.flush();
        }
    }

    Logger.Log("Performance stats enabled  Interval=" + std::to_string(LogIntervalSeconds) +
        "s  CSV=" + (CsvFile.is_open() ? CsvPath : "Off"));
}

void FServerPerformanceStats::SetNetworkWorkerCount(std::uint32_t Count)
{
    NetworkWorkerCount = Count;
}

void FServerPerformanceStats::RecordConnectionAccepted()
{
    if (!bEnabled) return;
    ActiveConnections.fetch_add(1);
    AcceptedConnections.fetch_add(1);
}

void FServerPerformanceStats::RecordConnectionClosed()
{
    if (!bEnabled) return;
    std::int64_t Current = ActiveConnections.load();
    while (Current > 0 && !ActiveConnections.compare_exchange_weak(Current, Current - 1)) {}
    ClosedConnections.fetch_add(1);
}

void FServerPerformanceStats::SetActiveConnections(std::uint64_t Count)
{
    if (!bEnabled) return;
    ActiveConnections = static_cast<std::int64_t>(Count);
}

void FServerPerformanceStats::RecordTcpReceive(std::size_t Bytes)
{
    if (!bEnabled) return;
    TcpReceiveOperations.fetch_add(1);
    TcpReceiveBytes.fetch_add(Bytes);
}

void FServerPerformanceStats::RecordTcpPacketsReceived(std::size_t Count)
{
    if (!bEnabled) return;
    TcpReceivePackets.fetch_add(Count);
}

void FServerPerformanceStats::RecordTcpSend(std::size_t Bytes)
{
    if (!bEnabled) return;
    TcpSendOperations.fetch_add(1);
    TcpSendBytes.fetch_add(Bytes);
}

void FServerPerformanceStats::RecordTcpPacketQueued(std::size_t Count)
{
    if (!bEnabled) return;
    TcpQueuedPackets.fetch_add(Count);
}

void FServerPerformanceStats::RecordUdpReceive(std::size_t Bytes, bool bValidPacket)
{
    if (!bEnabled) return;
    UdpReceiveOperations.fetch_add(1);
    UdpReceiveBytes.fetch_add(Bytes);
    if (bValidPacket) UdpValidPackets.fetch_add(1);
}

void FServerPerformanceStats::RecordUdpSend(std::size_t Bytes)
{
    if (!bEnabled) return;
    UdpSendOperations.fetch_add(1);
    UdpSendBytes.fetch_add(Bytes);
}

void FServerPerformanceStats::RecordIoSubmitted()
{
    if (!bEnabled) return;
    PendingIo.fetch_add(1);
}

void FServerPerformanceStats::RecordIoSubmissionFailed()
{
    if (!bEnabled) return;
    std::uint64_t Current = PendingIo.load();
    while (Current > 0 && !PendingIo.compare_exchange_weak(Current, Current - 1)) {}
    IoErrors.fetch_add(1);
}

void FServerPerformanceStats::RecordIoCompletion(bool bSucceeded)
{
    if (!bEnabled) return;
    std::uint64_t Current = PendingIo.load();
    while (Current > 0 && !PendingIo.compare_exchange_weak(Current, Current - 1)) {}
    IoCompletions.fetch_add(1);
    if (!bSucceeded) IoErrors.fetch_add(1);
}

void FServerPerformanceStats::RecordIoError()
{
    if (!bEnabled) return;
    IoErrors.fetch_add(1);
}

void FServerPerformanceStats::RecordPollCycle(int ReadyCount)
{
    if (!bEnabled) return;
    PollCycles.fetch_add(1);
    if (ReadyCount > 0) PollReadyEvents.fetch_add(static_cast<std::uint64_t>(ReadyCount));
}

void FServerPerformanceStats::RecordQueueOverflow()
{
    if (!bEnabled) return;
    QueueOverflows.fetch_add(1);
}

void FServerPerformanceStats::SampleQueueDepths(
    std::size_t Incoming, std::size_t OutgoingTcp, std::size_t OutgoingUdp)
{
    if (!bEnabled) return;
    UpdateMaximum(MaxIncomingQueueDepth, Incoming);
    UpdateMaximum(MaxOutgoingTcpQueueDepth, OutgoingTcp);
    UpdateMaximum(MaxOutgoingUdpQueueDepth, OutgoingUdp);
}

void FServerPerformanceStats::RecordTick(double WorkMilliseconds, double TickBudgetMilliseconds)
{
    if (!bEnabled) return;
    TickWorkSamples.push_back(WorkMilliseconds);
    if (WorkMilliseconds > TickBudgetMilliseconds) ++TickOverruns;
}

void FServerPerformanceStats::ReportIfDue(FServerLogger& Logger, std::size_t PlayerCount, bool bForce)
{
    if (!bEnabled) return;

    const auto Now = std::chrono::steady_clock::now();
    const double IntervalSeconds = std::chrono::duration<double>(Now - LastReportTime).count();
    if (!bForce && IntervalSeconds < static_cast<double>(LogIntervalSeconds)) return;
    if (IntervalSeconds <= 0.000001 || TickWorkSamples.empty()) return;

    std::vector<double> SortedTicks = TickWorkSamples;
    std::sort(SortedTicks.begin(), SortedTicks.end());
    const double TickAverage = std::accumulate(
        SortedTicks.begin(), SortedTicks.end(), 0.0) / static_cast<double>(SortedTicks.size());
    const double TickP95 = Percentile(SortedTicks, 0.95);
    const double TickP99 = Percentile(SortedTicks, 0.99);
    const double TickMaximum = SortedTicks.back();
    const double TickOverrunPercent = 100.0 * static_cast<double>(TickOverruns) /
        static_cast<double>(SortedTicks.size());
    const double CpuPercent = SampleProcessCpuPercent(IntervalSeconds);
    const std::uint32_t ThreadCount = GetProcessThreadCount();

    const auto Take = [](std::atomic<std::uint64_t>& Counter) { return Counter.exchange(0); };
    const std::uint64_t TcpRxOps = Take(TcpReceiveOperations);
    const std::uint64_t TcpRxBytes = Take(TcpReceiveBytes);
    const std::uint64_t TcpRxPackets = Take(TcpReceivePackets);
    const std::uint64_t TcpTxOps = Take(TcpSendOperations);
    const std::uint64_t TcpTxBytes = Take(TcpSendBytes);
    const std::uint64_t TcpTxPackets = Take(TcpQueuedPackets);
    const std::uint64_t UdpRxOps = Take(UdpReceiveOperations);
    const std::uint64_t UdpRxBytes = Take(UdpReceiveBytes);
    const std::uint64_t UdpRxPackets = Take(UdpValidPackets);
    const std::uint64_t UdpTxOps = Take(UdpSendOperations);
    const std::uint64_t UdpTxBytes = Take(UdpSendBytes);
    const std::uint64_t Completions = Take(IoCompletions);
    const std::uint64_t Errors = Take(IoErrors);
    const std::uint64_t PollCallCount = Take(PollCycles);
    const std::uint64_t PollReadyCount = Take(PollReadyEvents);
    const std::uint64_t OverflowCount = Take(QueueOverflows);
    const std::uint64_t IncomingQueueMax = Take(MaxIncomingQueueDepth);
    const std::uint64_t TcpQueueMax = Take(MaxOutgoingTcpQueueDepth);
    const std::uint64_t UdpQueueMax = Take(MaxOutgoingUdpQueueDepth);
    const auto Rate = [IntervalSeconds](std::uint64_t Value)
    {
        return static_cast<double>(Value) / IntervalSeconds;
    };
    const auto KiBRate = [&](std::uint64_t Bytes) { return Rate(Bytes) / 1024.0; };

    std::ostringstream Message;
    Message << std::fixed << std::setprecision(2)
        << "Perf I/O=" << ToString(IoMode)
        << " Workers=" << NetworkWorkerCount.load()
        << " Clients=" << std::max<std::int64_t>(0, ActiveConnections.load())
        << " Players=" << PlayerCount
        << " CPU=" << CpuPercent << "%"
        << " Threads=" << ThreadCount
        << " Tick(ms) avg/p95/p99/max=" << TickAverage << '/' << TickP95 << '/' << TickP99 << '/' << TickMaximum
        << " Overrun=" << TickOverruns << '/' << SortedTicks.size()
        << " TCP(pkt/s rx/tx)=" << Rate(TcpRxPackets) << '/' << Rate(TcpTxPackets)
        << " UDP(pkt/s rx/tx)=" << Rate(UdpRxPackets) << '/' << Rate(UdpTxOps)
        << " Net(KiB/s rx/tx)=" << KiBRate(TcpRxBytes + UdpRxBytes) << '/' << KiBRate(TcpTxBytes + UdpTxBytes)
        << " QueueMax(in/tcp/udp)=" << IncomingQueueMax << '/' << TcpQueueMax << '/' << UdpQueueMax;
    if (IoMode == EServerNetworkIoMode::Iocp)
    {
        Message << " IOCP(comp/s,pending)=" << Rate(Completions) << ',' << PendingIo.load();
    }
    else
    {
        Message << " Poll(calls/s,ready/s)=" << Rate(PollCallCount) << ',' << Rate(PollReadyCount);
    }
    Message << " Errors=" << Errors << " QueueDrops=" << OverflowCount;
    Logger.Log(Message.str());

    if (CsvFile.is_open())
    {
        const double ElapsedSeconds = std::chrono::duration<double>(Now - StartTime).count();
        CsvFile << std::fixed << std::setprecision(3)
            << ElapsedSeconds << ',' << ToString(IoMode) << ',' << NetworkWorkerCount.load() << ','
            << std::max<std::int64_t>(0, ActiveConnections.load()) << ',' << PlayerCount << ','
            << AcceptedConnections.load() << ',' << ClosedConnections.load() << ','
            << CpuPercent << ',' << ThreadCount << ',' << SortedTicks.size() << ','
            << TickAverage << ',' << TickP95 << ',' << TickP99 << ',' << TickMaximum << ','
            << TickOverruns << ',' << TickOverrunPercent << ','
            << Rate(TcpRxOps) << ',' << Rate(TcpRxPackets) << ',' << KiBRate(TcpRxBytes) << ','
            << Rate(TcpTxOps) << ',' << Rate(TcpTxPackets) << ',' << KiBRate(TcpTxBytes) << ','
            << Rate(UdpRxOps) << ',' << Rate(UdpRxPackets) << ',' << KiBRate(UdpRxBytes) << ','
            << Rate(UdpTxOps) << ',' << KiBRate(UdpTxBytes) << ',' << Rate(Completions) << ',' << Errors << ','
            << Rate(PollCallCount) << ',' << Rate(PollReadyCount) << ','
            << IncomingQueueMax << ',' << TcpQueueMax << ',' << UdpQueueMax << ','
            << OverflowCount << ',' << PendingIo.load() << '\n';
        CsvFile.flush();
    }

    TickWorkSamples.clear();
    TickOverruns = 0;
    LastReportTime = Now;
}

void FServerPerformanceStats::UpdateMaximum(std::atomic<std::uint64_t>& Target, std::uint64_t Value)
{
    std::uint64_t Current = Target.load();
    while (Current < Value && !Target.compare_exchange_weak(Current, Value)) {}
}

std::uint64_t FServerPerformanceStats::FileTimeToUInt64(const void* FileTimeValue)
{
    const auto* Value = static_cast<const FILETIME*>(FileTimeValue);
    ULARGE_INTEGER Result{};
    Result.LowPart = Value->dwLowDateTime;
    Result.HighPart = Value->dwHighDateTime;
    return Result.QuadPart;
}

std::uint32_t FServerPerformanceStats::GetProcessThreadCount()
{
    const DWORD ProcessId = GetCurrentProcessId();
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (Snapshot == INVALID_HANDLE_VALUE) return 0;

    std::uint32_t Count = 0;
    THREADENTRY32 Entry{};
    Entry.dwSize = sizeof(Entry);
    if (Thread32First(Snapshot, &Entry))
    {
        do
        {
            if (Entry.th32OwnerProcessID == ProcessId) ++Count;
        }
        while (Thread32Next(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Count;
}

double FServerPerformanceStats::SampleProcessCpuPercent(double ElapsedSeconds)
{
    FILETIME Creation{}, Exit{}, Kernel{}, User{};
    if (!GetProcessTimes(GetCurrentProcess(), &Creation, &Exit, &Kernel, &User)) return 0.0;

    const std::uint64_t Current = FileTimeToUInt64(&Kernel) + FileTimeToUInt64(&User);
    const std::uint64_t Delta = Current >= PreviousProcessTime100ns ? Current - PreviousProcessTime100ns : 0;
    PreviousProcessTime100ns = Current;
    return 100.0 * static_cast<double>(Delta) /
        (ElapsedSeconds * 10000000.0 * static_cast<double>(LogicalProcessorCount));
}

std::string FServerPerformanceStats::MakeCsvPath() const
{
    const auto Now = std::chrono::system_clock::now();
    const std::time_t Time = std::chrono::system_clock::to_time_t(Now);
    const std::tm Local = LocalTime(Time);
    std::ostringstream Name;
    Name << "Logs/ServerPerformance_" << std::put_time(&Local, "%Y%m%d_%H%M%S")
         << '_' << ToString(IoMode) << ".csv";
    return Name.str();
}
