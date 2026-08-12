#include "ServerLogger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
    std::tm LocalTime(std::time_t Time)
    {
        std::tm Result{};
        localtime_s(&Result, &Time);
        return Result;
    }
}

FServerLogger::FServerLogger()
{
    std::filesystem::create_directories("Logs");
    const auto Now = std::chrono::system_clock::now();
    const std::time_t Time = std::chrono::system_clock::to_time_t(Now);
    const std::tm Local = LocalTime(Time);
    std::ostringstream Name;
    Name << "Logs/Server_" << std::put_time(&Local, "%Y%m%d_%H%M%S") << ".log";
    File.open(Name.str(), std::ios::out | std::ios::app);
}

void FServerLogger::Log(const std::string& Message)
{
    const auto Now = std::chrono::system_clock::now();
    const std::time_t Time = std::chrono::system_clock::to_time_t(Now);
    const std::tm Local = LocalTime(Time);
    std::ostringstream Line;
    Line << '[' << std::put_time(&Local, "%H:%M:%S") << "] [Network] " << Message;

    std::lock_guard<std::mutex> Lock(Mutex);
    std::cout << Line.str() << std::endl;
    if (File.is_open())
    {
        File << Line.str() << std::endl;
    }
}
