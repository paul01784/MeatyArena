#include "Skeleton.h"

#include "../SDK/Offsets.h"
#include "../Core/Logging/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace
{
    constexpr auto SkeletonPointerRefreshInterval = std::chrono::milliseconds(500);
    constexpr auto SkeletonBuildRetryInterval = std::chrono::milliseconds(100);

    bool Pointer(const MemoryClient& memory, std::uint64_t address, std::uint64_t& value, bool uncached = false)
    {
        return memory.TryRead(address, value, uncached) && Unity::IsValidAddress(value);
    }

    struct TransformLayout
    {
        std::uint32_t hierarchy;
        std::uint32_t index;
        std::uint32_t vertices;
        std::uint32_t indices;
        bool hasCachedWorldPosition;
    };

    constexpr std::array<TransformLayout, 2> TransformLayouts = {{
        {0x58, 0x60, 0x50, 0xA0, true},  // Arena Unity 6000.3 layout
        {0x70, 0x78, 0x68, 0x40, false}, // alternate Unity native layout
    }};

    std::mutex DiscoveredLayoutMutex;
    std::optional<TransformLayout> DiscoveredLayout;
    std::chrono::steady_clock::time_point LastDiscoveryAttempt{};

    Unity::Vector3 Rotate(const Unity::Vector3& v, const Unity::Quaternion& q)
    {
        const Unity::Vector3 u{q.x, q.y, q.z};
        const float dot = u.x * v.x + u.y * v.y + u.z * v.z;
        const float dotU = u.x * u.x + u.y * u.y + u.z * u.z;
        const Unity::Vector3 cross{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        return u * (2.0f * dot) + v * (q.w * q.w - dotU) + cross * (2.0f * q.w);
    }

    bool TryLayout(const MemoryClient& memory, std::uint64_t current, const TransformLayout& layout, Unity::TransformAccessInfo& result)
    {
        std::uint64_t hierarchy = 0;
        if (!Pointer(memory, current + layout.hierarchy, hierarchy, true))
            return false;
        result.sawHierarchy = true;
        std::int32_t index = -1;
        if (!memory.TryRead(current + layout.index, index, true) || index < 0 || index > 128000)
            return false;
        result.sawIndex = true;
        std::uint64_t vertices = 0, indices = 0;
        if (!Pointer(memory, hierarchy + layout.vertices, vertices, true) || !Pointer(memory, hierarchy + layout.indices, indices, true))
            return false;
        result.address = current;
        result.hierarchy = hierarchy;
        result.vertices = vertices;
        result.indices = indices;
        result.index = index;
        result.hasCachedWorldPosition = layout.hasCachedWorldPosition;
        return true;
    }

    bool LooksLikeParents(const MemoryClient& memory, std::uint64_t address, std::int32_t index)
    {
        if (index <= 0 || index > 4096)
            return false;
        const auto count = static_cast<std::size_t>(index) + 1;
        std::vector<std::int32_t> values(count);
        if (!memory.Read(address, values.data(), values.size() * sizeof(values[0]), true))
            return false;
        std::size_t valid = 0, roots = 0, backwards = 0;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const auto parent = values[i];
            if (parent == -1)
            {
                ++valid;
                ++roots;
                continue;
            }
            if (parent >= 0 && parent < static_cast<std::int32_t>(count))
            {
                ++valid;
                if (parent < static_cast<std::int32_t>(i))
                    ++backwards;
            }
        }
        return roots != 0 && valid * 10 >= count * 9 && backwards * 2 >= count;
    }

    bool LooksLikeVertices(const MemoryClient& memory, std::uint64_t address, std::int32_t index)
    {
        if (index <= 0 || index > 4096)
            return false;
        const auto count = static_cast<std::size_t>(index) + 1;
        std::vector<Unity::TrsX> values(count);
        if (!memory.Read(address, values.data(), values.size() * sizeof(values[0]), true))
            return false;
        const std::size_t sampleCount = (std::min<std::size_t>)(values.size(), 64);
        std::size_t finite = 0, plausibleRotations = 0;
        for (std::size_t sample = 0; sample < sampleCount; ++sample)
        {
            const std::size_t i = sampleCount == 1 ? 0 : sample * (values.size() - 1) / (sampleCount - 1);
            const auto& v = values[i];
            const bool sane = v.translation.IsFinite() && v.scale.IsFinite() && std::isfinite(v.rotation.x) && std::isfinite(v.rotation.y) && std::isfinite(v.rotation.z) &&
                              std::isfinite(v.rotation.w) && std::abs(v.translation.x) < 1.0e6f && std::abs(v.translation.y) < 1.0e6f && std::abs(v.translation.z) < 1.0e6f &&
                              std::abs(v.scale.x) < 1.0e4f && std::abs(v.scale.y) < 1.0e4f && std::abs(v.scale.z) < 1.0e4f;
            if (!sane)
                continue;
            ++finite;
            const float norm = v.rotation.x * v.rotation.x + v.rotation.y * v.rotation.y + v.rotation.z * v.rotation.z + v.rotation.w * v.rotation.w;
            if (norm > 0.25f && norm < 2.25f)
                ++plausibleRotations;
        }
        return finite * 10 >= sampleCount * 9 && plausibleRotations * 3 >= sampleCount;
    }

    bool DiscoverLayout(const MemoryClient& memory, std::uint64_t candidate, TransformLayout& discovered, Unity::TransformAccessInfo& result)
    {
        std::uint64_t current = candidate;
        for (int depth = 0; depth < 4 && Unity::IsValidAddress(current); ++depth)
        {
            std::array<std::uint8_t, 0x200> transformBytes{};
            if (memory.Read(current, transformBytes.data(), transformBytes.size(), true))
            {
                for (std::uint32_t hierarchyOffset = 0x20; hierarchyOffset <= 0x1E8; hierarchyOffset += 8)
                {
                    std::uint64_t hierarchy = 0;
                    std::memcpy(&hierarchy, transformBytes.data() + hierarchyOffset, sizeof(hierarchy));
                    if (!Unity::IsValidAddress(hierarchy) || hierarchy == current)
                        continue;
                    std::int32_t index = -1;
                    std::memcpy(&index, transformBytes.data() + hierarchyOffset + 8, sizeof(index));
                    if (index <= 0 || index > 4096)
                        continue;

                    std::array<std::uint8_t, 0x120> hierarchyBytes{};
                    if (!memory.Read(hierarchy, hierarchyBytes.data(), hierarchyBytes.size(), true))
                        continue;
                    std::optional<std::uint32_t> verticesOffset, indicesOffset;
                    for (std::uint32_t offset = 0x20; offset <= 0x110; offset += 8)
                    {
                        std::uint64_t array = 0;
                        std::memcpy(&array, hierarchyBytes.data() + offset, sizeof(array));
                        if (!Unity::IsValidAddress(array))
                            continue;
                        if (!indicesOffset && LooksLikeParents(memory, array, index))
                            indicesOffset = offset;
                        if (!verticesOffset && LooksLikeVertices(memory, array, index))
                            verticesOffset = offset;
                        if (verticesOffset && indicesOffset && *verticesOffset != *indicesOffset)
                            break;
                    }
                    if (!verticesOffset || !indicesOffset || *verticesOffset == *indicesOffset)
                        continue;
                    discovered = {hierarchyOffset, hierarchyOffset + 8, *verticesOffset, *indicesOffset, false};
                    return TryLayout(memory, current, discovered, result);
                }
            }
            std::uint64_t inner = 0;
            if (!Pointer(memory, current + 0x10, inner, true) || inner == current)
                break;
            current = inner;
        }
        return false;
    }

} 

