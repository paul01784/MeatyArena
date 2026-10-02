#pragma once

#include "../Memory/MemoryClient.h"
#include "Bones.h"
#include "Types.h"

#include <chrono>
#include <cstdint>
#include <array>
#include <unordered_map>
#include <vector>

namespace Unity
{
    struct TrsX
    {
        Vector3 translation;
        float pad0 = 0;
        Quaternion rotation;
        Vector3 scale;
        float pad1 = 0;
    };
    static_assert(sizeof(TrsX) == 0x30);

    struct TransformAccessInfo
    {
        std::uint64_t address = 0;
        std::uint64_t hierarchy = 0;
        std::uint64_t vertices = 0;
        std::uint64_t indices = 0;
        std::int32_t index = -1;
        bool hasCachedWorldPosition = false;
        bool sawHierarchy = false;
        bool sawIndex = false;
    };

    bool ResolveTransformAccess(const MemoryClient& memory, std::uint64_t candidate, TransformAccessInfo& result);

    class Skeleton
    {
    public:
        struct Diagnostics
        {
            std::uint32_t components = 0;
            std::uint32_t hierarchies = 0;
            std::uint32_t indices = 0;
            std::uint32_t arrays = 0;
            std::uint32_t readyGroups = 0;
            std::uint32_t maxIndex = 0;
        };

        explicit Skeleton(const MemoryClient& memory) : memory_(memory)
        {
        }
        bool Initialize(std::uint64_t observedPlayer);
        bool ReadBone(Bone bone, Vector3& worldPosition) const;
        bool ReadBones(std::array<BoneSample, SkeletonBones.size()>& worldPositions);
        bool PrepareBones(std::vector<MemoryClient::ScatterEntry>& reads);
        bool FinishBones(std::array<BoneSample, SkeletonBones.size()>& worldPositions, const std::vector<bool>& completedEntries);
        std::uint8_t Stage() const
        {
            return stage_;
        }
        std::uint32_t ReadyBones() const
        {
            return readyBones_;
        }
        const Diagnostics& GetDiagnostics() const
        {
            return diagnostics_;
        }
        void Reset()
        {
            array_ = 0;
            stage_ = 0;
            readyBones_ = 0;
            diagnostics_ = {};
            cacheReady_ = false;
            accesses_ = {};
            groups_.clear();
            liveVertices_.clear();
            liveReadEntries_.clear();
            prepared_ = false;
            nextArrayRefreshAt_ = {};
            nextGroupRefreshAt_ = {};
            nextBuildAttemptAt_ = {};
        }

    private:
        struct CachedAccess
        {
            std::size_t group = 0;
            std::int32_t index = -1;
            bool ready = false;
        };
        struct CachedGroup
        {
            std::uint64_t hierarchy = 0;
            std::uint64_t verticesAddress = 0;
            std::uint64_t indicesAddress = 0;
            std::int32_t maxIndex = -1;
            std::size_t representativeBone = 0;
            std::vector<std::int32_t> parents;
        };
        bool BuildCache();
        bool RefreshCache();
        const MemoryClient& memory_;
        std::uint64_t array_ = 0;
        std::uint8_t stage_ = 0;
        std::uint32_t readyBones_ = 0;
        Diagnostics diagnostics_{};
        bool cacheReady_ = false;
        std::array<CachedAccess, SkeletonBones.size()> accesses_{};
        std::vector<CachedGroup> groups_;
        std::vector<std::vector<TrsX>> liveVertices_;
        std::vector<std::size_t> liveReadEntries_;
        bool prepared_ = false;
        std::chrono::steady_clock::time_point nextArrayRefreshAt_{};
        std::chrono::steady_clock::time_point nextGroupRefreshAt_{};
        std::chrono::steady_clock::time_point nextBuildAttemptAt_{};
    };
}
