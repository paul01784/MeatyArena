#include "FireportTracker.h"

#include "../../Core/Logging/Log.h"
#include "../../SDK/Offsets.h"

#include <cmath>
#include <unordered_set>

namespace
{
    constexpr float ProjectionDistance = 100.0f;

    bool Pointer(const MemoryClient& memory, std::uint64_t address, std::uint64_t& value)
    {
        return memory.TryRead(address, value, true) && Unity::IsValidAddress(value);
    }

    bool FieldPointer(const MemoryClient& memory, std::uint64_t object, std::uint32_t offset, std::uint64_t& value)
    {
        value = 0;
        return Unity::IsValidAddress(object) && offset != 0 && Pointer(memory, object + offset, value);
    }

    Unity::Vector3 Rotate(const Unity::Vector3& vector, const Unity::Quaternion& rotation)
    {
        const Unity::Vector3 u{rotation.x, rotation.y, rotation.z};
        const float dot = u.x * vector.x + u.y * vector.y + u.z * vector.z;
        const float dotU = u.x * u.x + u.y * u.y + u.z * u.z;
        const Unity::Vector3 cross{u.y * vector.z - u.z * vector.y, u.z * vector.x - u.x * vector.z, u.x * vector.y - u.y * vector.x};
        return u * (2.0f * dot) + vector * (rotation.w * rotation.w - dotU) + cross * (2.0f * rotation.w);
    }

    Unity::Quaternion Multiply(const Unity::Quaternion& left, const Unity::Quaternion& right)
    {
        return {left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y, left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
                left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w, left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z};
    }

    bool Normalize(Unity::Quaternion& rotation)
    {
        const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
        if (!std::isfinite(lengthSquared) || lengthSquared < 0.25f || lengthSquared > 2.25f)
            return false;
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        rotation.x *= inverseLength;
        rotation.y *= inverseLength;
        rotation.z *= inverseLength;
        rotation.w *= inverseLength;
        return true;
    }

    bool OnScreen(const Unity::Vector2& point, float width, float height)
    {
        return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0.0f && point.x <= width && point.y >= 0.0f && point.y <= height;
    }
} // namespace

const char* FireportTracker::StateText() const
{
    switch (state_)
    {
    case FireportState::Disabled:
        return "Disabled";
    case FireportState::LocalPlayerUnavailable:
        return "Local player unavailable";
    case FireportState::OffsetsUnavailable:
        return "Runtime offsets unavailable";
    case FireportState::HandsControllerUnavailable:
        return "Hands controller unavailable";
    case FireportState::PathUnavailable:
        return "No fireport path resolved";
    case FireportState::TransformReadFailed:
        return "Fireport transform read failed";
    case FireportState::ProjectionUnavailable:
        return "Fireport direction is off screen";
    case FireportState::Ready:
        return "Ready";
    default:
        return "Unknown";
    }
}

void FireportTracker::ClearTransform()
{
    access_ = {};
    parentChain_.clear();
    path_ = nullptr;
    consecutiveReadFailures_ = 0;
}

void FireportTracker::Clear(FireportState state)
{
    state_ = state;
    localPlayer_ = 0;
    handsController_ = 0;
    origin_ = {};
    forward_ = {};
    aimReference_ = {};
    nextRefresh_ = {};
    ClearTransform();
}

bool FireportTracker::ResolveTransform(const MemoryClient& memory, std::uint64_t candidate, const char* path)
{
    Unity::TransformAccessInfo access;
    if (!Unity::IsValidAddress(candidate) || !Unity::ResolveTransformAccess(memory, candidate, access))
        return false;

    std::vector<std::int32_t> chain;
    std::unordered_set<std::int32_t> visited;
    std::int32_t current = access.index;
    for (int depth = 0; depth < 4096; ++depth)
    {
        if (current < 0 || current > 128000 || !visited.insert(current).second)
            return false;
        chain.push_back(current);
        std::int32_t parent = -1;
        if (!memory.TryRead(access.indices + static_cast<std::uint64_t>(current) * sizeof(parent), parent, true))
            return false;
        if (parent == -1)
        {
            access_ = access;
            parentChain_ = std::move(chain);
            path_ = path;
            consecutiveReadFailures_ = 0;
            return true;
        }
        current = parent;
    }
    return false;
}

bool FireportTracker::ResolveManagedTransform(const MemoryClient& memory, std::uint64_t managedTransform, const char* path)
{
    return ResolveTransform(memory, managedTransform, path);
}

bool FireportTracker::ResolveBifacial(const MemoryClient& memory, std::uint64_t bifacial, const char* path)
{
    std::uint64_t original = 0;
    return FieldPointer(memory, bifacial, ArenaOffsets::BifacialTransform::Original, original) && ResolveManagedTransform(memory, original, path);
}

