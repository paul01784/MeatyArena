#pragma once

#include "../../Memory/MemoryClient.h"
#include "../../Unity/Camera.h"
#include "../../Unity/Skeleton.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

enum class FireportState
{
    Disabled,
    LocalPlayerUnavailable,
    OffsetsUnavailable,
    HandsControllerUnavailable,
    PathUnavailable,
    TransformReadFailed,
    ProjectionUnavailable,
    Ready
};

class FireportTracker
{
public:
    void Update(const MemoryClient& memory, const Unity::Camera& camera, std::uint64_t localPlayer, float width, float height);
    void Clear(FireportState state = FireportState::Disabled);

    FireportState State() const
    {
        return state_;
    }
    const char* StateText() const;
    const char* Path() const
    {
        return path_ ? path_ : "-";
    }
    bool AimReferenceReady() const
    {
        return state_ == FireportState::Ready;
    }
    const Unity::Vector2& AimReference() const
    {
        return aimReference_;
    }
    const Unity::Vector3& Origin() const
    {
        return origin_;
    }
    const Unity::Vector3& Forward() const
    {
        return forward_;
    }
    std::uint64_t LocalPlayer() const
    {
        return localPlayer_;
    }
    std::uint64_t HandsController() const
    {
        return handsController_;
    }
    std::uint64_t TransformAddress() const
    {
        return access_.address;
    }

private:
    bool RefreshPath(const MemoryClient& memory, std::uint64_t localPlayer, std::uint64_t handsController);
    bool ResolveBifacial(const MemoryClient& memory, std::uint64_t bifacial, const char* path);
    bool ResolveManagedTransform(const MemoryClient& memory, std::uint64_t managedTransform, const char* path);
    bool ResolveTransform(const MemoryClient& memory, std::uint64_t candidate, const char* path);
    bool ReadWorldPose(const MemoryClient& memory, Unity::Vector3& position, Unity::Quaternion& rotation) const;
    void ClearTransform();

    FireportState state_ = FireportState::Disabled;
    std::uint64_t localPlayer_ = 0;
    std::uint64_t handsController_ = 0;
    Unity::TransformAccessInfo access_{};
    std::vector<std::int32_t> parentChain_;
    const char* path_ = nullptr;
    Unity::Vector3 origin_{};
    Unity::Vector3 forward_{};
    Unity::Vector2 aimReference_{};
    int consecutiveReadFailures_ = 0;
    std::chrono::steady_clock::time_point nextRefresh_{};
};
