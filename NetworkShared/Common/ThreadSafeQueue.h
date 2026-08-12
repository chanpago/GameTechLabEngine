#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace Network
{
    template <typename T>
    class TThreadSafeQueue
    {
    public:
        explicit TThreadSafeQueue(std::size_t InMaxItems = 4096)
            : MaxItems(InMaxItems)
        {
        }

        bool TryPush(T Item)
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            if (Queue.size() >= MaxItems)
            {
                return false;
            }
            Queue.push_back(std::move(Item));
            return true;
        }

        std::vector<T> Drain(std::size_t MaxCount = static_cast<std::size_t>(-1))
        {
            std::vector<T> Result;
            std::lock_guard<std::mutex> Lock(Mutex);
            const std::size_t Count = Queue.size() < MaxCount ? Queue.size() : MaxCount;
            Result.reserve(Count);
            for (std::size_t Index = 0; Index < Count; ++Index)
            {
                Result.push_back(std::move(Queue.front()));
                Queue.pop_front();
            }
            return Result;
        }

        void Clear()
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            Queue.clear();
        }

        std::size_t Size() const
        {
            std::lock_guard<std::mutex> Lock(Mutex);
            return Queue.size();
        }

    private:
        const std::size_t MaxItems;
        mutable std::mutex Mutex;
        std::deque<T> Queue;
    };
}
