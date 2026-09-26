#pragma once

#include <cstdint>
#include <string>
#include "../Unity/Bones.h"

struct PlayerSnapshot
{
    std::uint64_t id = 0;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float headX = 0.0f;
    float headY = 0.0f;
    float headZ = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool hasPosition = false;
    bool positionFromBones = false;
    bool hasHead = false;
    std::uint8_t transformStage = 0;
    std::uint8_t skeletonStage = 0;
    std::uint32_t transformIndex = 0;
    std::uint32_t readyBones = 0;
    std::uint32_t skeletonComponents = 0;
    std::uint32_t skeletonHierarchies = 0;
    std::uint32_t skeletonIndices = 0;
    std::uint32_t skeletonArrays = 0;
    std::uint32_t skeletonReadyGroups = 0;
    std::uint32_t skeletonMaxIndex = 0;
    std::array<Unity::BoneSample, Unity::SkeletonBones.size()> bones{};
    bool local = false;
    std::uint32_t side = 0;
    std::int32_t teamId = -1;
    bool isAI = false;
    bool active = false;
};
