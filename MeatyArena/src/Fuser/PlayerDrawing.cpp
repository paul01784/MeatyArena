#include "PlayerDrawing.h"
#include "../Players/TeamColours.h"

#include <array>

namespace FuserDrawing
{
    void DrawPlayers(ImDrawList& draw, const ImVec2& origin, const ImVec2& size, const Unity::Camera& camera, const std::vector<PlayerSnapshot>& players, const FuserConfig& config)
    {
        if (!camera.Ready() || config.viewportWidth <= 0 || config.viewportHeight <= 0)
            return;
        const float width = static_cast<float>(config.viewportWidth);
        const float height = static_cast<float>(config.viewportHeight);
        const float sx = size.x / width;
        const float sy = size.y / height;
        int localTeam = -1;
        for (const PlayerSnapshot& player : players)
            if (player.local && player.teamId >= 0)
            {
                localTeam = player.teamId;
                break;
            }
        for (const PlayerSnapshot& player : players)
        {
            if (!player.active || player.local || !player.hasHead)
                continue;
            Unity::Vector2 head{}, feet{};
            if (!camera.WorldToScreen({player.headX, player.headY, player.headZ}, head, width, height) ||
                !camera.WorldToScreen({player.x, player.y, player.z}, feet, width, height))
                continue;
            const ImVec2 top(origin.x + head.x * sx, origin.y + head.y * sy);
            const ImVec2 bottom(origin.x + feet.x * sx, origin.y + feet.y * sy);
            const bool teammate = localTeam >= 0 && player.teamId == localTeam;
            const ArenaTeams::Colour teamColour = ArenaTeams::DisplayColour(player.teamId, teammate, player.isAI);
            const ImU32 colour = IM_COL32(teamColour.r, teamColour.g, teamColour.b, 230);
            int skeletonLines = 0;
            if (config.showSkeleton)
            {
                std::array<ImVec2, Unity::SkeletonBones.size()> points{};
                std::array<bool, Unity::SkeletonBones.size()> projected{};
                for (std::size_t index = 0; index < player.bones.size(); ++index)
                {
                    if (!player.bones[index].valid)
                        continue;
                    Unity::Vector2 screen{};
                    if (!camera.WorldToScreen(player.bones[index].position, screen, width, height))
                        continue;
                    points[index] = ImVec2(origin.x + screen.x * sx, origin.y + screen.y * sy);
                    projected[index] = true;
                }
                for (const auto& line : Unity::SkeletonLines)
                {
                    if (!projected[line[0]] || !projected[line[1]])
                        continue;
                    draw.AddLine(points[line[0]], points[line[1]], colour, 1.4f * config.scale);
                    ++skeletonLines;
                }
            }
            if (!config.showSkeleton || !skeletonLines)
                draw.AddLine(top, bottom, colour, 1.5f);
            draw.AddCircleFilled(top, 2.25f * config.scale, colour);
            if (config.showNames && !player.name.empty())
            {
                const ImVec2 textSize = ImGui::CalcTextSize(player.name.c_str());
                draw.AddText(ImVec2(top.x - textSize.x * 0.5f, top.y - textSize.y - 5.0f * config.scale), colour, player.name.c_str());
            }
        }
    }
} // namespace FuserDrawing
