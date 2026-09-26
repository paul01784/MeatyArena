#include "RuntimeResolver.h"

#include "Offsets.h"
#include "TargetProfile.h"
#include "../Core/Logging/Log.h"
#include "../Memory/MemoryClient.h"
#include "../Unity/Types.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr std::size_t MaxNameLength = 128;

    struct ClassNamePointers
    {
        std::uint64_t name = 0, nameSpace = 0;
    };
    struct RawFieldInfo
    {
        std::uint64_t name = 0;
        std::uint64_t type = 0;
        std::uint64_t parent = 0;
        std::int32_t offset = 0;
        std::uint32_t token = 0;
    };
    static_assert(sizeof(RawFieldInfo) == 0x20);

    struct RawMethodInfo
    {
        std::uint64_t methodPointer = 0;
        std::array<std::byte, 0x10> unused{};
        std::uint64_t name = 0;
    };
    static_assert(sizeof(RawMethodInfo) == 0x20);

    bool Pointer(const MemoryClient& memory, std::uint64_t address, std::uint64_t& value)
    {
        return memory.TryRead(address, value, true) && Unity::IsValidAddress(value);
    }

    std::string BufferString(const std::array<char, MaxNameLength>& buffer)
    {
        const auto end = std::find(buffer.begin(), buffer.end(), '\0');
        if (end == buffer.begin() || end == buffer.end())
            return {};
        for (auto it = buffer.begin(); it != end; ++it)
        {
            const auto c = static_cast<unsigned char>(*it);
            if (c < 0x20 || c > 0x7E)
                return {};
        }
        return std::string(buffer.begin(), end);
    }

    bool ReadCString(const MemoryClient& memory, std::uint64_t address, std::string& value)
    {
        value.clear();
        if (!Unity::IsValidAddress(address))
            return false;
        std::array<char, MaxNameLength> buffer{};
        if (!memory.Read(address, buffer.data(), buffer.size(), true))
            return false;
        value = BufferString(buffer);
        return !value.empty();
    }

    bool IsValidClass(const MemoryClient& memory, std::uint64_t klass)
    {
        std::uint64_t name = 0;
        std::string text;
        return Pointer(memory, klass + ArenaOffsets::Il2CppClass::Name, name) && ReadCString(memory, name, text);
    }

    bool ProbeTable(const MemoryClient& memory, std::uint64_t table, std::size_t start, std::size_t count, std::size_t required)
    {
        std::vector<std::uint64_t> pointers(count);
        if (!memory.Read(table + start * sizeof(std::uint64_t), pointers.data(), pointers.size() * sizeof(std::uint64_t), true))
            return false;
        std::size_t valid = 0;
        for (const auto pointer : pointers)
            if (IsValidClass(memory, pointer) && ++valid >= required)
                return true;
        return false;
    }

    bool ValidateTypeInfoTable(const MemoryClient& memory, std::uint64_t base, std::uint64_t rva)
    {
        std::uint64_t table = 0;
        return rva && Pointer(memory, base + rva, table) && ProbeTable(memory, table, 0, 16, 8) && ProbeTable(memory, table, 5000, 8, 3);
    }

    std::uint64_t DecodeRipRelativeRva(const MemoryClient& memory, std::uint64_t instruction, std::uint32_t displacementOffset, std::uint32_t instructionLength,
                                       std::uint64_t moduleBase, std::uint64_t moduleSize)
    {
        std::int32_t displacement = 0;
        if (!memory.TryRead(instruction + displacementOffset, displacement, true))
            return 0;
        const auto target = static_cast<std::uint64_t>(static_cast<std::int64_t>(instruction + instructionLength) + displacement);
        if (target <= moduleBase || target >= moduleBase + moduleSize)
            return 0;
        return target - moduleBase;
    }

    bool ResolveTypeInfoTable(MemoryClient& memory, std::uint64_t base, std::uint64_t size)
    {
        struct Signature
        {
            const char* bytes;
            std::uint32_t relOffset;
            std::uint32_t length;
            std::size_t limit;
        };
        constexpr Signature primary[] = {
            {"48 8B 05 ? ? ? ? ? ? ? ? ? ? ? 90 48 85 DB 75 ? 48 8D 2D ? ? ? ? 48 89 6C 24 ? 48 8B CD E8 ? ? ? ? 90 ? ? ? 48 85 DB 75 ? 8B CF", 3, 7, 64},
            {"48 8B 0D ? ? ? ? 48 63 D0 ? ? ? ? 48 85 C9 74", 3, 7, 64},
            {"48 89 05 ? ? ? ? 4C 8B 05 ? ? ? ? 48 8B 05 ? ? ? ? 48 63 48 ? BA ? ? ? ? 41 FF D0 48 89 05 ? ? ? ?", 3, 7, 64},
        };
        
        constexpr Signature lastResort[] = {
            {"48 8B 05 ? ? ? ?", 3, 7, 20000}, {"48 8B 0D ? ? ? ?", 3, 7, 20000}, {"48 8B 15 ? ? ? ?", 3, 7, 20000}, {"4C 8B 05 ? ? ? ?", 3, 7, 20000},
            {"4C 8B 0D ? ? ? ?", 3, 7, 20000}, {"48 89 05 ? ? ? ?", 3, 7, 20000}, {"48 89 0D ? ? ? ?", 3, 7, 20000}, {"4C 89 05 ? ? ? ?", 3, 7, 20000},
        };

        std::vector<std::uint64_t> candidates;
        std::unordered_set<std::uint64_t> seen;
        const auto scan = [&](const auto& signatures)
        {
            for (const auto& signature : signatures)
                for (const auto address : memory.FindSignatures(TargetProfile::GameAssemblyModule, signature.bytes, signature.limit))
                {
                    const auto rva = DecodeRipRelativeRva(memory, address, signature.relOffset, signature.length, base, size);
                    if (rva && seen.insert(rva).second)
                        candidates.push_back(rva);
                }
        };
        scan(primary);
        if (candidates.empty())
            scan(lastResort);
        Log::Write("Resolver: TypeInfo scan produced " + std::to_string(candidates.size()) + " candidate globals.");
        if (candidates.empty())
            return false;

        for (int attempt = 0; attempt < 30; ++attempt)
        {
            for (const auto rva : candidates)
            {
                if (!ValidateTypeInfoTable(memory, base, rva))
                    continue;
                ArenaOffsets::Special::TypeInfoTableRva = rva;
                Log::Write("Resolver: TypeInfoTable RVA = 0x" +
                           [&]
                           {
                               char value[24]{};
                               std::snprintf(value, sizeof(value), "%llX", static_cast<unsigned long long>(rva));
                               return std::string(value);
                           }());
                return true;
            }
            std::this_thread::sleep_for(attempt < 10 ? std::chrono::seconds(1) : std::chrono::seconds(2));
        }
        return false;
    }

    using ClassMap = std::unordered_map<std::string, std::uint64_t>;

    ClassMap ReadTargetClasses(const MemoryClient& memory, std::uint64_t table)
    {
        const std::unordered_set<std::string> targetNames = {"GamePlayerOwner",
                                                             "GameWorld",
                                                             "ObservedPlayerView",
                                                             "ObservedPlayerController",
                                                             "InventoryController",
                                                             "Inventory",
                                                             "CompoundItem",
                                                             "Slot",
                                                             "Item",
                                                             "ItemTemplate",
                                                             "Player",
                                                             "FirearmController",
                                                             "Firearms",
                                                             "BifacialTransform",
                                                             "PlayerBones",
                                                             "ProceduralWeaponAnimation",
                                                             "PlayerSpring",
                                                             "MovementContext",
                                                             "ObservedPlayerMovementController",
                                                             "ObservedPlayerStateContext",
                                                             "PlayerBody",
                                                             "Skeleton",
                                                             "CameraManager",
                                                             "OpticCameraManager"};
        ClassMap classes;
        constexpr std::size_t chunkSize = 2048;
        constexpr std::size_t maxClasses = 50000;
        for (std::size_t offset = 0; offset < maxClasses; offset += chunkSize)
        {
            std::vector<std::uint64_t> pointers((std::min)(chunkSize, maxClasses - offset));
            if (!memory.Read(table + offset * 8, pointers.data(), pointers.size() * sizeof(std::uint64_t), true))
                break;
            std::vector<std::size_t> valid;
            valid.reserve(pointers.size());
            for (std::size_t i = 0; i < pointers.size(); ++i)
                if (Unity::IsValidAddress(pointers[i]))
                    valid.push_back(i);
            if (valid.empty())
                break;

            std::vector<ClassNamePointers> headers(valid.size());
            std::vector<MemoryClient::ScatterEntry> headerReads;
            headerReads.reserve(valid.size());
            for (std::size_t i = 0; i < valid.size(); ++i)
                headerReads.push_back({pointers[valid[i]] + ArenaOffsets::Il2CppClass::Name, &headers[i], sizeof(headers[i])});
            memory.ReadScatter(headerReads, true);

            std::vector<std::array<char, MaxNameLength>> names(valid.size()), namespaces(valid.size());
            std::vector<MemoryClient::ScatterEntry> stringReads;
            stringReads.reserve(valid.size() * 2);
            for (std::size_t i = 0; i < valid.size(); ++i)
            {
                if (Unity::IsValidAddress(headers[i].name))
                    stringReads.push_back({headers[i].name, names[i].data(), static_cast<std::uint32_t>(names[i].size())});
                if (Unity::IsValidAddress(headers[i].nameSpace))
                    stringReads.push_back({headers[i].nameSpace, namespaces[i].data(), static_cast<std::uint32_t>(namespaces[i].size())});
            }
            memory.ReadScatter(stringReads, true);
            for (std::size_t i = 0; i < valid.size(); ++i)
            {
                const auto name = BufferString(names[i]);
                if (!targetNames.contains(name))
                    continue;
                const auto nameSpace = BufferString(namespaces[i]);
                classes.try_emplace(name, pointers[valid[i]]);
                if (!nameSpace.empty())
                    classes.try_emplace(nameSpace + "." + name, pointers[valid[i]]);
                if (name == "GamePlayerOwner")
                    ArenaOffsets::Special::GamePlayerOwnerTypeIndex = static_cast<std::uint32_t>(offset + valid[i]);
            }
        }
        return classes;
    }

    std::unordered_map<std::string, std::uint32_t> ReadFields(const MemoryClient& memory, std::uint64_t klass)
    {
        std::unordered_map<std::string, std::uint32_t> result;
        std::uint16_t count = 0;
        std::uint64_t fields = 0;
        if (!memory.TryRead(klass + ArenaOffsets::Il2CppClass::FieldCount, count, true) || count == 0 || count > 4096 ||
            !Pointer(memory, klass + ArenaOffsets::Il2CppClass::Fields, fields))
            return result;
        std::vector<RawFieldInfo> raw(count);
        if (!memory.Read(fields, raw.data(), raw.size() * sizeof(RawFieldInfo), true))
            return result;
        std::vector<std::array<char, MaxNameLength>> names(count);
        std::vector<MemoryClient::ScatterEntry> reads;
        for (std::size_t i = 0; i < raw.size(); ++i)
            if (Unity::IsValidAddress(raw[i].name))
                reads.push_back({raw[i].name, names[i].data(), static_cast<std::uint32_t>(names[i].size())});
        memory.ReadScatter(reads, true);
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const auto name = BufferString(names[i]);
            if (!name.empty() && raw[i].offset >= 0 && raw[i].offset < 0x4000)
                result.try_emplace(name, static_cast<std::uint32_t>(raw[i].offset));
        }
        return result;
    }

    std::unordered_map<std::string, std::uint32_t> ReadMethods(const MemoryClient& memory, std::uint64_t klass, std::uint64_t moduleBase, std::uint64_t moduleSize)
    {
        std::unordered_map<std::string, std::uint32_t> result;
        std::uint16_t count = 0;
        std::uint64_t methods = 0;
        if (!memory.TryRead(klass + ArenaOffsets::Il2CppClass::MethodCount, count, true) || count == 0 || count > 4096 ||
            !Pointer(memory, klass + ArenaOffsets::Il2CppClass::Methods, methods))
            return result;
        std::vector<std::uint64_t> methodPointers(count);
        if (!memory.Read(methods, methodPointers.data(), methodPointers.size() * sizeof(std::uint64_t), true))
            return result;
        std::vector<RawMethodInfo> info(count);
        std::vector<MemoryClient::ScatterEntry> infoReads;
        for (std::size_t i = 0; i < methodPointers.size(); ++i)
            if (Unity::IsValidAddress(methodPointers[i]))
                infoReads.push_back({methodPointers[i], &info[i], sizeof(info[i])});
        memory.ReadScatter(infoReads, true);
        std::vector<std::array<char, MaxNameLength>> names(count);
        std::vector<MemoryClient::ScatterEntry> nameReads;
        for (std::size_t i = 0; i < info.size(); ++i)
            if (Unity::IsValidAddress(info[i].name))
                nameReads.push_back({info[i].name, names[i].data(), static_cast<std::uint32_t>(names[i].size())});
        memory.ReadScatter(nameReads, true);
        for (std::size_t i = 0; i < info.size(); ++i)
        {
            if (info[i].methodPointer < moduleBase || info[i].methodPointer >= moduleBase + moduleSize)
                continue;
            const auto name = BufferString(names[i]);
            if (!name.empty())
                result.try_emplace(name, static_cast<std::uint32_t>(info[i].methodPointer - moduleBase));
        }
        return result;
    }

    std::uint64_t FindClass(const ClassMap& classes, std::initializer_list<const char*> names)
    {
        for (const auto name : names)
        {
            const auto it = classes.find(name);
            if (it != classes.end())
                return it->second;
        }
        return 0;
    }

    bool TryField(const std::unordered_map<std::string, std::uint32_t>& fields, std::uint32_t& target, std::initializer_list<const char*> names)
    {
        for (const auto name : names)
        {
            const auto it = fields.find(name);
            if (it == fields.end())
                continue;
            target = it->second;
            return true;
        }
        return false;
    }

    int ResolveManagedOffsets(const MemoryClient& memory, const ClassMap& classes, std::uint64_t moduleBase, std::uint64_t moduleSize)
    {
        int updated = 0;
        std::unordered_map<std::uint64_t, std::unordered_map<std::string, std::uint32_t>> fieldCache;
        const auto apply = [&](std::initializer_list<const char*> classNames, std::uint32_t& target, std::initializer_list<const char*> fieldNames)
        {
            const auto klass = FindClass(classes, classNames);
            if (!klass)
                return;
            const auto [it, inserted] = fieldCache.try_emplace(klass);
            if (inserted)
                it->second = ReadFields(memory, klass);
            if (TryField(it->second, target, fieldNames))
                ++updated;
        };

        apply({"GamePlayerOwner"}, ArenaOffsets::GamePlayerOwner::MyPlayer, {"_myPlayer"});
        apply({"GameWorld"}, ArenaOffsets::ClientLocalGameWorld::RegisteredPlayers, {"RegisteredPlayers", "<RegisteredPlayers>k__BackingField"});
        apply({"GameWorld"}, ArenaOffsets::ClientLocalGameWorld::MainPlayer, {"MainPlayer", "<MainPlayer>k__BackingField"});
        apply({"GameWorld"}, ArenaOffsets::ClientLocalGameWorld::LocationId, {"<LocationId>k__BackingField", "_LocationId_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerView", "ObservedPlayerView"}, ArenaOffsets::ObservedPlayerView::ObservedPlayerController,
              {"<ObservedPlayerController>k__BackingField", "_ObservedPlayerController_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerView", "ObservedPlayerView"}, ArenaOffsets::ObservedPlayerView::Side, {"<Side>k__BackingField", "_Side_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerView", "ObservedPlayerView"}, ArenaOffsets::ObservedPlayerView::IsAI, {"<IsAI>k__BackingField", "_IsAI_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerView", "ObservedPlayerView"}, ArenaOffsets::ObservedPlayerView::ProfileId,
              {"<ProfileId>k__BackingField", "_ProfileId_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerView", "ObservedPlayerView"}, ArenaOffsets::ObservedPlayerView::NickName,
              {"<NickName>k__BackingField", "_NickName_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerView", "ObservedPlayerView"}, ArenaOffsets::ObservedPlayerView::PlayerBody,
              {"<PlayerBody>k__BackingField", "_PlayerBody_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerView", "ObservedPlayerView"}, ArenaOffsets::ObservedPlayerView::PlayerLookRaycastTransform, {"_playerLookRaycastTransform"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerController", "ObservedPlayerController"}, ArenaOffsets::ObservedPlayerController::MovementController,
              {"<MovementController>k__BackingField", "_MovementController_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerController", "ObservedPlayerController"}, ArenaOffsets::ObservedPlayerController::InventoryController,
              {"<InventoryController>k__BackingField", "_InventoryController_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerMovementController", "ObservedPlayerMovementController"}, ArenaOffsets::ObservedMovementController::StateContext,
              {"<ObservedPlayerStateContext>k__BackingField", "_ObservedPlayerStateContext_k__BackingField"});
        apply({"EFT.NextObservedPlayer.ObservedPlayerStateContext", "ObservedPlayerStateContext"}, ArenaOffsets::ObservedPlayerStateContext::Rotation,
              {"<Rotation>k__BackingField", "_Rotation_k__BackingField"});
        apply({"EFT.InventoryLogic.InventoryController", "InventoryController"}, ArenaOffsets::InventoryController::Inventory,
              {"<Inventory>k__BackingField", "_Inventory_k__BackingField"});
        apply({"EFT.InventoryLogic.Inventory", "Inventory"}, ArenaOffsets::Inventory::Equipment, {"Equipment"});
        apply({"EFT.InventoryLogic.CompoundItem", "CompoundItem"}, ArenaOffsets::CompoundItem::Slots, {"Slots"});
        apply({"EFT.InventoryLogic.Slot", "Slot"}, ArenaOffsets::Slot::ContainedItem, {"<ContainedItem>k__BackingField", "_ContainedItem_k__BackingField"});
        apply({"EFT.InventoryLogic.Slot", "Slot"}, ArenaOffsets::Slot::Id, {"<ID>k__BackingField", "_ID_k__BackingField"});
        apply({"EFT.InventoryLogic.Item", "Item"}, ArenaOffsets::LootItem::Template, {"<Template>k__BackingField", "_Template_k__BackingField"});
        apply({"EFT.InventoryLogic.ItemTemplate", "ItemTemplate"}, ArenaOffsets::ItemTemplate::Id, {"<_id>k__BackingField", "__id_k__BackingField"});
        apply({"EFT.Player", "Player"}, ArenaOffsets::Player::GameWorld, {"GameWorld", "<GameWorld>k__BackingField"});
        apply({"EFT.Player", "Player"}, ArenaOffsets::Player::MovementContext, {"<MovementContext>k__BackingField", "_MovementContext_k__BackingField"});
        apply({"EFT.Player", "Player"}, ArenaOffsets::Player::PlayerLookRaycastTransform, {"_playerLookRaycastTransform"});
        apply({"EFT.Player", "Player"}, ArenaOffsets::Player::InventoryController, {"_inventoryController", "<_inventoryController>k__BackingField"});
        apply({"EFT.Player", "Player"}, ArenaOffsets::Player::ProceduralWeaponAnimation,
              {"<ProceduralWeaponAnimation>k__BackingField", "_ProceduralWeaponAnimation_k__BackingField", "ProceduralWeaponAnimation"});
        apply({"EFT.Player", "Player"}, ArenaOffsets::Player::HandsController, {"_handsController", "<HandsController>k__BackingField", "_HandsController_k__BackingField"});
        apply({"EFT.Player", "Player"}, ArenaOffsets::Player::PlayerBones, {"<PlayerBones>k__BackingField", "_PlayerBones_k__BackingField", "PlayerBones"});
        apply({"EFT.Player.FirearmController", "EFT.FirearmController", "FirearmController"}, ArenaOffsets::FirearmController::Firearms, {"Firearms", "_firearms"});
        apply({"EFT.Player.FirearmController", "EFT.FirearmController", "FirearmController"}, ArenaOffsets::FirearmController::GunBaseTransform,
              {"GunBaseTransform", "<GunBaseTransform>k__BackingField"});
        apply({"EFT.Player.FirearmController", "EFT.FirearmController", "FirearmController"}, ArenaOffsets::FirearmController::Fireport, {"Fireport", "<Fireport>k__BackingField"});
        apply({"EFT.Firearms", "Firearms"}, ArenaOffsets::Firearms::Fireport, {"_fireport", "Fireport", "<Fireport>k__BackingField"});
        apply({"EFT.BifacialTransform", "BifacialTransform"}, ArenaOffsets::BifacialTransform::Original, {"Original", "<Original>k__BackingField"});
        apply({"EFT.PlayerBones", "PlayerBones"}, ArenaOffsets::PlayerBones::Fireport, {"Fireport", "<Fireport>k__BackingField"});
        apply({"EFT.Animations.ProceduralWeaponAnimation", "ProceduralWeaponAnimation"}, ArenaOffsets::ProceduralWeaponAnimation::HandsContainer,
              {"HandsContainer", "<HandsContainer>k__BackingField"});
        apply({"EFT.Animations.PlayerSpring", "PlayerSpring"}, ArenaOffsets::PlayerSpring::Fireport, {"Fireport", "<Fireport>k__BackingField"});
        apply({"EFT.MovementContext", "MovementContext"}, ArenaOffsets::MovementContext::Rotation, {"_rotation"});
        apply({"EFT.PlayerBody", "PlayerBody"}, ArenaOffsets::PlayerBody::SkeletonRootJoint, {"SkeletonRootJoint", "<SkeletonRootJoint>k__BackingField"});
        apply({"Diz.Skinning.Skeleton", "Skeleton"}, ArenaOffsets::DizSkinningSkeleton::Values, {"<_values>k__BackingField", "_values"});
        apply({"EFT.CameraControl.OpticCameraManager", "OpticCameraManager"}, ArenaOffsets::OpticCameraManager::Camera, {"<Camera>k__BackingField", "_Camera_k__BackingField"});
        apply({"EFT.CameraControl.CameraManager", "CameraManager"}, ArenaOffsets::CameraManager::OpticCameraManager,
              {"<OpticCameraManager>k__BackingField", "_OpticCameraManager_k__BackingField"});
        apply({"EFT.CameraControl.CameraManager", "CameraManager"}, ArenaOffsets::CameraManager::Camera, {"<Camera>k__BackingField", "_Camera_k__BackingField"});

        const auto cameraManager = FindClass(classes, {"EFT.CameraControl.CameraManager", "CameraManager"});
        if (cameraManager)
        {
            ArenaOffsets::CameraManager::ClassAddress = cameraManager;
            const auto methods = ReadMethods(memory, cameraManager, moduleBase, moduleSize);
            const auto instance = methods.find("get_Instance");
            if (instance != methods.end())
            {
                ArenaOffsets::CameraManager::GetInstanceRva = instance->second;
                ++updated;
            }
        }
        return updated;
    }

    void ResolveUnityOffsets(const MemoryClient& memory)
    {
        std::uint64_t base = 0, size = 0;
        if (!memory.ModuleInfo(TargetProfile::UnityPlayerModule, base, size))
            return;
        const auto allCameras =
            memory.FindSignatures(TargetProfile::UnityPlayerModule, "48 8B 1D ? ? ? ? 48 8B 73 ? 48 8B 43 ? 48 FF C6 ? ? ? 48 3B F0 76 ? 48 8B CB E8 ? ? ? ? ? ? ? 48 83 C1", 16);
        for (const auto match : allCameras)
        {
            const auto rva = DecodeRipRelativeRva(memory, match, 3, 7, base, size);
            std::uint64_t list = 0, items = 0;
            std::int32_t count = 0;
            if (rva && Pointer(memory, base + rva, list) && Pointer(memory, list, items) && memory.TryRead(list + 8, count, true) && count >= 0 && count < 1024)
            {
                UnityOffsets::AllCamerasRva = static_cast<std::uint32_t>(rva);
                break;
            }
        }

        const auto vm = memory.FindSignatures(TargetProfile::UnityPlayerModule,
                                              "E8 ? ? ? ? 48 3B 58 ? 0F 83 ? ? ? ? ? ? ? 48 8D 0C 5D ? ? ? ? 48 03 CB ? ? ? ? E8 ? ? ? ? 4C 8B C7 49 FF C0 ? ? ? ? ? 75", 8);
        if (!vm.empty())
        {
            std::int32_t rel = 0;
            std::uint32_t offset = 0;
            if (memory.TryRead(vm.front() + 1, rel, true))
            {
                const auto target = static_cast<std::uint64_t>(static_cast<std::int64_t>(vm.front() + 5) + rel);
                if (memory.TryRead(target + 3, offset, true) && offset > 0 && offset < 0x1000)
                    UnityOffsets::Camera::ViewMatrix = offset;
            }
        }
        const auto fov = memory.FindSignatures(TargetProfile::UnityPlayerModule, "83 B9 ? ? ? ? 02 75 ? F3 0F 10 81 ? ? ? ? C3 F3 0F 10 81 ? ? ? ? C3", 8);
        if (!fov.empty())
        {
            std::uint32_t offset = 0;
            if (memory.TryRead(fov.front() + 22, offset, true) && offset > 0 && offset < 0x1000)
                UnityOffsets::Camera::Fov = offset;
        }
        const auto aspect = memory.FindSignatures(TargetProfile::UnityPlayerModule, "F3 0F 11 8B ? ? ? ? 83 BB ? ? ? ? 02 66 C7 83 ? ? ? ? 01 01", 8);
        if (!aspect.empty())
        {
            std::uint32_t offset = 0;
            if (memory.TryRead(aspect.front() + 4, offset, true) && offset > 0 && offset < 0x1000)
                UnityOffsets::Camera::AspectRatio = offset;
        }
    }
} // namespace

