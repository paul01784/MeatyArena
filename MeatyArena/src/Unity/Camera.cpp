#include "Camera.h"

#include "../Core/Logging/Log.h"
#include "../SDK/Offsets.h"
#include "../SDK/TargetProfile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>

namespace
{
    float LengthSquared(const Unity::Vector3& value)
    {
        return value.x * value.x + value.y * value.y + value.z * value.z;
    }

    float Dot(const Unity::Vector3& left, const Unity::Vector3& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    bool ValidViewMatrix(const Unity::Matrix4x4& matrix)
    {
        constexpr float kMaximumElement = 1000000.0f;
        for (const auto& row : matrix.m)
            for (const float value : row)
                if (!std::isfinite(value) || std::fabs(value) > kMaximumElement)
                    return false;

        const Unity::Vector3 right{matrix.m[0][0], matrix.m[1][0], matrix.m[2][0]};
        const Unity::Vector3 up{matrix.m[0][1], matrix.m[1][1], matrix.m[2][1]};
        const Unity::Vector3 forward{matrix.m[0][2], matrix.m[1][2], matrix.m[2][2]};
        const float rightLength = std::sqrt(LengthSquared(right));
        const float upLength = std::sqrt(LengthSquared(up));
        const float forwardLength = std::sqrt(LengthSquared(forward));
        if (rightLength < 0.5f || rightLength > 1.5f || upLength < 0.5f || upLength > 1.5f || forwardLength < 0.5f || forwardLength > 1.5f)
            return false;

        constexpr float kMaximumNormalizedDot = 0.2f;
        return std::fabs(Dot(right, up) / (rightLength * upLength)) < kMaximumNormalizedDot &&
               std::fabs(Dot(right, forward) / (rightLength * forwardLength)) < kMaximumNormalizedDot &&
               std::fabs(Dot(up, forward) / (upLength * forwardLength)) < kMaximumNormalizedDot;
    }

    bool ValidCamera(const MemoryClient& memory, std::uint64_t camera)
    {
        Unity::Matrix4x4 matrix{};
        float fov = 0.0f, aspect = 0.0f;
        return Unity::IsValidAddress(camera) && memory.TryRead(camera + UnityOffsets::Camera::ViewMatrix, matrix, true) &&
               memory.TryRead(camera + UnityOffsets::Camera::Fov, fov, true) && memory.TryRead(camera + UnityOffsets::Camera::AspectRatio, aspect, true) && std::isfinite(fov) &&
               fov >= 1.0f && fov < 180.0f && std::isfinite(aspect) && aspect > 0.0f && ValidViewMatrix(matrix);
    }

    bool NativeCameraAddressFromInstance(const MemoryClient& memory, std::uint64_t instance, std::uint64_t& camera)
    {
        camera = 0;
        std::uint64_t cameraReference = 0;
        return Unity::IsValidAddress(instance) && memory.TryRead(instance + ArenaOffsets::CameraManager::Camera, cameraReference, true) && Unity::IsValidAddress(cameraReference) &&
               memory.TryRead(cameraReference + 0x10, camera, true) && Unity::IsValidAddress(camera);
    }

    bool NativeCameraFromInstance(const MemoryClient& memory, std::uint64_t instance, std::uint64_t& camera)
    {
        return NativeCameraAddressFromInstance(memory, instance, camera) && ValidCamera(memory, camera);
    }

    bool StaticInstance(const MemoryClient& memory, std::uint64_t klass, std::uint32_t staticFieldsOffset, std::uint64_t& instance, std::uint64_t& instanceSlot)
    {
        instance = 0;
        instanceSlot = 0;
        std::uint64_t staticFields = 0;
        if (!memory.TryRead(klass + staticFieldsOffset, staticFields, true) || !Unity::IsValidAddress(staticFields) || !memory.TryRead(staticFields, instance, true) ||
            !Unity::IsValidAddress(instance))
            return false;
        instanceSlot = staticFields;
        return true;
    }

    bool ResolveViaCameraManager(const MemoryClient& memory, std::uint64_t& camera, std::uint64_t& managerInstance, std::uint64_t& instanceSlot)
    {
        camera = 0;
        managerInstance = 0;
        instanceSlot = 0;
        if (Unity::IsValidAddress(ArenaOffsets::CameraManager::ClassAddress))
        {
            constexpr std::array<std::int32_t, 6> adjustments = {0, -0x10, -0x08, 0x08, 0x10, 0x18};
            for (const auto adjustment : adjustments)
            {
                const auto offset = static_cast<std::uint32_t>(static_cast<std::int32_t>(ArenaOffsets::Il2CppClass::StaticFields) + adjustment);
                std::uint64_t instance = 0;
                std::uint64_t slot = 0;
                if (StaticInstance(memory, ArenaOffsets::CameraManager::ClassAddress, offset, instance, slot) && NativeCameraFromInstance(memory, instance, camera))
                {
                    managerInstance = instance;
                    instanceSlot = slot;
                    return true;
                }
            }
        }

        std::uint64_t gameAssembly = 0;
        if (!memory.ModuleBase(TargetProfile::GameAssemblyModule, gameAssembly) || !ArenaOffsets::CameraManager::GetInstanceRva)
            return false;
        const auto method = gameAssembly + ArenaOffsets::CameraManager::GetInstanceRva;
        std::array<std::uint8_t, 128> bytes{};
        if (!memory.Read(method, bytes.data(), bytes.size(), true))
            return false;

        // get_Instance normally loads the Il2CppClass through a RIP-relative
        for (std::size_t i = 0; i + 7 <= bytes.size(); ++i)
        {
            if (bytes[i] != 0x48 || bytes[i + 1] != 0x8D || bytes[i + 2] != 0x0D)
                continue;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, bytes.data() + i + 3, sizeof(displacement));
            const auto classGlobal = static_cast<std::uint64_t>(static_cast<std::int64_t>(method + i + 7) + displacement);
            std::uint64_t klass = 0;
            if (!memory.TryRead(classGlobal, klass, true) || !Unity::IsValidAddress(klass))
                continue;
            constexpr std::array<std::int32_t, 6> adjustments = {0, -0x10, -0x08, 0x08, 0x10, 0x18};
            for (const auto adjustment : adjustments)
            {
                const auto offset = static_cast<std::uint32_t>(static_cast<std::int32_t>(ArenaOffsets::Il2CppClass::StaticFields) + adjustment);
                std::uint64_t instance = 0;
                std::uint64_t slot = 0;
                if (StaticInstance(memory, klass, offset, instance, slot) && NativeCameraFromInstance(memory, instance, camera))
                {
                    managerInstance = instance;
                    instanceSlot = slot;
                    return true;
                }
            }
        }

        // Some builds load the singleton directly with mov rax,[rip+rel32]
        for (std::size_t i = 24; i + 7 <= bytes.size(); ++i)
        {
            if (bytes[i] != 0x48 || bytes[i + 1] != 0x8B || bytes[i + 2] != 0x05)
                continue;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, bytes.data() + i + 3, sizeof(displacement));
            const auto global = static_cast<std::uint64_t>(static_cast<std::int64_t>(method + i + 7) + displacement);
            std::uint64_t instance = 0;
            if (memory.TryRead(global, instance, true) && NativeCameraFromInstance(memory, instance, camera))
            {
                managerInstance = instance;
                instanceSlot = global;
                return true;
            }
        }
        return false;
    }

    bool IsFpsCameraName(const MemoryClient& memory, std::uint64_t address)
    {
        if (!Unity::IsValidAddress(address))
            return false;
        char name[64]{};
        if (!memory.Read(address, name, sizeof(name), true))
            return false;
        name[sizeof(name) - 1] = '\0';
        return std::strstr(name, "FPS") && std::strstr(name, "Camera");
    }

    bool TryNamedCamera(const MemoryClient& memory, std::uint64_t camera, std::uint32_t objectOffset, std::uint32_t nameOffset)
    {
        std::uint64_t gameObject = 0, name = 0;
        return memory.TryRead(camera + objectOffset, gameObject, true) && Unity::IsValidAddress(gameObject) && memory.TryRead(gameObject + nameOffset, name, true) &&
               IsFpsCameraName(memory, name) && ValidCamera(memory, camera);
    }

    bool DiscoverNamedCameraLayout(const MemoryClient& memory, std::uint64_t camera, std::uint32_t& objectOffset, std::uint32_t& nameOffset)
    {
        constexpr std::size_t PointerCount = 0x100 / sizeof(std::uint64_t);
        std::array<std::uint64_t, PointerCount> cameraFields{};
        if (!memory.Read(camera, cameraFields.data(), sizeof(cameraFields), true))
            return false;
        for (std::size_t objectIndex = 2; objectIndex < cameraFields.size(); ++objectIndex)
        {
            const auto gameObject = cameraFields[objectIndex];
            if (!Unity::IsValidAddress(gameObject) || gameObject == camera)
                continue;
            std::array<std::uint64_t, PointerCount> objectFields{};
            if (!memory.Read(gameObject, objectFields.data(), sizeof(objectFields), true))
                continue;
            for (std::size_t nameIndex = 2; nameIndex < objectFields.size(); ++nameIndex)
            {
                if (!IsFpsCameraName(memory, objectFields[nameIndex]) || !ValidCamera(memory, camera))
                    continue;
                objectOffset = static_cast<std::uint32_t>(objectIndex * sizeof(std::uint64_t));
                nameOffset = static_cast<std::uint32_t>(nameIndex * sizeof(std::uint64_t));
                return true;
            }
        }
        return false;
    }
}