bool Unity::ResolveTransformAccess(const MemoryClient& memory, std::uint64_t candidate, TransformAccessInfo& result)
{
    result = {};
    std::optional<TransformLayout> cached;
    {
        std::lock_guard lock(DiscoveredLayoutMutex);
        cached = DiscoveredLayout;
    }

    std::uint64_t current = candidate;
    for (int depth = 0; depth < 4 && IsValidAddress(current); ++depth)
    {
        if (cached && TryLayout(memory, current, *cached, result))
            return true;
        for (const auto& layout : TransformLayouts)
            if (TryLayout(memory, current, layout, result))
                return true;
        std::uint64_t inner = 0;
        if (!Pointer(memory, current + 0x10, inner, true) || inner == current)
            break;
        current = inner;
    }

    std::lock_guard lock(DiscoveredLayoutMutex);
    const auto now = std::chrono::steady_clock::now();
    if (LastDiscoveryAttempt.time_since_epoch().count() != 0 && now - LastDiscoveryAttempt < std::chrono::seconds(2))
        return false;
    LastDiscoveryAttempt = now;
    TransformLayout discovered{};
    if (!DiscoverLayout(memory, candidate, discovered, result))
    {
        Log::Write("Transform: adaptive layout scan found no valid hierarchy/array combination.");
        return false;
    }
    DiscoveredLayout = discovered;
    std::ostringstream message;
    message << "Transform: discovered live layout TA[h=0x" << std::hex << std::uppercase << discovered.hierarchy << ", i=0x" << discovered.index << "] TH[v=0x"
            << discovered.vertices << ", i=0x" << discovered.indices << "].";
    Log::Write(message.str());
    return true;
}

