#include "WorldSource.h"

#include "../SDK/Offsets.h"
#include "../Unity/Collections.h"
#include "../Unity/Skeleton.h"
#include "../Core/Logging/Log.h"
#include "../Players/TeamColours.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace
{
    bool Pointer(const MemoryClient& memory, std::uint64_t address, std::uint64_t& value, bool uncached = false)
    {
        return memory.TryRead(address, value, uncached) && Unity::IsValidAddress(value);
    }

    Unity::Vector3 Rotate(const Unity::Vector3& v, const Unity::Quaternion& q)
    {
        const Unity::Vector3 u{q.x, q.y, q.z};
        const float dot = u.x * v.x + u.y * v.y + u.z * v.z;
        const float dotU = u.x * u.x + u.y * u.y + u.z * u.z;
        const Unity::Vector3 cross{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        return u * (2.0f * dot) + v * (q.w * q.w - dotU) + cross * (2.0f * q.w);
    }

    bool ReadTransformWorld(const MemoryClient& memory, std::uint64_t verticesAddress, std::uint64_t indicesAddress, std::int32_t index, Unity::Vector3& result)
    {
        if (!Unity::IsValidAddress(verticesAddress) || !Unity::IsValidAddress(indicesAddress) || index < 0 || index > 128000)
            return false;
        const auto count = static_cast<std::size_t>(index) + 1;
        std::vector<Unity::TrsX> vertices(count);
        std::vector<std::int32_t> parents(count);
        if (!memory.Read(verticesAddress, vertices.data(), vertices.size() * sizeof(Unity::TrsX), true) ||
            !memory.Read(indicesAddress, parents.data(), parents.size() * sizeof(std::int32_t), true))
            return false;
        Unity::Vector3 position = vertices[index].translation;
        std::int32_t parent = parents[index];
        for (int iteration = 0; parent >= 0 && parent < static_cast<std::int32_t>(count) && iteration < 4096; ++iteration)
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
} // namespace

bool WorldSource::ResolveWorld(std::string& error)
{
    using namespace ArenaOffsets;
    if (Special::TypeInfoTableRva == 0)
    {
        error = "TypeInfoTableRva is 0";
        return false;
    }
    const auto base = memory_.GetStatus().moduleBase;
    if (!Unity::IsValidAddress(base))
    {
        error = "GameAssembly module is not connected.";
        return false;
    }
    if (!Unity::IsValidAddress(ownerClass_))
    {
        std::uint64_t table = 0;
        if (!Pointer(memory_, base + Special::TypeInfoTableRva, table))
        {
            error = "TypeInfoTable pointer is unavailable.";
            return false;
        }
        if (typeIndex_ && Pointer(memory_, table + static_cast<std::uint64_t>(typeIndex_) * 8, ownerClass_))
        {
        }
        else
        {
            ownerClass_ = 0;
            
            for (std::uint32_t i = 0; i < 20000; ++i)
            {
                std::uint64_t candidate = 0, nameAddress = 0;
                if (!Pointer(memory_, table + static_cast<std::uint64_t>(i) * 8, candidate) || !Pointer(memory_, candidate + Il2CppClass::Name, nameAddress))
                    continue;
                char name[64]{};
                if (memory_.Read(nameAddress, name, sizeof(name)) && std::strncmp(name, "GamePlayerOwner", sizeof("GamePlayerOwner")) == 0)
                {
                    ownerClass_ = candidate;
                    typeIndex_ = i;
                    break;
                }
            }
        }
    }
    std::uint64_t statics = 0, player = 0, nextWorld = 0;
    if (!Pointer(memory_, ownerClass_ + Il2CppClass::StaticFields, statics) || !Pointer(memory_, statics + GamePlayerOwner::MyPlayer, player) ||
        !Pointer(memory_, player + Player::GameWorld, nextWorld))
    {
        ownerClass_ = 0;
        error = "GamePlayerOwner chain is not ready.";
        return false;
    }
    if (world_ != nextWorld)
    {
        tracked_.clear();
        lastRegisteredPlayerCount_ = 0;
        world_ = nextWorld;
    }
    return true;
}

bool WorldSource::ReadManagedString(std::uint64_t address, std::string& value) const
{
    value.clear();
    std::int32_t length = 0;
    if (!Unity::IsValidAddress(address) || !memory_.TryRead(address + ArenaOffsets::UnityString::Length, length) || length < 0 || length > 64)
        return false;
    std::vector<char16_t> chars(length);
    if (length && !memory_.Read(address + ArenaOffsets::UnityString::Data, chars.data(), length * sizeof(char16_t)))
        return false;
    for (char16_t c : chars)
        value.push_back(c < 128 ? static_cast<char>(c) : '?');
    return true;
}

bool WorldSource::InitializeTransform(std::uint64_t player, Tracked& tracked) const
{
    using namespace ArenaOffsets;
    tracked.snapshot.transformStage = 1;
    std::uint64_t look = 0, internal = 0;
    const auto offset = tracked.local ? Player::PlayerLookRaycastTransform : ObservedPlayerView::PlayerLookRaycastTransform;
    if (!Pointer(memory_, player + offset, look))
        return false;
    tracked.snapshot.transformStage = 2;
    if (!Pointer(memory_, look + 0x10, internal))
        return false;
    tracked.snapshot.transformStage = 3;
    Unity::TransformAccessInfo transform;
    if (!Unity::ResolveTransformAccess(memory_, internal, transform))
        return false;
    tracked.snapshot.transformStage = 4;
    tracked.snapshot.transformStage = 5;
    tracked.snapshot.transformIndex = static_cast<std::uint32_t>(transform.index);
    tracked.transformVerticesAddress = transform.vertices;
    tracked.transformIndicesAddress = transform.indices;
    tracked.transformIndex = transform.index;
    tracked.positionAddress = transform.hasCachedWorldPosition ? transform.hierarchy + UnityOffsets::TransformHierarchy::WorldPosition : 0;
    tracked.snapshot.transformStage = 6;
    return true;
}

bool WorldSource::InitializeRotation(std::uint64_t player, Tracked& tracked) const
{
    using namespace ArenaOffsets;
    std::uint64_t context = 0;
    if (tracked.local)
    {
        if (!Pointer(memory_, player + Player::MovementContext, context))
            return false;
        tracked.rotationAddress = context + MovementContext::Rotation;
    }
    else
    {
        std::uint64_t controller = 0, movement = 0;
        if (!Pointer(memory_, player + ObservedPlayerView::ObservedPlayerController, controller) ||
            !Pointer(memory_, controller + ObservedPlayerController::MovementController, movement) ||
            !Pointer(memory_, movement + ObservedMovementController::StateContext, context))
            return false;
        tracked.rotationAddress = context + ObservedPlayerStateContext::Rotation;
    }
    Unity::Vector2 initial{};
    return memory_.TryRead(tracked.rotationAddress, initial) && std::isfinite(initial.x) && std::isfinite(initial.y);
}

int WorldSource::ResolveTeamId(std::uint64_t player, bool local) const
{
    using namespace ArenaOffsets;
    std::uint64_t controller = 0;
    if (local)
    {
        if (!Pointer(memory_, player + Player::InventoryController, controller))
            return -1;
    }
    else
    {
        std::uint64_t observedController = 0;
        if (!Pointer(memory_, player + ObservedPlayerView::ObservedPlayerController, observedController) ||
            !Pointer(memory_, observedController + ObservedPlayerController::InventoryController, controller))
            return -1;
    }
    std::uint64_t inventory = 0, equipment = 0, slots = 0;
    if (!Pointer(memory_, controller + InventoryController::Inventory, inventory) || !Pointer(memory_, inventory + Inventory::Equipment, equipment) ||
        !Pointer(memory_, equipment + CompoundItem::Slots, slots))
        return -1;
    std::vector<std::uint64_t> slotPointers;
    if (!Unity::Collections::ReadArray(memory_, slots, slotPointers, 64))
        return -1;
    for (const auto slot : slotPointers)
    {
        std::uint64_t nameAddress = 0;
        std::string name;
        if (!Unity::IsValidAddress(slot) || !Pointer(memory_, slot + Slot::Id, nameAddress) || !ReadManagedString(nameAddress, name) || name != "ArmBand")
            continue;
        std::uint64_t item = 0, itemTemplate = 0, guidAddress = 0;
        if (!Pointer(memory_, slot + Slot::ContainedItem, item) || !Pointer(memory_, item + LootItem::Template, itemTemplate) ||
            !Pointer(memory_, itemTemplate + ItemTemplate::Id + 0x10, guidAddress))
            return -1;
        std::string guid;
        if (!ReadManagedString(guidAddress, guid))
            return -1;
        return ArenaTeams::FromArmbandGuid(guid);
    }
    return -1;
}

void WorldSource::ReadRealtime()
{
    std::vector<MemoryClient::ScatterEntry> reads;
    reads.reserve(tracked_.size() * 2);
    for (auto& [address, tracked] : tracked_)
    {
        if (!tracked.snapshot.active)
            continue;
        tracked.position = {NAN, NAN, NAN};
        tracked.rotation = {NAN, NAN};
        if (tracked.positionAddress)
            reads.push_back({tracked.positionAddress, &tracked.position, sizeof(tracked.position)});
        if (tracked.rotationAddress)
            reads.push_back({tracked.rotationAddress, &tracked.rotation, sizeof(tracked.rotation)});
    }
    if (!reads.empty())
        memory_.ReadScatter(reads, true);
    for (auto& [address, tracked] : tracked_)
    {
        if (!tracked.snapshot.active)
            continue;
        if ((tracked.local || !tracked.skeleton) && (!tracked.position.IsFinite() || tracked.position.y <= -500.0f || tracked.position.LengthSquared() == 0.0f) &&
            Unity::IsValidAddress(tracked.transformVerticesAddress))
            ReadTransformWorld(memory_, tracked.transformVerticesAddress, tracked.transformIndicesAddress, tracked.transformIndex, tracked.position);
        if (std::isfinite(tracked.rotation.x) && std::isfinite(tracked.rotation.y))
        {
            tracked.snapshot.yaw = tracked.rotation.x;
            tracked.snapshot.pitch = tracked.rotation.y;
        }
        const auto& p = tracked.position;
        const bool finite = p.IsFinite() && std::abs(p.x) < 4096 && std::abs(p.y) < 4096 && std::abs(p.z) < 4096;
        if (finite && p.y > -500 && p.LengthSquared() > 0)
        {
            tracked.snapshot.x = p.x;
            tracked.snapshot.y = p.y;
            tracked.snapshot.z = p.z;
            tracked.snapshot.hasPosition = true;
            tracked.snapshot.positionFromBones = false;
            if (tracked.usingBonePosition)
                Log::Write("World: cached transform recovered for " + tracked.snapshot.name);
            tracked.usingBonePosition = false;
            tracked.established = true;
        }
    }
}

bool WorldSource::ReadPlayers(std::vector<PlayerSnapshot>& out, std::string& error)
{
    out.clear();
    if (memory_.GetStatus().state != MemoryConnectionState::Connected)
    {
        tracked_.clear();
        world_ = 0;
        publishedWorld_ = 0;
        localPlayer_ = 0;
        publishedLocalPlayer_ = 0;
        inRaid_ = false;
        lastActivePlayerListAt_ = {};
        lastRegisteredPlayerCount_ = 0;
        error = "Connect PCLeech first.";
        return false;
    }
    constexpr auto RaidStateRetention = std::chrono::seconds(10);
    const auto readNow = std::chrono::steady_clock::now();
    const auto retainRaidState = [&]
    {
        return inRaid_.load(std::memory_order_acquire) && lastActivePlayerListAt_.time_since_epoch().count() != 0 && readNow - lastActivePlayerListAt_ < RaidStateRetention;
    };
    const auto clearRaidState = [&]
    {
        inRaid_ = false;
        publishedWorld_ = 0;
        localPlayer_ = 0;
        publishedLocalPlayer_ = 0;
        world_ = 0;
        tracked_.clear();
        lastActivePlayerListAt_ = {};
        lastRegisteredPlayerCount_ = 0;
    };
    if (!ResolveWorld(error))
    {
        if (!retainRaidState())
            clearRaidState();
        return false;
    }
    publishedWorld_ = world_;
    std::uint64_t list = 0;
    if (!Pointer(memory_, world_ + ArenaOffsets::ClientLocalGameWorld::RegisteredPlayers, list, true))
    {
        if (!retainRaidState())
            clearRaidState();
        error = "RegisteredPlayers list is unavailable.";
        return false;
    }
    std::vector<std::uint64_t> pointers;
    if (!Unity::Collections::ReadList(memory_, list, pointers, 64, true))
    {
        if (!retainRaidState())
            clearRaidState();
        error = "RegisteredPlayers list read failed or count exceeds 64.";
        return false;
    }
    if (pointers.empty())
    {
        if (!retainRaidState())
            clearRaidState();
        error = "Waiting for active player list.";
        return false;
    }
    inRaid_ = true;
    lastActivePlayerListAt_ = readNow;
    const auto registeredCount = static_cast<std::uint32_t>(pointers.size());
    if (registeredCount != lastRegisteredPlayerCount_)
    {
        Log::Write("World: registered player count changed from " + std::to_string(lastRegisteredPlayerCount_) + " to " + std::to_string(registeredCount) + ".");
        lastRegisteredPlayerCount_ = registeredCount;
    }
    std::uint64_t nextLocalPlayer = 0;
    if (memory_.TryRead(world_ + ArenaOffsets::ClientLocalGameWorld::MainPlayer, nextLocalPlayer, true))
    {
        if (!Unity::IsValidAddress(nextLocalPlayer))
            nextLocalPlayer = 0;
        if (nextLocalPlayer != localPlayer_)
        {
            Log::Write("World: local player changed; refreshing respawn state.");
            localPlayer_ = nextLocalPlayer;
            publishedLocalPlayer_.store(localPlayer_, std::memory_order_release);
        }
    }
    std::unordered_set<std::uint64_t> seen;
    if (Unity::IsValidAddress(localPlayer_))
        pointers.push_back(localPlayer_);
    for (const auto player : pointers)
    {
        if (!Unity::IsValidAddress(player) || !seen.insert(player).second)
            continue;
        auto& tracked = tracked_[player];
        const bool local = player == localPlayer_;
        if (tracked.snapshot.id == 0 || tracked.local != local)
        {
            tracked = {};
            tracked.local = local;
            tracked.snapshot.id = player;
            tracked.snapshot.name = local ? "LocalPlayer" : "Player";
            if (!local)
            {
                std::uint64_t name = 0;
                if (Pointer(memory_, player + ArenaOffsets::ObservedPlayerView::NickName, name))
                    ReadManagedString(name, tracked.snapshot.name);
                if (tracked.snapshot.name.empty())
                    tracked.snapshot.name = "Player";
                std::int32_t side = 0;
                memory_.TryRead(player + ArenaOffsets::ObservedPlayerView::Side, side);
                tracked.snapshot.side = static_cast<std::uint32_t>((std::max)(0, side));
                tracked.classificationReady = memory_.TryRead(player + ArenaOffsets::ObservedPlayerView::IsAI, tracked.snapshot.isAI, true);
            }
        }
        if (!local && !tracked.classificationReady)
            tracked.classificationReady = memory_.TryRead(player + ArenaOffsets::ObservedPlayerView::IsAI, tracked.snapshot.isAI, true);
        if (!local && (tracked.snapshot.name.empty() || tracked.snapshot.name == "Player"))
        {
            std::uint64_t name = 0;
            std::string resolvedName;
            if (Pointer(memory_, player + ArenaOffsets::ObservedPlayerView::NickName, name) && ReadManagedString(name, resolvedName) && !resolvedName.empty())
                tracked.snapshot.name = std::move(resolvedName);
        }
        tracked.snapshot.active = true;
        tracked.snapshot.local = local;
        if (tracked.missingSince.time_since_epoch().count() != 0)
            Log::Write("World: player returned to registered list: " + tracked.snapshot.name);
        tracked.missingSince = {};
        if (!tracked.positionAddress && !tracked.transformVerticesAddress)
            InitializeTransform(player, tracked);
        if (!tracked.rotationAddress)
            InitializeRotation(player, tracked);
        if (!tracked.snapshot.isAI && tracked.snapshot.teamId < 0 && sampleTick_ >= tracked.nextTeamTick)
        {
            tracked.snapshot.teamId = ResolveTeamId(player, local);
            tracked.nextTeamTick = sampleTick_ + 10;
        }
    }
    for (auto it = tracked_.begin(); it != tracked_.end();)
    {
        const bool listed = seen.contains(it->first);
        if (!listed)
        {
            constexpr auto MissingPlayerRetention = std::chrono::seconds(2);
            if (it->second.missingSince.time_since_epoch().count() == 0)
            {
                it->second.missingSince = readNow;
                Log::Write("World: retaining player missing from registered list: " + it->second.snapshot.name);
            }
            if (readNow - it->second.missingSince >= MissingPlayerRetention)
            {
                Log::Write("World: removing player absent from registered list: " + it->second.snapshot.name);
                it = tracked_.erase(it);
            }
            else
            {
                // Retain cached state briefly in case the list sample was transient, but do not
                // publish or update stale runtime pointers as an active player.
                it->second.snapshot.active = false;
                ++it;
            }
        }
        else
        {
            it->second.snapshot.active = true;
            ++it;
        }
    }
    ReadRealtime();
    ++sampleTick_;

    const auto refreshBoneSummary = [](Tracked& tracked)
    {
        std::uint32_t retainedBones = 0;
        for (const auto& bone : tracked.snapshot.bones)
            if (bone.valid)
                ++retainedBones;
        tracked.snapshot.readyBones = retainedBones;
        const auto& head = tracked.snapshot.bones[0];
        tracked.snapshot.hasHead = head.valid;
        if (head.valid)
        {
            tracked.snapshot.headX = head.position.x;
            tracked.snapshot.headY = head.position.y;
            tracked.snapshot.headZ = head.position.z;
        }
    };

    std::vector<MemoryClient::ScatterEntry> boneReads;
    std::vector<Tracked*> skeletonTargets;
    boneReads.reserve(tracked_.size() * 2);
    skeletonTargets.reserve(tracked_.size());
    for (auto& [address, tracked] : tracked_)
    {
        if (!tracked.snapshot.active || tracked.local)
            continue;
        if (!tracked.skeleton)
            tracked.skeleton = std::make_unique<Unity::Skeleton>(memory_);
        if (tracked.skeleton->Initialize(address) && tracked.skeleton->PrepareBones(boneReads))
        {
            skeletonTargets.push_back(&tracked);
            continue;
        }
        tracked.snapshot.skeletonStage = tracked.skeleton ? tracked.skeleton->Stage() : 0;
        refreshBoneSummary(tracked);
    }

    std::vector<bool> completedBoneReads;
    memory_.ReadScatter(boneReads, true, &completedBoneReads);

    for (Tracked* target : skeletonTargets)
    {
        auto& tracked = *target;
        std::array<Unity::BoneSample, Unity::SkeletonBones.size()> freshBones{};
        const bool skeletonReady = tracked.skeleton->FinishBones(freshBones, completedBoneReads);
        if (skeletonReady)
        {
            for (std::size_t index = 0; index < freshBones.size(); ++index)
            {
                const auto& fresh = freshBones[index];
                if (!fresh.valid || !fresh.position.IsFinite() || std::abs(fresh.position.x) >= 4096.0f || std::abs(fresh.position.y) >= 4096.0f ||
                    std::abs(fresh.position.z) >= 4096.0f)
                    continue;
                tracked.snapshot.bones[index] = fresh;
            }
        }
        tracked.snapshot.skeletonStage = tracked.skeleton->Stage();
        const auto& skeletonDiagnostics = tracked.skeleton->GetDiagnostics();
        tracked.snapshot.skeletonComponents = skeletonDiagnostics.components;
        tracked.snapshot.skeletonHierarchies = skeletonDiagnostics.hierarchies;
        tracked.snapshot.skeletonIndices = skeletonDiagnostics.indices;
        tracked.snapshot.skeletonArrays = skeletonDiagnostics.arrays;
        tracked.snapshot.skeletonReadyGroups = skeletonDiagnostics.readyGroups;
        tracked.snapshot.skeletonMaxIndex = skeletonDiagnostics.maxIndex;
        refreshBoneSummary(tracked);

        constexpr std::size_t pelvisIndex = 5;
        constexpr std::size_t leftFootIndex = 14;
        constexpr std::size_t rightFootIndex = 15;
        const auto& leftFoot = tracked.snapshot.bones[leftFootIndex];
        const auto& rightFoot = tracked.snapshot.bones[rightFootIndex];
        const auto& pelvis = tracked.snapshot.bones[pelvisIndex];
        Unity::Vector3 bonePosition{};
        bool hasBonePosition = false;
        if (leftFoot.valid && rightFoot.valid)
        {
            bonePosition = leftFoot.position.y < rightFoot.position.y ? leftFoot.position : rightFoot.position;
            hasBonePosition = true;
        }
        else if (leftFoot.valid)
        {
            bonePosition = leftFoot.position;
            hasBonePosition = true;
        }
        else if (rightFoot.valid)
        {
            bonePosition = rightFoot.position;
            hasBonePosition = true;
        }
        else if (pelvis.valid)
        {
            bonePosition = {pelvis.position.x, pelvis.position.y - 0.95f, pelvis.position.z};
            hasBonePosition = true;
        }

        if (hasBonePosition && bonePosition.IsFinite() && bonePosition.y > -500.0f && bonePosition.LengthSquared() >= 1.0f)
        {
            const bool cachedMissing = !tracked.snapshot.hasPosition || !tracked.established;
            if (cachedMissing)
            {
                tracked.snapshot.x = bonePosition.x;
                tracked.snapshot.y = bonePosition.y;
                tracked.snapshot.z = bonePosition.z;
                tracked.snapshot.hasPosition = true;
                tracked.snapshot.positionFromBones = true;
                if (!tracked.usingBonePosition)
                    Log::Write("World: using bone-derived position for " + tracked.snapshot.name);
                tracked.usingBonePosition = true;
            }
        }
    }
    out.reserve(tracked_.size());
    for (const auto& [address, tracked] : tracked_)
        out.push_back(tracked.snapshot);
    error.clear();
    return true;
}
