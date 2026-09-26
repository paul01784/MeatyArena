#pragma once

#include <cstdint>


// ============================================================================
// FIXED
// ============================================================================
namespace ArenaOffsets
{
    namespace Il2CppClass
    {
        inline constexpr std::uint32_t Name = 0x10;
        inline constexpr std::uint32_t Namespace = 0x18;
        inline constexpr std::uint32_t Parent = 0x58;
        inline constexpr std::uint32_t Fields = 0x80;
        inline constexpr std::uint32_t StaticFields = 0xB8;
        inline constexpr std::uint32_t Methods = 0x98;
        inline constexpr std::uint32_t MethodCount = 0x120;
        inline constexpr std::uint32_t FieldCount = 0x124;
    } // namespace Il2CppClass

    namespace ObservedPlayerView
    {
        inline constexpr std::uint32_t Voice = 0x40;
        inline constexpr std::uint32_t Id = 0x7C;
    } // namespace ObservedPlayerView
    namespace ObservedPlayerController
    {
        inline constexpr std::uint32_t HealthController = 0x120;
        inline constexpr std::uint32_t Culling = 0x100;
    } // namespace ObservedPlayerController
    namespace ObservedPlayerStateContext
    {
        inline constexpr std::uint32_t PlayerTransform = 0x98;
    }
    namespace PlayerBody
    {
        inline constexpr std::uint32_t PlayerBones = 0x28;
        inline constexpr std::uint32_t SkeletonHands = 0x38;
    } // namespace PlayerBody
    namespace TrsX
    {
        inline constexpr std::uint32_t Position = 0x90;
    }
    // Legacy GOM layout values. The current camera path does not use these
    namespace GameObjectManager
    {
        inline constexpr std::uint32_t LastTaggedNode = 0x0;
        inline constexpr std::uint32_t TaggedNodes = 0x8;
        inline constexpr std::uint32_t LastMainCameraTaggedNode = 0x38;
        inline constexpr std::uint32_t MainCameraTaggedNodes = 0x40;
    } // namespace GameObjectManager
    namespace TaggedObject
    {
        inline constexpr std::uint32_t Object = 0x10;
        inline constexpr std::uint32_t Next = 0x28;
    } // namespace TaggedObject
    namespace UnityString
    {
        inline constexpr std::uint32_t Length = 0x10;
        inline constexpr std::uint32_t Data = 0x14;
    } // namespace UnityString
    namespace AssemblyCSharp
    {
        inline constexpr std::uint32_t TypeStart = 0;
        inline constexpr std::uint32_t TypeCount = 0;
    } // namespace AssemblyCSharp
} // namespace ArenaOffsets

namespace UnityOffsets
{
    // Legacy GOM RVA. It is retained for reference but is not used currentl
    inline constexpr std::uint32_t GomFallbackRva = 0x21A4450;
    namespace ManagedList
    {
        inline constexpr std::uint32_t ItemsPtr = 0x10;
        inline constexpr std::uint32_t Count = 0x18;
    } // namespace ManagedList
    namespace ManagedArray
    {
        inline constexpr std::uint32_t FirstElement = 0x20;
        inline constexpr std::uint32_t ElementSize = 0x8;
    } // namespace ManagedArray
    namespace TransformAccess
    {
        inline constexpr std::uint32_t Hierarchy = 0x58;
        inline constexpr std::uint32_t Index = 0x60;
    } // namespace TransformAccess
    namespace TransformHierarchy
    {
        inline constexpr std::uint32_t Vertices = 0x50;
        inline constexpr std::uint32_t Indices = 0xA0;
        inline constexpr std::uint32_t WorldPosition = 0xB0;
        inline constexpr std::uint32_t WorldRotation = 0xC0;
    } // namespace TransformHierarchy
} // namespace UnityOffsets

