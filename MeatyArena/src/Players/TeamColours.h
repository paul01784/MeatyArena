#pragma once

#include <cstdint>
#include <string_view>

namespace ArenaTeams
{
    struct Colour
    {
        std::uint8_t r, g, b;
    };

    inline int FromArmbandGuid(std::string_view guid)
    {
        if (guid == "63615c104bc92641374a97c8")
            return 0; // red
        if (guid == "63615bf35cb3825ded0db945")
            return 1; // fuchsia
        if (guid == "63615c36e3114462cd79f7c1")
            return 2; // yellow
        if (guid == "63615bfc5cb3825ded0db947")
            return 3; // green
        if (guid == "63615bc6ff557272023d56ac")
            return 4; // azure
        if (guid == "63615c225cb3825ded0db949")
            return 5; // white
        if (guid == "63615be82e60050cb330ef2f")
            return 6; // blue
        return -1;
    }

    inline Colour DisplayColour(int teamId, bool teammate, bool isAI)
    {
        if (teammate)
            return {100, 220, 100};
        switch (teamId)
        {
        case 0:
            return {230, 60, 60};
        case 1:
            return {230, 80, 200};
        case 2:
            return {240, 220, 60};
        case 3:
            return {70, 200, 90};
        case 4:
            return {80, 190, 230};
        case 5:
            return {235, 237, 240};
        case 6:
            return {70, 130, 230};
        default:
            return isAI ? Colour{240, 230, 60} : Colour{200, 200, 200};
        }
    }
} // namespace ArenaTeams