bool FireportTracker::RefreshPath(const MemoryClient& memory, std::uint64_t localPlayer, std::uint64_t handsController)
{
    ClearTransform();
    std::uint64_t value = 0;
    if (FieldPointer(memory, handsController, ArenaOffsets::FirearmController::Fireport, value) && ResolveBifacial(memory, value, "firearm.fireport"))
        return true;

    std::uint64_t playerBones = 0;
    if (FieldPointer(memory, localPlayer, ArenaOffsets::Player::PlayerBones, playerBones) && FieldPointer(memory, playerBones, ArenaOffsets::PlayerBones::Fireport, value) &&
        ResolveBifacial(memory, value, "player_bones.fireport"))
        return true;

    std::uint64_t firearms = 0;
    if (FieldPointer(memory, handsController, ArenaOffsets::FirearmController::Firearms, firearms) && FieldPointer(memory, firearms, ArenaOffsets::Firearms::Fireport, value) &&
        ResolveBifacial(memory, value, "firearms._fireport"))
        return true;

    if (FieldPointer(memory, handsController, ArenaOffsets::FirearmController::GunBaseTransform, value) && ResolveManagedTransform(memory, value, "firearm.gun_base_transform"))
        return true;

    std::uint64_t pwa = 0, spring = 0;
    if (FieldPointer(memory, localPlayer, ArenaOffsets::Player::ProceduralWeaponAnimation, pwa) &&
        FieldPointer(memory, pwa, ArenaOffsets::ProceduralWeaponAnimation::HandsContainer, spring) && FieldPointer(memory, spring, ArenaOffsets::PlayerSpring::Fireport, value) &&
        ResolveManagedTransform(memory, value, "pwa.spring.fireport"))
        return true;
    return false;
}

bool FireportTracker::ReadWorldPose(const MemoryClient& memory, Unity::Vector3& position, Unity::Quaternion& rotation) const
{
    if (!Unity::IsValidAddress(access_.vertices) || parentChain_.empty())
        return false;
    std::vector<Unity::TrsX> transforms(parentChain_.size());
    std::vector<MemoryClient::ScatterEntry> reads;
    reads.reserve(parentChain_.size());
    for (std::size_t index = 0; index < parentChain_.size(); ++index)
        reads.push_back({access_.vertices + static_cast<std::uint64_t>(parentChain_[index]) * sizeof(Unity::TrsX), &transforms[index], sizeof(Unity::TrsX)});
    std::vector<bool> completed;
    memory.ReadScatter(reads, true, &completed);
    if (completed.size() != transforms.size())
        return false;
    for (const bool complete : completed)
        if (!complete)
            return false;

    position = transforms.front().translation;
    rotation = transforms.front().rotation;
    for (std::size_t index = 1; index < transforms.size(); ++index)
    {
        const auto& parent = transforms[index];
        position = {position.x * parent.scale.x, position.y * parent.scale.y, position.z * parent.scale.z};
        position = Rotate(position, parent.rotation) + parent.translation;
        rotation = Multiply(parent.rotation, rotation);
    }
    return position.IsFinite() && Normalize(rotation);
}

void FireportTracker::Update(const MemoryClient& memory, const Unity::Camera& camera, std::uint64_t localPlayer, float width, float height)
{
    origin_ = {};
    forward_ = {};
    aimReference_ = {};
    if (!Unity::IsValidAddress(localPlayer))
    {
        Clear(FireportState::LocalPlayerUnavailable);
        return;
    }
    if (ArenaOffsets::Player::HandsController == 0)
    {
        Clear(FireportState::OffsetsUnavailable);
        return;
    }

    std::uint64_t handsController = 0;
    if (!FieldPointer(memory, localPlayer, ArenaOffsets::Player::HandsController, handsController))
    {
        if (localPlayer != localPlayer_)
            ClearTransform();
        localPlayer_ = localPlayer;
        handsController_ = 0;
        state_ = FireportState::HandsControllerUnavailable;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool identityChanged = localPlayer != localPlayer_ || handsController != handsController_;
    localPlayer_ = localPlayer;
    handsController_ = handsController;
    if (identityChanged || parentChain_.empty() || now >= nextRefresh_)
    {
        if (!RefreshPath(memory, localPlayer, handsController))
        {
            state_ = FireportState::PathUnavailable;
            nextRefresh_ = now + std::chrono::seconds(1);
            return;
        }
        Log::Write(std::string("Fireport: resolved ") + Path() + ".");
        nextRefresh_ = (std::chrono::steady_clock::time_point::max)();
    }

    Unity::Quaternion rotation;
    if (!ReadWorldPose(memory, origin_, rotation))
    {
        state_ = FireportState::TransformReadFailed;
        if (++consecutiveReadFailures_ >= 3)
        {
            ClearTransform();
            nextRefresh_ = now + std::chrono::milliseconds(250);
        }
        return;
    }
    consecutiveReadFailures_ = 0;
    forward_ = Rotate({0.0f, -1.0f, 0.0f}, rotation);
    const float lengthSquared = forward_.LengthSquared();
    if (!forward_.IsFinite() || lengthSquared < 1.0e-8f)
    {
        state_ = FireportState::TransformReadFailed;
        return;
    }
    forward_ = forward_ * (1.0f / std::sqrt(lengthSquared));
    const Unity::Vector3 endpoint = origin_ + forward_ * ProjectionDistance;
    if (!camera.WorldToScreen(endpoint, aimReference_, width, height) || !OnScreen(aimReference_, width, height))
    {
        state_ = FireportState::ProjectionUnavailable;
        return;
    }
    state_ = FireportState::Ready;
}