bool RuntimeResolver::Initialize(MemoryClient& memory, std::string& error)
{
    std::uint64_t gameAssembly = 0, gameAssemblySize = 0;
    if (!memory.ModuleInfo(TargetProfile::GameAssemblyModule, gameAssembly, gameAssemblySize))
    {
        error = "GameAssembly.dll is unavailable.";
        return false;
    }
    if (!ResolveTypeInfoTable(memory, gameAssembly, gameAssemblySize))
    {
        error = "TypeInfoTable signature resolution failed.";
        return false;
    }

    std::uint64_t table = 0;
    if (!Pointer(memory, gameAssembly + ArenaOffsets::Special::TypeInfoTableRva, table))
    {
        error = "The resolved TypeInfoTable pointer is not ready.";
        return false;
    }
    const auto classes = ReadTargetClasses(memory, table);
    if (!FindClass(classes, {"GamePlayerOwner"}) || !FindClass(classes, {"GameWorld"}))
    {
        error = "Required IL2CPP classes (GamePlayerOwner/GameWorld) were not found.";
        return false;
    }

    const int updated = ResolveManagedOffsets(memory, classes, gameAssembly, gameAssemblySize);
    ResolveUnityOffsets(memory);
    Log::Write("Resolver: live IL2CPP pass updated " + std::to_string(updated) + " offsets.");
    error.clear();
    return true;
}
