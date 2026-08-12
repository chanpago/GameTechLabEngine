#pragma once

#include <fstream>
#include <mutex>
#include <string>

class FServerLogger
{
public:
    FServerLogger();
    void Log(const std::string& Message);

private:
    std::mutex Mutex;
    std::ofstream File;
};
