#include "PlayerService.h"
#include "../Core/Logging/Log.h"

#include <algorithm>
#include <thread>

PlayerService::~PlayerService()
{
    Stop();
}

void PlayerService::SetSource(IPlayerSource* source)
{
    if (source_ == source)
        return;
    Stop();
    source_ = source;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        players_.clear();
        lastError_.clear();
    }
    successfulSamples_ = 0;
    failedSamples_ = 0;
    stop_ = false;
    if (source_)
        worker_ = std::thread(&PlayerService::Worker, this);
}

void PlayerService::Tick(int intervalMs)
{
    intervalMs_ = (std::clamp)(intervalMs, 8, 5000);
}

void PlayerService::Stop()
{
    stop_ = true;
    if (worker_.joinable())
        worker_.join();
    source_ = nullptr;
}

std::vector<PlayerSnapshot> PlayerService::GetPlayers() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return players_;
}

std::string PlayerService::GetLastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

void PlayerService::Worker()
{
    constexpr auto FailedSampleRetention = std::chrono::seconds(10);
    std::chrono::steady_clock::time_point lastSuccessfulSample{};

    while (!stop_)
    {
        const auto began = std::chrono::steady_clock::now();
        std::vector<PlayerSnapshot> nextPlayers;
        std::string error;
        if (source_->ReadPlayers(nextPlayers, error))
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                players_ = std::move(nextPlayers);
                lastError_.clear();
            }
            lastSuccessfulSample = std::chrono::steady_clock::now();
            ++successfulSamples_;
        }
        else
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (error != lastError_ && error != "Connect DMA first.")
                Log::Write("Player worker: " + error);
            lastError_ = std::move(error);
            const auto failedAt = std::chrono::steady_clock::now();
            const bool clearPlayers = lastSuccessfulSample.time_since_epoch().count() == 0 || failedAt - lastSuccessfulSample >= FailedSampleRetention ||
                                      lastError_ == "Connect PCLeech first.";
            if (clearPlayers)
                players_.clear();
            else
                for (auto& player : players_)
                    player.active = false;
            ++failedSamples_;
        }
        const auto finished = std::chrono::steady_clock::now();
        lastPollMs_ = std::chrono::duration<float, std::milli>(finished - began).count();

        const auto deadline = began + std::chrono::milliseconds(intervalMs_.load());
        if (finished < deadline)
            std::this_thread::sleep_until(deadline);
    }
}