bool Unity::Skeleton::Initialize(std::uint64_t player)
{
    const auto keepCurrent = [this]
    {
        return cacheReady_ && IsValidAddress(array_);
    };
    const auto now = std::chrono::steady_clock::now();
    if (IsValidAddress(array_) && now < nextArrayRefreshAt_)
        return true;
    if (IsValidAddress(array_))
        nextArrayRefreshAt_ = now + SkeletonPointerRefreshInterval;
    stage_ = 1;
    readyBones_ = 0;
    std::uint64_t body = 0, root = 0, values = 0, nextArray = 0;
    if (!Pointer(memory_, player + ArenaOffsets::ObservedPlayerView::PlayerBody, body, true))
        return keepCurrent();
    stage_ = 2;
    if (!Pointer(memory_, body + ArenaOffsets::PlayerBody::SkeletonRootJoint, root, true))
        return keepCurrent();
    stage_ = 3;
    if (!Pointer(memory_, root + ArenaOffsets::DizSkinningSkeleton::Values, values, true))
        return keepCurrent();
    stage_ = 4;
    if (!Pointer(memory_, values + UnityOffsets::ManagedList::ItemsPtr, nextArray, true))
        return keepCurrent();
    if (nextArray != array_)
    {
        array_ = nextArray;
        cacheReady_ = false;
        accesses_ = {};
        groups_.clear();
        liveVertices_.clear();
        liveReadEntries_.clear();
        diagnostics_ = {};
        prepared_ = false;
        nextGroupRefreshAt_ = {};
        nextBuildAttemptAt_ = {};
    }
    nextArrayRefreshAt_ = now + SkeletonPointerRefreshInterval;
    stage_ = 5;
    return true;
}

