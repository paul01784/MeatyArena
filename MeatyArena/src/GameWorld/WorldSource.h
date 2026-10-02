#pragma once

#include "../Memory/MemoryClient.h"
#include "../Players/IPlayerSource.h"
#include "../Unity/Skeleton.h"

#include <cstdint>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

class WorldSource final : public IPlayerSource
{
public:
    explicit WorldSource(MemoryClient& memory) : memory_(memory)
    {
    }
    bool ReadPlayers(std::vector<PlayerSnapshot>& out, std::string& error) override;
    std::uint64_t WorldAddress() const
    {
        return publishedWorld_.load(std::memory_order_acquire);
    }
    std::uint64_t LocalPlayerAddress() const
    {
        return publishedLocalPlayer_.load(std::memory_order_acquire);
    }
    bool InRaid() const
    {
        return inRaid_.load(std::memory_order_acquire);
    }

private:
    struct Tracked
    {
        PlayerSnapshot snapshot;
        std::uint64_t positionAddress = 0;
        std::uint64_t transformVerticesAddress = 0;
        std::uint64_t transformIndicesAddress = 0;
        std::int32_t transformIndex = -1;
        std::uint64_t rotationAddress = 0;
        Unity::Vector3 position{};
        Unity::Vector2 rotation{};
        bool established = false;
        bool usingBonePosition = false;
        bool local = false;
        bool classificationReady = false;
        std::chrono::steady_clock::time_point missingSince{};
        std::uint64_t nextTeamTick = 0;
        std::unique_ptr<Unity::Skeleton> skeleton;
    };

    bool ResolveWorld(std::string& error);
    bool ReadManagedString(std::uint64_t address, std::string& value) const;
    bool InitializeTransform(std::uint64_t player, Tracked& tracked) const;
    bool InitializeRotation(std::uint64_t player, Tracked& tracked) const;
    int ResolveTeamId(std::uint64_t player, bool local) const;
    void ReadRealtime();
    MemoryClient& memory_;
    std::uint64_t world_ = 0;
    std::uint64_t ownerClass_ = 0;
    std::uint64_t localPlayer_ = 0;
    std::uint32_t typeIndex_ = 0;
    std::uint64_t sampleTick_ = 0;
    std::uint32_t lastRegisteredPlayerCount_ = 0;
    std::chrono::steady_clock::time_point lastActivePlayerListAt_{};
    std::atomic<std::uint64_t> publishedWorld_{0};
    std::atomic<std::uint64_t> publishedLocalPlayer_{0};
    std::atomic_bool inRaid_{false};
    std::unordered_map<std::uint64_t, Tracked> tracked_;
};