// ============================================================================
// RUNTIME-RESOLVED WITH FALLBACK
// ============================================================================
namespace ArenaOffsets
{
    namespace ClientLocalGameWorld
    {
        inline std::uint32_t RegisteredPlayers = 0x1B8;
        inline std::uint32_t MainPlayer = 0x218;
        inline std::uint32_t LocationId = 0xD0;
    } // namespace ClientLocalGameWorld
    namespace ObservedPlayerView
    {
        inline std::uint32_t ObservedPlayerController = 0x28;
        inline std::uint32_t Side = 0x9C;
        inline std::uint32_t IsAI = 0xA8;
        inline std::uint32_t ProfileId = 0xB0;
        inline std::uint32_t NickName = 0xC0;
        inline std::uint32_t PlayerBody = 0xE0;
        inline std::uint32_t PlayerLookRaycastTransform = 0x110;
    } // namespace ObservedPlayerView
    namespace ObservedPlayerController
    {
        inline std::uint32_t MovementController = 0x110;
        inline std::uint32_t InventoryController = 0x10;
    } // namespace ObservedPlayerController
    namespace InventoryController
    {
        inline std::uint32_t Inventory = 0x120;
    }
    namespace Inventory
    {
        inline std::uint32_t Equipment = 0x18;
    }
    namespace CompoundItem
    {
        inline std::uint32_t Slots = 0x98;
    }
    namespace Slot
    {
        inline std::uint32_t ContainedItem = 0x58;
        inline std::uint32_t Id = 0x68;
    } // namespace Slot
    namespace LootItem
    {
        inline std::uint32_t Template = 0x78;
    }
    namespace ItemTemplate
    {
        inline std::uint32_t Id = 0x110;
    }
    namespace ObservedMovementController
    {
        inline std::uint32_t StateContext = 0xB0;
    }
    namespace ObservedPlayerStateContext
    {
        inline std::uint32_t Rotation = 0x20;
    }
    namespace GamePlayerOwner
    {
        inline std::uint32_t MyPlayer = 0x8;
    }
    namespace Player
    {
        inline std::uint32_t GameWorld = 0x640;
        inline std::uint32_t MovementContext = 0x70;
        inline std::uint32_t PlayerLookRaycastTransform = 0xA88;
        inline std::uint32_t InventoryController = 0x9E0;
    } 
    namespace FirearmController
    {
        inline std::uint32_t Firearms = 0xD0;
        inline std::uint32_t GunBaseTransform = 0x148;
        inline std::uint32_t Fireport = 0x150;
    }
    namespace Firearms
    {
        inline std::uint32_t Fireport = 0xD8;
    }
    namespace BifacialTransform
    {
        inline std::uint32_t Original = 0x10;
    }
    namespace PlayerBones
    {
        inline std::uint32_t Fireport = 0x1D0;
    }
    namespace ProceduralWeaponAnimation
    {
        inline std::uint32_t HandsContainer = 0x38;
    }
    namespace PlayerSpring
    {
        inline std::uint32_t Fireport = 0x88;
    }
    namespace MovementContext
    {
        inline std::uint32_t Rotation = 0xD4;
    }
    namespace PlayerBody
    {
        inline std::uint32_t SkeletonRootJoint = 0x30;
    }
    namespace DizSkinningSkeleton
    {
        inline std::uint32_t Values = 0x30;
    }
    namespace CameraManager
    {
        inline std::uint32_t OpticCameraManager = 0x10;
        inline std::uint32_t Camera = 0x68;
        inline std::uint32_t GetInstanceRva = 0x1BCA160;
    } 
    namespace OpticCameraManager
    {
        inline std::uint32_t Camera = 0x70;
    }
} // namespace ArenaOffsets

namespace UnityOffsets
{
    inline std::uint32_t AllCamerasRva = 0x19F3080;
    namespace Camera
    {
        inline std::uint32_t ViewMatrix = 0x88;
        inline std::uint32_t Fov = 0x188;
        inline std::uint32_t AspectRatio = 0x4F8;
    } 
} // namespace UnityOffsets

// ============================================================================
// RUNTIME-ONLY
// ============================================================================
namespace ArenaOffsets
{
    namespace Special
    {
        inline std::uint64_t TypeInfoTableRva = 0;
        inline std::uint32_t GamePlayerOwnerTypeIndex = 0;
    }
    namespace Player
    {
        inline std::uint32_t ProceduralWeaponAnimation = 0;
        inline std::uint32_t HandsController = 0;
        inline std::uint32_t PlayerBones = 0;
    }
    namespace CameraManager
    {
        inline std::uint64_t ClassAddress = 0;
    }
} // namespace ArenaOffsets

namespace UnityOffsets
{
    // Discovered by the AllCameras fallback
    inline std::uint32_t GoObjectClass = 0;
    inline std::uint32_t GoName = 0;
} // namespace UnityOffsets
