#pragma once

#include "../App/AppConfig.h"
#include "../Players/PlayerModel.h"
#include "../Unity/Camera.h"

#include <imgui.h>

#include <vector>

namespace FuserDrawing
{
    void DrawPlayers(ImDrawList& draw, const ImVec2& origin, const ImVec2& size, const Unity::Camera& camera, const std::vector<PlayerSnapshot>& players,
                     const FuserConfig& config);
}