bool Unity::Skeleton::BuildCache()
{
    cacheReady_ = false;
    accesses_ = {};
    groups_.clear();
    diagnostics_ = {};
    stage_ = 6;

    std::unordered_map<std::uint64_t, std::size_t> groupIndices;
    for (std::size_t boneIndex = 0; boneIndex < SkeletonBones.size(); ++boneIndex)
    {
        std::uint64_t component = 0, candidate = 0;
        if (!Pointer(memory_, array_ + 0x20 + static_cast<std::uint32_t>(SkeletonBones[boneIndex]) * 8ULL, component, true) ||
            !Pointer(memory_, component + 0x10, candidate, true))
            continue;
        ++diagnostics_.components;
        TransformAccessInfo transform;
        const bool resolved = ResolveTransformAccess(memory_, candidate, transform);
        if (transform.sawHierarchy)
            ++diagnostics_.hierarchies;
        if (transform.sawIndex)
            ++diagnostics_.indices;
        if (!resolved)
            continue;
        diagnostics_.maxIndex = (std::max)(diagnostics_.maxIndex, static_cast<std::uint32_t>(transform.index));

        auto [it, inserted] = groupIndices.emplace(transform.hierarchy, groups_.size());
        if (inserted)
        {
            CachedGroup group;
            group.hierarchy = transform.hierarchy;
            group.verticesAddress = transform.vertices;
            group.indicesAddress = transform.indices;
            group.representativeBone = boneIndex;
            groups_.push_back(std::move(group));
        }
        const auto groupIndex = it->second;
        groups_[groupIndex].maxIndex = (std::max)(groups_[groupIndex].maxIndex, transform.index);
        accesses_[boneIndex] = {groupIndex, transform.index, true};
        ++diagnostics_.arrays;
    }
    if (groups_.empty())
        return false;

    std::vector<MemoryClient::ScatterEntry> reads;
    reads.reserve(groups_.size());
    for (auto& group : groups_)
    {
        if (group.maxIndex < 0)
            continue;
        group.parents.resize(static_cast<std::size_t>(group.maxIndex) + 1);
        reads.push_back({group.indicesAddress, group.parents.data(), static_cast<std::uint32_t>(group.parents.size() * sizeof(group.parents[0]))});
    }
    const bool complete = memory_.ReadScatter(reads, true);
    for (auto& group : groups_)
    {
        const bool ready = complete || memory_.Read(group.indicesAddress, group.parents.data(), group.parents.size() * sizeof(group.parents[0]), true);
        if (ready)
            ++diagnostics_.readyGroups;
        else
            group.parents.clear();
    }
    cacheReady_ = diagnostics_.arrays == SkeletonBones.size() && diagnostics_.readyGroups == groups_.size();
    return cacheReady_;
}

bool Unity::Skeleton::RefreshCache()
{
    for (const auto& group : groups_)
    {
        if (group.representativeBone >= SkeletonBones.size())
            continue;
        std::uint64_t component = 0, candidate = 0;
        const auto bone = SkeletonBones[group.representativeBone];
        if (!Pointer(memory_, array_ + 0x20 + static_cast<std::uint32_t>(bone) * 8ULL, component, true) || !Pointer(memory_, component + 0x10, candidate, true))
            continue;
        TransformAccessInfo transform;
        if (!ResolveTransformAccess(memory_, candidate, transform))
            continue;
        const auto& access = accesses_[group.representativeBone];
        if (transform.hierarchy == group.hierarchy && transform.vertices == group.verticesAddress && transform.indices == group.indicesAddress &&
            transform.index == access.index)
            continue;

        cacheReady_ = false;
        return BuildCache();
    }
    return true;
}

bool Unity::Skeleton::ReadBone(Bone bone, Vector3& result) const
{
    if (!IsValidAddress(array_))
        return false;
    std::uint64_t component = 0, candidate = 0;
    if (!Pointer(memory_, array_ + 0x20 + static_cast<std::uint32_t>(bone) * 8ULL, component) || !Pointer(memory_, component + 0x10, candidate))
        return false;
    TransformAccessInfo transform;
    if (!ResolveTransformAccess(memory_, candidate, transform))
        return false;
    std::vector<TrsX> vertices(transform.index + 1);
    std::vector<std::int32_t> parents(transform.index + 1);
    if (!memory_.Read(transform.vertices, vertices.data(), vertices.size() * sizeof(TrsX), true) ||
        !memory_.Read(transform.indices, parents.data(), parents.size() * sizeof(std::int32_t), true))
        return false;
    Vector3 position = vertices[transform.index].translation;
    int parent = parents[transform.index];
    for (int iteration = 0; parent >= 0 && parent < static_cast<int>(vertices.size()) && iteration < 4096; ++iteration)
    {
        const auto& p = vertices[parent];
        position = Rotate(position, p.rotation);
        position = {position.x * p.scale.x, position.y * p.scale.y, position.z * p.scale.z};
        position = position + p.translation;
        parent = parents[parent];
    }
    if (!position.IsFinite())
        return false;
    result = position;
    return true;
}

