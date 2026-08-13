#pragma once

#include "LoadTestConfig.h"

#include <atomic>
#include <memory>

class FLoadTestRunner
{
public:
    explicit FLoadTestRunner(FLoadTestConfig InConfig);
    ~FLoadTestRunner();

    FLoadTestRunner(const FLoadTestRunner&) = delete;
    FLoadTestRunner& operator=(const FLoadTestRunner&) = delete;

    int Run(const std::atomic<bool>& ExternalStopRequested);

private:
    class FImpl;
    std::unique_ptr<FImpl> Impl;
};