bool Unity::Camera::Resolve(std::string& error)
{
    std::uint64_t camera = 0;
    std::uint64_t managerInstance = 0;
    std::uint64_t instanceSlot = 0;
    if (ResolveViaCameraManager(memory_, camera, managerInstance, instanceSlot))
    {
        address_ = camera;
        managerInstance_ = managerInstance;
        managerInstanceSlot_ = instanceSlot;
        return true;
    }
    std::uint64_t unityBase = 0, allCameras = 0, items = 0;
    std::int32_t count = 0;
    if (!memory_.ModuleBase(TargetProfile::UnityPlayerModule, unityBase) || !memory_.TryRead(unityBase + UnityOffsets::AllCamerasRva, allCameras) || !IsValidAddress(allCameras) ||
        !memory_.TryRead(allCameras, items) || !IsValidAddress(items) || !memory_.TryRead(allCameras + 0x8, count) || count < 1 || count > 1024)
    {
        error = "Unity AllCameras list is unavailable.";
        return false;
    }
    for (int i = 0; i < (std::min)(count, 100); ++i)
    {
        std::uint64_t cameraCandidate = 0;
        if (!memory_.TryRead(items + static_cast<std::uint64_t>(i) * 8, cameraCandidate) || !IsValidAddress(cameraCandidate))
            continue;
        if (UnityOffsets::GoObjectClass != 0 && UnityOffsets::GoName != 0 && TryNamedCamera(memory_, cameraCandidate, UnityOffsets::GoObjectClass, UnityOffsets::GoName))
        {
            address_ = cameraCandidate;
            managerInstance_ = 0;
            managerInstanceSlot_ = 0;
            return true;
        }
        std::uint32_t objectOffset = 0, nameOffset = 0;
        if (!DiscoverNamedCameraLayout(memory_, cameraCandidate, objectOffset, nameOffset))
            continue;
        UnityOffsets::GoObjectClass = objectOffset;
        UnityOffsets::GoName = nameOffset;
        std::ostringstream message;
        message << "Camera: discovered AllCameras name layout object=0x" << std::hex << std::uppercase << objectOffset << ", name=0x" << nameOffset << ".";
        Log::Write(message.str());
        address_ = cameraCandidate;
        managerInstance_ = 0;
        managerInstanceSlot_ = 0;
        return true;
    }
    std::ostringstream detail;
    detail << "CameraManager.Instance unresolved (class=0x" << std::hex << ArenaOffsets::CameraManager::ClassAddress << ", get_Instance RVA=0x"
           << ArenaOffsets::CameraManager::GetInstanceRva << "); FPS Camera not found in AllCameras.";
    error = detail.str();
    return false;
}