bool Unity::Skeleton::ReadBones(std::array<BoneSample, SkeletonBones.size()>& output)
{
    std::vector<MemoryClient::ScatterEntry> reads;
    if (!PrepareBones(reads))
    {
        output = {};
        return false;
    }
    std::vector<bool> completed;
    memory_.ReadScatter(reads, true, &completed);
    return FinishBones(output, completed);
}

bool Unity::Skeleton::PrepareBones(std::vector<MemoryClient::ScatterEntry>& reads)
{
    prepared_ = false;
    readyBones_ = 0;
    if (!IsValidAddress(array_))
        return false;
    const auto now = std::chrono::steady_clock::now();
    if (!cacheReady_)
    {
        if (now < nextBuildAttemptAt_)
            return false;
        nextBuildAttemptAt_ = now + SkeletonBuildRetryInterval;
        if (!BuildCache())
            return false;
        nextGroupRefreshAt_ = now + SkeletonPointerRefreshInterval;
    }
    if (now >= nextGroupRefreshAt_)
    {
        nextGroupRefreshAt_ = now + SkeletonPointerRefreshInterval;
        if (!RefreshCache())
        {
            nextBuildAttemptAt_ = now + SkeletonBuildRetryInterval;
            return false;
        }
    }
    liveVertices_.clear();
    liveVertices_.resize(groups_.size());
    liveReadEntries_.assign(groups_.size(), static_cast<std::size_t>(-1));
    for (std::size_t i = 0; i < groups_.size(); ++i)
    {
        const auto& group = groups_[i];
        if (group.maxIndex < 0)
            continue;
        const std::size_t count = static_cast<std::size_t>(group.maxIndex) + 1;
        liveVertices_[i].resize(count);
        liveReadEntries_[i] = reads.size();
        reads.push_back({group.verticesAddress, liveVertices_[i].data(), static_cast<std::uint32_t>(count * sizeof(TrsX))});
    }
    prepared_ = true;
    return true;
}

bool Unity::Skeleton::FinishBones(std::array<BoneSample, SkeletonBones.size()>& output, const std::vector<bool>& completedEntries)
{
    output = {};
    if (!prepared_)
        return false;
    prepared_ = false;
    stage_ = 7;
    std::vector<bool> ready(groups_.size(), false);
    for (std::size_t i = 0; i < groups_.size(); ++i)
    {
        const auto entry = liveReadEntries_[i];
        ready[i] = entry < completedEntries.size() && completedEntries[entry];
        if (!ready[i] && !liveVertices_[i].empty())
            ready[i] = memory_.Read(groups_[i].verticesAddress, liveVertices_[i].data(), liveVertices_[i].size() * sizeof(TrsX), true);
    }

    bool any = false;
    for (std::size_t boneIndex = 0; boneIndex < accesses_.size(); ++boneIndex)
    {
        const auto& access = accesses_[boneIndex];
        if (!access.ready || access.group >= groups_.size() || !ready[access.group])
            continue;
        const auto& group = groups_[access.group];
        const auto& groupVertices = liveVertices_[access.group];
        if (access.index < 0 || access.index >= static_cast<int>(groupVertices.size()) || access.index >= static_cast<int>(group.parents.size()))
            continue;
        Vector3 position = groupVertices[access.index].translation;
        int parent = group.parents[access.index];
        int iterations = 0;
        while (parent >= 0 && parent < static_cast<int>(groupVertices.size()) && iterations++ < 4096)
        {
            const auto& p = groupVertices[parent];
            position = Rotate(position, p.rotation);
            position = {position.x * p.scale.x, position.y * p.scale.y, position.z * p.scale.z};
            position = position + p.translation;
            parent = group.parents[parent];
        }

        if (!position.IsFinite())
            continue;
        output[boneIndex] = {position, true};
        ++readyBones_;
        any = true;
    }
    stage_ = any ? 8 : 7;
    return any;
}
