#pragma once

#include "IPlayerSource.h"

#include <chrono>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class PlayerService
{
public:
    ~PlayerService();
    void SetSource(IPlayerSource* source);
    void Tick(int intervalMs);
    void Stop();

    std::vector<PlayerSnapshot> GetPlayers() const;
    std::string GetLastError() const;
    std::uint64_t GetSuccessfulSamples() const
    {
        return successfulSamples_.load();
    }
    std::uint64_t GetFailedSamples() const
    {
        return failedSamples_.load();
    }
    float GetLastPollMs() const
    {
        return lastPollMs_.load();
    }

private:
    IPlayerSource* source_ = nullptr;
    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool stop_{false};
    std::atomic<int> intervalMs_{100};
    std::vector<PlayerSnapshot> players_;
    std::string lastError_;
    std::atomic<std::uint64_t> successfulSamples_{0};
    std::atomic<std::uint64_t> failedSamples_{0};
    std::atomic<float> lastPollMs_{0.0f};
    void Worker();
};
