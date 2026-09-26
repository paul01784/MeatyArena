#pragma once

#include "Types.h"

#include <array>
#include <cstdint>

namespace Unity
{
    
    enum class Bone : std::uint32_t
    {
        HumanBase = 0,
        HumanPelvis = 14,
        HumanLThigh1 = 15,
        HumanLThigh2 = 16,
        HumanLCalf = 17,
        HumanLFoot = 18,
        HumanLToe = 19,
        HumanRThigh1 = 20,
        HumanRThigh2 = 21,
        HumanRCalf = 22,
        HumanRFoot = 23,
        HumanRToe = 24,
        HumanSpine1 = 29,
        HumanSpine2 = 36,
        HumanSpine3 = 37,
        HumanLCollarbone = 89,
        HumanLUpperarm = 90,
        HumanLForearm1 = 91,
        HumanLForearm2 = 92,
        HumanLForearm3 = 93,
        HumanLPalm = 94,
        HumanRCollarbone = 110,
        HumanRUpperarm = 111,
        HumanRForearm1 = 112,
        HumanRForearm2 = 113,
        HumanRForearm3 = 114,
        HumanRPalm = 115,
        HumanNeck = 132,
        HumanHead = 133
    };

    inline constexpr std::array<Bone, 16> SkeletonBones = {Bone::HumanHead,      Bone::HumanNeck,      Bone::HumanSpine3,      Bone::HumanSpine2,
                                                           Bone::HumanSpine1,    Bone::HumanPelvis,    Bone::HumanLCollarbone, Bone::HumanRCollarbone,
                                                           Bone::HumanLForearm2, Bone::HumanRForearm2, Bone::HumanLPalm,       Bone::HumanRPalm,
                                                           Bone::HumanLThigh2,   Bone::HumanRThigh2,   Bone::HumanLFoot,       Bone::HumanRFoot};

    inline constexpr std::array<std::array<std::size_t, 2>, 15> SkeletonLines = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {1, 6}, {6, 8}, {8, 10}, {1, 7}, {7, 9}, {9, 11}, {5, 12}, {12, 14}, {5, 13}, {13, 15}}};

    struct BoneSample
    {
        Vector3 position{};
        bool valid = false;
    };
} 
