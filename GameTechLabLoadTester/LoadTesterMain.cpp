#include "LoadTestConfig.h"
#include "LoadTestRunner.h"

#include <WinSock2.h>
#include <Windows.h>

#include <atomic>
#include <iostream>
#include <utility>

namespace
{
    std::atomic<bool> GStopRequested{false};

    BOOL WINAPI ConsoleHandler(DWORD Event)
    {
        if (Event == CTRL_C_EVENT || Event == CTRL_BREAK_EVENT || Event == CTRL_CLOSE_EVENT || Event == CTRL_SHUTDOWN_EVENT)
        {
            GStopRequested.store(true, std::memory_order_relaxed);
            return TRUE;
        }
        return FALSE;
    }
}

int main(int ArgC, char** ArgV)
{
    FLoadTestConfig Config;
    bool bHelpRequested = false;
    std::string Error;
    if (!ParseLoadTestConfig(ArgC, ArgV, Config, bHelpRequested, Error))
    {
        std::cerr << "Argument error: " << Error << "\n\n";
        PrintLoadTestUsage();
        return 2;
    }
    if (bHelpRequested)
    {
        PrintLoadTestUsage();
        return 0;
    }

    WSADATA WsaData{};
    const int StartupResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (StartupResult != 0)
    {
        std::cerr << "WSAStartup failed: " << StartupResult << '\n';
        return 1;
    }

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    FLoadTestRunner Runner(std::move(Config));
    const int Result = Runner.Run(GStopRequested);
    WSACleanup();
    return Result;
}
