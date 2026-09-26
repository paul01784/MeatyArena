#include "ReadOnlyAim.h"

#include "../InputDevice.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float AutoFireRadiusPixels = 6.0f;
} // namespace

void ReadOnlyAim::StopAutoFire()
{
    if (!autoHolding_)
        return;

    if (inputDevice.IsConnected() && !inputDevice.ButtonUp(InputMouseButton::Left))
        (void)inputDevice.ButtonForceRelease(InputMouseButton::Left);
    autoHolding_ = false;
}

const char* ReadOnlyAim::StateText() const
{
    switch (state_)
    {
    case AimState::Disabled:
        return "Disabled";
    case AimState::CameraUnavailable:
        return "Camera unavailable";
    case AimState::FireportUnavailable:
        return "Fireport unavailable";
    case AimState::DeviceDisconnected:
        return "MAKCU / Ferrum disconnected";
    case AimState::DmaKeyboardUnavailable:
        return "DMA keyboard unavailable";
    case AimState::InvalidViewport:
        return "Invalid viewport size";
    case AimState::WaitingForActivation:
        return "Waiting for activation key";
    case AimState::NoValidTargets:
        return "No valid target bones";
    case AimState::OutsideRadius:
        return "Targets outside aim radius";
    case AimState::OnTarget:
        return "On target";
    case AimState::AutoFiring:
        return "Auto holding fire";
    case AimState::Moving:
        return "Sending movement";
    case AimState::MoveFailed:
        return "Device movement failed";
    case AimState::AutoFireFailed:
        return "Auto fire press failed";
    default:
        return "Unknown";
    }
}

void ReadOnlyAim::Tick(const AimConfig& config, const MemoryClient& memory, const Unity::Camera& camera, const std::vector<PlayerSnapshot>& players, std::uint64_t localPlayer,
                       float width, float height)
{
    lastTarget_ = 0;
    lastBoneIndex_ = -1;
    activationHeld_ = false;
    activationFromDma_ = false;
    projectedCandidates_ = 0;
    lastMoveX_ = 0;
    lastMoveY_ = 0;
    autoAligned_ = false;
    autoPressedThisTick_ = false;
    lastTargetDistancePixels_ = 0.0f;
    if (!config.enabled)
    {
        StopAutoFire();
        fireport_.Clear();
        state_ = AimState::Disabled;
        return;
    }
    if (!camera.Ready())
    {
        StopAutoFire();
        state_ = AimState::CameraUnavailable;
        return;
    }
    if (width <= 0 || height <= 0)
    {
        StopAutoFire();
        state_ = AimState::InvalidViewport;
        return;
    }
    Unity::Vector2 aimReference{width * 0.5f, height * 0.5f};
    if (config.fireportAim)
    {
        fireport_.Update(memory, camera, localPlayer, width, height);
        if (!fireport_.AimReferenceReady())
        {
            StopAutoFire();
            state_ = AimState::FireportUnavailable;
            return;
        }
        aimReference = fireport_.AimReference();
    }
    else
    {
        StopAutoFire();
        fireport_.Clear();
    }
    if (!inputDevice.IsConnected())
    {
        autoHolding_ = false;
        state_ = AimState::DeviceDisconnected;
        return;
    }
    if (!memory.KeyboardReady())
    {
        StopAutoFire();
        state_ = AimState::DmaKeyboardUnavailable;
        return;
    }
    activationFromDma_ = memory.IsKeyDown(static_cast<std::uint32_t>(config.activationKey));
    activationHeld_ = activationFromDma_;
    if (!activationHeld_)
    {
        StopAutoFire();
        state_ = AimState::WaitingForActivation;
        return;
    }
    Unity::Vector2 best{};
    float bestDistance = config.radiusPixels * config.radiusPixels;
    int localTeam = -1;
    for (const auto& player : players)
        if (player.active && player.local && player.teamId >= 0)
        {
            localTeam = player.teamId;
            break;
        }

    for (const auto& player : players)
    {
        if (!player.active || player.local || (localTeam >= 0 && player.teamId == localTeam))
            continue;

        const auto consider = [&](const Unity::Vector3& position, int boneIndex)
        {
            Unity::Vector2 screen{};
            if (!camera.WorldToScreen(position, screen, width, height))
                return;
            ++projectedCandidates_;
            const float dx = screen.x - aimReference.x, dy = screen.y - aimReference.y;
            const float distance = dx * dx + dy * dy;
            if (distance >= bestDistance)
                return;
            bestDistance = distance;
            best = screen;
            lastTarget_ = player.id;
            lastBoneIndex_ = boneIndex;
        };

        if (config.closestBone)
        {
            for (std::size_t index = 0; index < player.bones.size(); ++index)
                if (player.bones[index].valid)
                    consider(player.bones[index].position, static_cast<int>(index));
        }
        else
        {
            const int index = (std::clamp)(config.targetBoneIndex, 0, static_cast<int>(player.bones.size()) - 1);
            if (player.bones[index].valid)
                consider(player.bones[index].position, index);
            else if (player.hasHead)
                consider({player.headX, player.headY, player.headZ}, 0);
        }
    }
    if (!lastTarget_)
    {
        StopAutoFire();
        state_ = projectedCandidates_ == 0 ? AimState::NoValidTargets : AimState::OutsideRadius;
        return;
    }
    lastTargetDistancePixels_ = std::sqrt(bestDistance);
    autoAligned_ = config.fireportAim && config.autoFire && bestDistance <= AutoFireRadiusPixels * AutoFireRadiusPixels;
    if (autoAligned_)
    {
        if (!autoHolding_)
        {
            autoPressedThisTick_ = true;
            if (!inputDevice.ButtonDown(InputMouseButton::Left))
            {
                state_ = AimState::AutoFireFailed;
                return;
            }
            autoHolding_ = true;
            ++autoHoldCount_;
        }
    }
    else
        StopAutoFire();
    if (config.autoFire && !config.autoAimAssist)
    {
        state_ = autoHolding_ ? AimState::AutoFiring : AimState::OnTarget;
        return;
    }
    const float strength = (std::clamp)(config.strength, 0.01f, 1.0f);
    const int limit = (std::clamp)(config.maxStepPixels, 1, 100);
    lastMoveX_ = (std::clamp)(static_cast<int>(std::lround((best.x - aimReference.x) * strength * inputDevice.GetMouseUnitsPerScreenPixelX())), -limit, limit);
    lastMoveY_ = (std::clamp)(static_cast<int>(std::lround((best.y - aimReference.y) * strength * inputDevice.GetMouseUnitsPerScreenPixelY())), -limit, limit);
    if (!lastMoveX_ && !lastMoveY_)
    {
        state_ = autoHolding_ ? AimState::AutoFiring : AimState::OnTarget;
        return;
    }
    state_ = inputDevice.Move(lastMoveX_, lastMoveY_) ? AimState::Moving : AimState::MoveFailed;
}
