#pragma once

#include "../../App/AppConfig.h"
#include "../../Memory/MemoryClient.h"
#include "../../Players/PlayerModel.h"
#include "../../Unity/Camera.h"
#include "FireportTracker.h"

#include <vector>

enum class AimState
{
    Disabled,
    CameraUnavailable,
    FireportUnavailable,
    DeviceDisconnected,
    DmaKeyboardUnavailable,
    InvalidViewport,
    WaitingForActivation,
    NoValidTargets,
    OutsideRadius,
    OnTarget,
    AutoFiring,
    Moving,
    MoveFailed,
    AutoFireFailed
};

class ReadOnlyAim
{
public:
    void Tick(const AimConfig& config, const MemoryClient& memory, const Unity::Camera& camera, const std::vector<PlayerSnapshot>& players, std::uint64_t localPlayer, float width,
              float height);
    std::uint64_t LastTarget() const
    {
        return lastTarget_;
    }
    int LastBoneIndex() const
    {
        return lastBoneIndex_;
    }
    AimState State() const
    {
        return state_;
    }
    const char* StateText() const;
    bool ActivationHeld() const
    {
        return activationHeld_;
    }
    bool ActivationFromDma() const
    {
        return activationFromDma_;
    }
    int ProjectedCandidates() const
    {
        return projectedCandidates_;
    }
    int LastMoveX() const
    {
        return lastMoveX_;
    }
    int LastMoveY() const
    {
        return lastMoveY_;
    }
    const FireportTracker& Fireport() const
    {
        return fireport_;
    }
    bool AutoAligned() const
    {
        return autoAligned_;
    }
    bool AutoPressedThisTick() const
    {
        return autoPressedThisTick_;
    }
    bool AutoHolding() const
    {
        return autoHolding_;
    }
    bool ManualFireHeld() const
    {
        return manualFireHeld_;
    }
    std::uint64_t AutoHoldCount() const
    {
        return autoHoldCount_;
    }
    float LastTargetDistancePixels() const
    {
        return lastTargetDistancePixels_;
    }

private:
    void StopAutoFire();

    AimState state_ = AimState::Disabled;
    std::uint64_t lastTarget_ = 0;
    int lastBoneIndex_ = -1;
    bool activationHeld_ = false;
    bool activationFromDma_ = false;
    int projectedCandidates_ = 0;
    int lastMoveX_ = 0;
    int lastMoveY_ = 0;
    bool autoAligned_ = false;
    bool autoPressedThisTick_ = false;
    bool autoHolding_ = false;
    bool autoTriggered_ = false;
    bool manualFireHeld_ = false;
    std::uint64_t autoHoldCount_ = 0;
    float lastTargetDistancePixels_ = 0.0f;
    FireportTracker fireport_;
};
