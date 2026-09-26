#pragma once

#include "../Memory/MemoryClient.h"
#include "ViewMatrix.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace Unity
{
    class Camera
    {
    public:
        explicit Camera(const MemoryClient& memory) : memory_(memory)
        {
        }
        bool Tick(std::string& error);
        void Invalidate()
        {
            address_ = 0;
            managerInstance_ = 0;
            managerInstanceSlot_ = 0;
            ready_ = false;
            holdingPreviousFrame_ = false;
            nextManagerProbe_ = {};
        }
        bool WorldToScreen(const Vector3& world, Vector2& screen, float width, float height) const;
        bool Ready() const
        {
            return ready_;
        }
        std::uint64_t Address() const
        {
            return address_;
        }
        float Fov() const
        {
            return fov_;
        }
        float Aspect() const
        {
            return aspect_;
        }
        Vector3 Position() const
        {
            return matrix_.GetWorldPosition();
        }
        std::uint64_t RejectedSamples() const
        {
            return rejectedSamples_;
        }
        std::uint64_t CameraSwitches() const
        {
            return cameraSwitches_;
        }
        bool HoldingPreviousFrame() const
        {
            return holdingPreviousFrame_;
        }

    private:
        bool Resolve(std::string& error);
        const MemoryClient& memory_;
        ViewMatrix matrix_;
        std::uint64_t address_ = 0;
        std::uint64_t managerInstance_ = 0;
        std::uint64_t managerInstanceSlot_ = 0;
        float fov_ = 0.0f;
        float aspect_ = 0.0f;
        bool ready_ = false;
        bool holdingPreviousFrame_ = false;
        std::chrono::steady_clock::time_point nextManagerProbe_{};
        std::uint64_t rejectedSamples_ = 0;
        std::uint64_t cameraSwitches_ = 0;
    };
} 