bool Unity::Camera::Tick(std::string& error)
{
    ready_ = false;
    holdingPreviousFrame_ = false;
    const auto now = std::chrono::steady_clock::now();

    // CameraManager swaps its active native camera during death, spectating and respawn
    if (IsValidAddress(managerInstanceSlot_))
    {
        std::uint64_t currentManager = 0;
        if (memory_.TryRead(managerInstanceSlot_, currentManager, true) && IsValidAddress(currentManager))
            managerInstance_ = currentManager;
        else
        {
            managerInstance_ = 0;
            managerInstanceSlot_ = 0;
            address_ = 0;
        }
    }
    if (IsValidAddress(managerInstance_))
    {
        std::uint64_t currentCamera = 0;
        if (NativeCameraAddressFromInstance(memory_, managerInstance_, currentCamera))
        {
            if (IsValidAddress(address_) && currentCamera != address_)
            {
                ++cameraSwitches_;
                std::ostringstream message;
                message << "Camera: active address changed from 0x" << std::hex << address_ << " to 0x" << currentCamera << ".";
                Log::Write(message.str());
            }
            address_ = currentCamera;
        }
        else
        {
            managerInstance_ = 0;
            managerInstanceSlot_ = 0;
            address_ = 0;
        }
    }

    // A camera destroyed during death can remain readable with a plausible matrix
    if (now >= nextManagerProbe_)
    {
        nextManagerProbe_ = now + std::chrono::milliseconds(50);
        std::uint64_t probedCamera = 0;
        std::uint64_t probedManager = 0;
        std::uint64_t probedSlot = 0;
        if (ResolveViaCameraManager(memory_, probedCamera, probedManager, probedSlot))
        {
            if (IsValidAddress(address_) && probedCamera != address_)
            {
                ++cameraSwitches_;
                std::ostringstream message;
                message << "Camera: periodic validation changed address from 0x" << std::hex << address_ << " to 0x" << probedCamera << ".";
                Log::Write(message.str());
            }
            address_ = probedCamera;
            managerInstance_ = probedManager;
            managerInstanceSlot_ = probedSlot;
        }
    }
    if (!IsValidAddress(address_) && !Resolve(error))
        return false;

    Matrix4x4 raw{};
    if (!memory_.TryRead(address_ + UnityOffsets::Camera::ViewMatrix, raw, true) || !memory_.TryRead(address_ + UnityOffsets::Camera::Fov, fov_, true) ||
        !memory_.TryRead(address_ + UnityOffsets::Camera::AspectRatio, aspect_, true) || !std::isfinite(fov_) || fov_ < 1.0f || fov_ >= 180.0f || !std::isfinite(aspect_) ||
        aspect_ <= 0.0f || !ValidViewMatrix(raw))
    {
        ++rejectedSamples_;
        address_ = 0;
        error = "Camera matrix, FOV or aspect read failed validation; resolving the active camera again.";
        return false;
    }
    matrix_.Update(raw);
    ready_ = true;
    error.clear();
    return true;
}

bool Unity::Camera::WorldToScreen(const Vector3& world, Vector2& screen, float width, float height) const
{
    return ready_ && matrix_.WorldToScreen(world, screen, width, height, fov_, aspect_);
}
