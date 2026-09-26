#include "MainWindow.h"
#include "../App/Version.h"
#include "../Core/Makcu/Makcu.h"
#include "../Core/Ferrum/Ferrum.h"
#include "../Core/Logging/Log.h"
#include "../Fuser/PlayerDrawing.h"
#include "../Players/TeamColours.h"
#include "../SDK/Offsets.h"
#include "IconsFontAwesome7.h"
#include "DisplayMonitors.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <cfloat>
#include <sstream>

namespace
{
    const char* StateName(MemoryConnectionState state)
    {
        switch (state)
        {
        case MemoryConnectionState::Connecting:
            return "Connecting";
        case MemoryConnectionState::WaitingForProcess:
            return "Waiting for process";
        case MemoryConnectionState::Resolving:
            return "Resolving live offsets";
        case MemoryConnectionState::Connected:
            return "Connected";
        case MemoryConnectionState::Failed:
            return "Failed";
        default:
            return "Disconnected";
        }
    }

    ImVec4 StateColour(MemoryConnectionState state)
    {
        if (state == MemoryConnectionState::Connected)
            return ImVec4(0.24f, 0.90f, 0.38f, 1.0f);
        if (state == MemoryConnectionState::Failed)
            return ImVec4(1.0f, 0.28f, 0.28f, 1.0f);
        if (state == MemoryConnectionState::Connecting || state == MemoryConnectionState::WaitingForProcess || state == MemoryConnectionState::Resolving)
            return ImVec4(1.0f, 0.72f, 0.24f, 1.0f);
        return ImVec4(0.62f, 0.62f, 0.62f, 1.0f);
    }

    void SectionTitle(const char* title, const char* description)
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const float labelWidth = ImGui::CalcTextSize(title).x;
        const float lineWidth = (std::max)(0.0f, (width - labelWidth - 16.0f) * 0.5f);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float lineY = start.y + ImGui::GetTextLineHeight() * 0.5f;
        const ImU32 lineColor = ImGui::GetColorU32(ImVec4(0.62f, 0.12f, 0.15f, 0.58f));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x, lineY), ImVec2(start.x + lineWidth, lineY), lineColor);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x + lineWidth + 16.0f + labelWidth, lineY), ImVec2(start.x + width, lineY), lineColor);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lineWidth + 8.0f);
        ImGui::TextColored(ImVec4(0.94f, 0.22f, 0.25f, 1.0f), "%s", title);
        if (description && *description)
            ImGui::TextDisabled("%s", description);
        ImGui::Spacing();
    }

    void DrawCenteredStatus(const ImVec2 center, ImFont* font, float size, const ImVec4 color, const char* text)
    {
        if (!text || !*text)
            return;
        if (!font)
            font = ImGui::GetFont();
        const ImVec2 dimensions = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        ImGui::GetWindowDrawList()->AddText(font, size, ImVec2(center.x - dimensions.x * 0.5f, center.y - dimensions.y * 0.5f), ImGui::GetColorU32(color), text);
    }

    constexpr const char* kAimBoneNames[] = {"Head",         "Neck",          "Upper spine", "Mid spine",  "Lower spine", "Pelvis",      "Left collar", "Right collar",
                                             "Left forearm", "Right forearm", "Left palm",   "Right palm", "Left thigh",  "Right thigh", "Left foot",   "Right foot"};

    std::string VirtualKeyName(int key)
    {
        switch (key)
        {
        case VK_LBUTTON:
            return "Mouse 1";
        case VK_RBUTTON:
            return "Mouse 2";
        case VK_MBUTTON:
            return "Mouse 3";
        case VK_XBUTTON1:
            return "Mouse 4";
        case VK_XBUTTON2:
            return "Mouse 5";
        case VK_LSHIFT:
            return "Left Shift";
        case VK_RSHIFT:
            return "Right Shift";
        case VK_LCONTROL:
            return "Left Control";
        case VK_RCONTROL:
            return "Right Control";
        case VK_LMENU:
            return "Left Alt";
        case VK_RMENU:
            return "Right Alt";
        default:
            break;
        }

        UINT scanCode = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC_EX);
        if (!scanCode)
        {
            char fallback[16]{};
            std::snprintf(fallback, sizeof(fallback), "VK 0x%02X", key & 0xff);
            return fallback;
        }
        LONG keyData = static_cast<LONG>(scanCode << 16);
        if (key == VK_INSERT || key == VK_DELETE || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT || key == VK_LEFT || key == VK_RIGHT || key == VK_UP ||
            key == VK_DOWN || key == VK_RCONTROL || key == VK_RMENU)
            keyData |= 1 << 24;
        char name[64]{};
        return GetKeyNameTextA(keyData, name, static_cast<int>(sizeof(name))) > 0 ? std::string(name) : "Unknown";
    }

    std::string CompactMonitorLabel(std::string label)
    {
        const std::size_t coordinateStart = label.rfind("  (");
        if (coordinateStart == std::string::npos)
            return label;
        const std::size_t coordinateEnd = label.find(')', coordinateStart);
        if (coordinateEnd != std::string::npos)
            label.erase(coordinateStart, coordinateEnd - coordinateStart + 1);
        return label;
    }
} // namespace

MainWindow::MainWindow(AppConfig& config, ConfigStore& configStore, MemoryClient& memory, PlayerService& players, WorldSource& worldSource, FuserController& fuser)
    : config_(config), configStore_(configStore), memory_(memory), players_(players), worldSource_(worldSource), fuser_(fuser), camera_(memory)
{
    for (const auto& port : MakcuController::EnumerateSerialPorts())
        serialPorts_.push_back(port.portName);
    for (const auto& port : FerrumController::EnumerateSerialPorts())
        if (std::find(serialPorts_.begin(), serialPorts_.end(), port.portName) == serialPorts_.end())
            serialPorts_.push_back(port.portName);
}

void MainWindow::Render()
{
    const MemoryStatus status = memory_.GetStatus();
    const bool wasInRaid = status.state == MemoryConnectionState::Connected && worldSource_.InRaid();
    const bool highRefreshFuser = fuser_.IsRunning() && wasInRaid;

    players_.SetSource(&worldSource_);
    players_.Tick(highRefreshFuser ? (std::min)(config_.players.pollIntervalMs, 8) : config_.players.pollIntervalMs);
    const auto now = std::chrono::steady_clock::now();
    const bool inRaid = status.state == MemoryConnectionState::Connected && worldSource_.InRaid();
    const std::uint64_t localPlayerAddress = worldSource_.LocalPlayerAddress();
    const bool raidChanged = inRaid != lastInRaid_;
    const bool localPlayerChanged = localPlayerAddress != lastLocalPlayerAddress_;
    if (raidChanged || (inRaid && localPlayerChanged))
    {
        camera_.Invalidate();
        nextCameraPoll_ = now;
        if (raidChanged)
            Log::Write(inRaid ? "Camera: match started; resolving active camera." : "Camera: match ended; cleared cached camera state.");
        else
            Log::Write("Camera: local player changed; resolving active camera immediately.");
    }
    lastInRaid_ = inRaid;
    lastLocalPlayerAddress_ = localPlayerAddress;
    if (status.state == MemoryConnectionState::Connected && now >= nextCameraPoll_)
    {
        camera_.Tick(cameraError_);
        nextCameraPoll_ = now + std::chrono::milliseconds(camera_.Ready() ? (highRefreshFuser ? 4 : 16) : (inRaid ? 25 : 100));
    }
    if (status.state != MemoryConnectionState::Connected)
        camera_.Invalidate();
    if (status.state == MemoryConnectionState::Connected)
        aim_.Tick(config_.aim, memory_, camera_, players_.GetPlayers(), localPlayerAddress, static_cast<float>(config_.fuser.viewportWidth),
                  static_cast<float>(config_.fuser.viewportHeight));

    ImGui::GetIO().FontGlobalScale = std::clamp(config_.window.textScale, 0.75f, 1.5f);
    ImGui::PushFont(SelectedFont());
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowBgAlpha(inRaid ? config_.window.alpha : 1.0f);
    if (!inRaid)
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("MeatyArena", nullptr, flags);

    ImGui::SetCursorPos(ImVec2(18.0f, 14.0f));
    ImGui::TextColored(ImVec4(0.95f, 0.19f, 0.23f, 1.0f), "MEATY");
    ImGui::SameLine(0.0f, 3.0f);
    ImGui::TextUnformatted("ARENA");

    if (inRaid)
    {
        ImGui::SetCursorPos(ImVec2(18.0f, 52.0f));
        const ImVec2 contentSize((std::max)(100.0f, ImGui::GetWindowWidth() - 88.0f), (std::max)(100.0f, ImGui::GetWindowHeight() - 64.0f));
        if (ImGui::BeginChild("##MainContent", contentSize, true))
        {
            ImGui::TextDisabled("PLAYER STATUS");
            ImGui::Separator();
            RenderPlayersTab();
        }
        ImGui::EndChild();
    }
    else
        RenderWaitingStatus(status);
    RenderRightMenu();
    char versionLabel[32]{};
    std::snprintf(versionLabel, sizeof(versionLabel), "v%s", AppVersion::Number);
    const ImVec2 versionSize = ImGui::CalcTextSize(versionLabel);
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - versionSize.x - 12.0f, ImGui::GetWindowHeight() - versionSize.y - 9.0f));
    ImGui::TextDisabled("%s", versionLabel);
    ImGui::End();
    if (!inRaid)
        ImGui::PopStyleColor();

    RenderPanel();

    if (fuser_.IsRunning())
        RenderFuserOutput();
    ImGui::PopFont();
}

ImFont* MainWindow::SelectedFont() const
{
    const int family = std::clamp(config_.window.fontIndex, 0, 2);
    return fonts_[family][config_.window.fontBold ? 1 : 0];
}

void MainWindow::MoveApplicationToMonitor()
{
    if (!window_)
        return;
    const auto monitors = DisplayMonitors::Enumerate();
    if (monitors.empty())
        return;
    const int index = std::clamp(config_.window.monitorIndex, 0, static_cast<int>(monitors.size()) - 1);
    RECT windowRect{};
    if (!GetWindowRect(window_, &windowRect))
        return;
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const RECT work = monitors[index].workArea;
    SetWindowPos(window_, nullptr, work.left + ((work.right - work.left) - width) / 2, work.top + ((work.bottom - work.top) - height) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::RenderRightMenu()
{
    constexpr float buttonSize = 42.0f;
    constexpr float top = 12.0f;
    constexpr float step = 48.0f;
    const float x = ImGui::GetWindowWidth() - buttonSize - 10.0f;
    struct Item
    {
        const char* icon;
        const char* tip;
        Panel panel;
    };
    const Item items[] = {{ICON_FA_GEARS, "Settings / Connect", Panel::Settings},
                          {ICON_FA_TV, "Fuser", Panel::Fuser},
                          {ICON_FA_COMPUTER_MOUSE, "MAKCU / Ferrum", Panel::Devices},
                          {ICON_FA_BUG, "Live diagnostics", Panel::Debug}};
    for (int index = 0; index < IM_ARRAYSIZE(items); ++index)
    {
        ImGui::SetCursorPos(ImVec2(x, top + step * index));
        ImGui::PushID(index);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.22f, 0.25f, 1.0f));
        if (activePanel_ == items[index].panel)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.09f, 0.12f, 1.0f));
        const bool pressed = ImGui::Button(items[index].icon, ImVec2(buttonSize, buttonSize));
        if (activePanel_ == items[index].panel)
            ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        if (pressed)
            activePanel_ = activePanel_ == items[index].panel ? Panel::None : items[index].panel;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", items[index].tip);
        ImGui::PopID();
    }
}

void MainWindow::RenderPanel()
{
    if (activePanel_ == Panel::None)
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const char* title = activePanel_ == Panel::Settings ? "Settings"
                        : activePanel_ == Panel::Fuser  ? "Fuser"
                        : activePanel_ == Panel::Debug  ? "Debug / Diagnostics"
                                                        : "MAKCU / Ferrum";
    const bool debugPanel = activePanel_ == Panel::Debug;
    const float width = debugPanel ? 720.0f : activePanel_ == Panel::Devices ? 760.0f : activePanel_ == Panel::Fuser ? 540.0f : 470.0f;
    const float height = debugPanel ? 650.0f : activePanel_ == Panel::Devices ? 620.0f : activePanel_ == Panel::Fuser ? 500.0f : 390.0f;
    if (debugPanel)
    {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - 72.0f, viewport->WorkPos.y + 68.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
    }
    else
    {
        const float menuY = activePanel_ == Panel::Settings ? 12.0f : activePanel_ == Panel::Fuser ? 60.0f : 108.0f;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - width - 60.0f, viewport->WorkPos.y + menuY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    }
    bool open = true;
    const ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoCollapse | (debugPanel ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoResize);
    if (ImGui::Begin(title, &open, panelFlags))
    {
        switch (activePanel_)
        {
        case Panel::Settings:
            RenderConnectionTab();
            break;
        case Panel::Fuser:
            RenderFuserTab();
            break;
        case Panel::Devices:
            RenderDevicesTab();
            break;
        case Panel::Debug:
            RenderDebugTab();
            break;
        default:
            break;
        }
    }
    ImGui::End();
    if (!open)
        activePanel_ = Panel::None;
}

void MainWindow::RenderConnectionTab()
{
    SectionTitle("DMA connection", nullptr);
    const MemoryStatus status = memory_.GetStatus();
    const bool busy =
        status.state == MemoryConnectionState::Connecting || status.state == MemoryConnectionState::WaitingForProcess || status.state == MemoryConnectionState::Resolving;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Connect", ImVec2(130.0f, 32.0f)))
        memory_.ConnectAsync(config_.connection);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Disconnect", ImVec2(130.0f, 32.0f)))
        memory_.Disconnect();
    ImGui::Spacing();
    SectionTitle("Status", nullptr);
    ImGui::TextColored(StateColour(status.state), "%s", StateName(status.state));
    if (!status.message.empty() && status.message != StateName(status.state))
        ImGui::TextWrapped("%s", status.message.c_str());
    if (status.processId)
        ImGui::Text("PID: %u", status.processId);
    if (status.state == MemoryConnectionState::Connected && !worldSource_.InRaid())
        ImGui::TextDisabled("Waiting for raid data from GameWorld.");
    ImGui::Spacing();
    SectionTitle("Application appearance", nullptr);
    const char* fontNames[] = {"Segoe UI", "Arial", "Tahoma"};
    config_.window.fontIndex = std::clamp(config_.window.fontIndex, 0, 2);
    ImGui::SetNextItemWidth(140.0f);
    bool appearanceChanged = ImGui::Combo("Font", &config_.window.fontIndex, fontNames, IM_ARRAYSIZE(fontNames));
    ImGui::SameLine();
    appearanceChanged |= ImGui::Checkbox("Bold", &config_.window.fontBold);
    ImGui::SetNextItemWidth(180.0f);
    appearanceChanged |= ImGui::SliderFloat("Font Scale", &config_.window.textScale, 0.75f, 1.5f, "%.2fx");
    if (appearanceChanged)
        SaveConfig();
    if (!configMessage_.empty())
        ImGui::TextDisabled("%s", configMessage_.c_str());
}

void MainWindow::RenderWaitingStatus(const MemoryStatus& status)
{
    const char* waiting = "Waiting for Match Start";
    if (status.state == MemoryConnectionState::Connecting)
        waiting = "Connecting to DMA";
    else if (status.state == MemoryConnectionState::Disconnected || status.state == MemoryConnectionState::Failed)
        waiting = "Waiting for DMA Connection";
    else if (status.state == MemoryConnectionState::WaitingForProcess)
        waiting = "Waiting for Process";
    else if (status.state == MemoryConnectionState::Resolving)
        waiting = "Resolving Live Offsets";
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 center(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
    const float scale = std::clamp(config_.window.textScale, 0.75f, 1.5f);
    DrawCenteredStatus(center, SelectedFont(), 36.0f * scale, ImVec4(1, 0, 0, 1), waiting);
    const bool connectionHint = status.state == MemoryConnectionState::Disconnected || status.state == MemoryConnectionState::Failed;
    if (connectionHint)
        DrawCenteredStatus(ImVec2(center.x, center.y + 42.0f * scale), SelectedFont(), 20.0f * scale, ImVec4(1, 1, 1, 1), "Connect from Settings menu -->");
    const std::string detail = status.state == MemoryConnectionState::Connected ? players_.GetLastError() : status.message;
    if (!detail.empty())
        DrawCenteredStatus(ImVec2(center.x, center.y + (connectionHint ? 72.0f : 45.0f) * scale), SelectedFont(), 20.0f * scale, ImVec4(1, 1, 1, 1), detail.c_str());
}

void MainWindow::RenderPlayersTab()
{
    std::vector<PlayerSnapshot> snapshots = players_.GetPlayers();
    std::sort(snapshots.begin(), snapshots.end(),
              [](const auto& a, const auto& b)
              {
                  return a.name < b.name;
              });
    int localTeam = -1;
    for (const auto& player : snapshots)
        if (player.local && player.teamId >= 0)
        {
            localTeam = player.teamId;
            break;
        }
    const auto countFor = [&](int team)
    {
        return static_cast<int>(std::count_if(snapshots.begin(), snapshots.end(),
                                              [team](const auto& p)
                                              {
                                                  return p.active && p.teamId == team;
                                              }));
    };
    const int activePlayers = static_cast<int>(std::count_if(snapshots.begin(), snapshots.end(),
                                                             [](const auto& p)
                                                             {
                                                                 return p.active;
                                                             }));
    const int assignedPlayers = static_cast<int>(std::count_if(snapshots.begin(), snapshots.end(),
                                                               [](const auto& p)
                                                               {
                                                                   return p.active && p.teamId >= 0;
                                                               }));
    const int unknown = static_cast<int>(std::count_if(snapshots.begin(), snapshots.end(),
                                                       [](const auto& p)
                                                       {
                                                           return p.active && p.teamId < 0;
                                                       }));
    const bool freeForAll = activePlayers > 0 && assignedPlayers == 0;
    ImGui::TextColored(ImVec4(0.94f, 0.22f, 0.25f, 1.0f), ICON_FA_USERS "  PLAYER INFO");
    ImGui::SameLine();
    if (freeForAll)
        ImGui::TextDisabled("free for all | %d players", activePlayers);
    else
        ImGui::TextDisabled("%d players | %d awaiting team", activePlayers, unknown);
    ImGui::Separator();
    const float panelHeight = (std::max)(140.0f, ImGui::GetContentRegionAvail().y - 4.0f);

    // Last Hero
    if (freeForAll)
    {
        ImGui::BeginChild("##FreeForAllPanel", ImVec2(0.0f, panelHeight), true);
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.20f, 1.0f), "LAST HERO / FREE FOR ALL  (%d)", activePlayers);
        ImGui::Separator();
        if (ImGui::BeginTable("##FreeForAllPlayers", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Player");
            ImGui::TableHeadersRow();
            for (const auto& player : snapshots)
            {
                if (!player.active)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const ArenaTeams::Colour colour = player.local ? ArenaTeams::Colour{50, 205, 50} : ArenaTeams::DisplayColour(-1, false, player.isAI);
                ImGui::TextColored(ImVec4(colour.r / 255.0f, colour.g / 255.0f, colour.b / 255.0f, 1.0f), "%s%s%s", player.name.c_str(), player.local ? " (you)" : "",
                                   player.isAI ? " [AI]" : "");
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        return;
    }

    if (ImGui::BeginTable("##Teams", 2, ImGuiTableFlags_SizingStretchSame))
    {
        for (int team : {0, 6})
        {
            ImGui::TableNextColumn();
            ImGui::PushID(team);
            ImGui::BeginChild("##TeamPanel", ImVec2(0.0f, panelHeight), true);
            const bool red = team == 0;
            ImGui::TextColored(red ? ImVec4(0.96f, 0.25f, 0.29f, 1.0f) : ImVec4(0.27f, 0.59f, 1.0f, 1.0f), "TEAM %s  (%d)", red ? "RED" : "BLUE", countFor(team));
            ImGui::Separator();
            if (ImGui::BeginTable("##TeamPlayers", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Player");
                ImGui::TableHeadersRow();
                for (const auto& player : snapshots)
                {
                    if (!player.active || player.teamId != team)
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const ArenaTeams::Colour colour =
                        player.local ? ArenaTeams::Colour{50, 205, 50} : ArenaTeams::DisplayColour(player.teamId, localTeam >= 0 && player.teamId == localTeam, player.isAI);
                    ImGui::TextColored(ImVec4(colour.r / 255.0f, colour.g / 255.0f, colour.b / 255.0f, 1.0f), "%s%s%s", player.name.c_str(), player.local ? " (you)" : "",
                                       player.isAI ? " [AI]" : "");
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void MainWindow::RenderFuserTab()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 10.0f));
    SectionTitle("Fuser window", nullptr);
    ImGui::BeginDisabled(fuser_.IsRunning());
    if (ImGui::Button("Start", ImVec2(120.0f, 30.0f)))
    {
        fuserPlacementPending_ = true;
        fuser_.Start();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!fuser_.IsRunning());
    if (ImGui::Button("Stop", ImVec2(120.0f, 30.0f)))
    {
        fuser_.Stop();
        fuserWindowHandle_ = nullptr;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(fuser_.IsRunning() ? ImVec4(0.24f, 0.90f, 0.38f, 1.0f) : ImVec4(0.62f, 0.62f, 0.62f, 1.0f), fuser_.IsRunning() ? "Running" : "Stopped");
    ImGui::Spacing();
    SectionTitle("Monitor", nullptr);
    const auto monitors = DisplayMonitors::Enumerate();
    if (!monitors.empty())
    {
        config_.fuser.monitorIndex = std::clamp(config_.fuser.monitorIndex, 0, static_cast<int>(monitors.size()) - 1);
        const std::string selectedMonitor = CompactMonitorLabel(monitors[config_.fuser.monitorIndex].label);
        ImGui::SetNextItemWidth(410.0f);
        if (ImGui::BeginCombo("##FuserMonitor", selectedMonitor.c_str()))
        {
            for (int index = 0; index < static_cast<int>(monitors.size()); ++index)
            {
                const std::string monitorLabel = CompactMonitorLabel(monitors[index].label);
                if (ImGui::Selectable(monitorLabel.c_str(), index == config_.fuser.monitorIndex))
                {
                    config_.fuser.monitorIndex = index;
                    fuserPlacementPending_ = true;
                    SaveConfig();
                }
            }
            ImGui::EndCombo();
        }
    }
    else
        ImGui::TextDisabled("No monitors detected.");
    if (ImGui::Checkbox("Fullscreen", &config_.fuser.fullscreen))
    {
        fuserPlacementPending_ = true;
        SaveConfig();
    }
    ImGui::SameLine(0.0f, 24.0f);
    if (ImGui::Button("Refresh monitors", ImVec2(150.0f, 28.0f)))
        fuserPlacementPending_ = true;
    bool backgroundChanged = ImGui::Checkbox("Transparent", &config_.fuser.transparentBackground);
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::BeginDisabled(config_.fuser.transparentBackground);
    backgroundChanged |= ImGui::ColorEdit3("##FuserBackgroundColour", config_.fuser.backgroundColor.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::EndDisabled();
    if (backgroundChanged)
        SaveConfig();
    ImGui::Spacing();
    SectionTitle("PC Resolution (Game)", nullptr);
    const char* options[] = {"1920x1080", "2560x1440", "3440x1440", "3840x2160"};
    int resolution = 0;
    if (config_.fuser.viewportWidth == 2560 && config_.fuser.viewportHeight == 1440)
        resolution = 1;
    else if (config_.fuser.viewportWidth == 3440 && config_.fuser.viewportHeight == 1440)
        resolution = 2;
    else if (config_.fuser.viewportWidth == 3840 && config_.fuser.viewportHeight == 2160)
        resolution = 3;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("##GamePcResolution", &resolution, options, IM_ARRAYSIZE(options)))
    {
        constexpr int widths[] = {1920, 2560, 3440, 3840};
        constexpr int heights[] = {1080, 1440, 1440, 2160};
        config_.fuser.viewportWidth = widths[resolution];
        config_.fuser.viewportHeight = heights[resolution];
        SaveConfig();
    }
    ImGui::Spacing();
    SectionTitle("Render", nullptr);
    bool renderChanged = ImGui::Checkbox("Show names", &config_.fuser.showNames);
    ImGui::SameLine(0.0f, 24.0f);
    renderChanged |= ImGui::Checkbox("Show skeleton", &config_.fuser.showSkeleton);
    ImGui::SameLine(0.0f, 24.0f);
    ImGui::SetNextItemWidth(130.0f);
    renderChanged |= ImGui::SliderFloat("Scale", &config_.fuser.scale, 0.75f, 2.0f, "%.2fx");
    if (renderChanged)
        SaveConfig();
    ImGui::PopStyleVar();
}

void MainWindow::RenderDevicesTab()
{
    const auto pageButton = [&](const char* icon, const char* label, DevicePage page)
    {
        const bool selected = devicePage_ == page;
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.10f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.14f, 0.17f, 1.0f));
        }
        const std::string text = std::string(icon) + "  " + label;
        if (ImGui::Button(text.c_str(), ImVec2(180.0f, 38.0f)))
            devicePage_ = page;
        if (selected)
            ImGui::PopStyleColor(2);
    };

    pageButton(ICON_FA_PLUG, "Connection", DevicePage::Connection);
    ImGui::SameLine();
    pageButton(ICON_FA_CROSSHAIRS, "Aim", DevicePage::Aim);
    ImGui::SameLine();
    pageButton(ICON_FA_BUG, "Diagnostics", DevicePage::Diagnostics);
    ImGui::Separator();
    ImGui::Spacing();

    const InputDeviceType connectedType = inputDevice.GetConnectedType();
    SerialDeviceType selectedType = config_.device.type == SerialDeviceType::Ferrum ? SerialDeviceType::Ferrum : SerialDeviceType::Makcu;

    if (devicePage_ == DevicePage::Connection)
    {
        bool changed = false;
        SectionTitle("Device", "Select one controller. Disconnect before switching hardware.");
        const auto selectDevice = [&](const char* label, SerialDeviceType type, InputDeviceType inputType)
        {
            const bool selected = selectedType == type;
            const bool locked = connectedType != InputDeviceType::None && connectedType != inputType;
            ImGui::BeginDisabled(locked);
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.10f, 0.13f, 1.0f));
            if (ImGui::Button(label, ImVec2(220.0f, 42.0f)) && !locked)
            {
                config_.device.type = type;
                selectedType = type;
                changed = true;
            }
            if (selected)
                ImGui::PopStyleColor();
            ImGui::EndDisabled();
        };
        const float selectorWidth = 456.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - selectorWidth) * 0.5f));
        selectDevice("MAKCU", SerialDeviceType::Makcu, InputDeviceType::Makcu);
        ImGui::SameLine(0.0f, 16.0f);
        selectDevice("FERRUM", SerialDeviceType::Ferrum, InputDeviceType::Ferrum);

        ImGui::Spacing();
        SectionTitle("Serial port", "Choose the controller's COM port, then connect and probe its firmware.");
        ImGui::SetNextItemWidth(330.0f);
        if (ImGui::BeginCombo("COM port", config_.device.port.empty() ? "Select a COM port" : config_.device.port.c_str()))
        {
            for (const std::string& port : serialPorts_)
            {
                const bool selected = port == config_.device.port;
                if (ImGui::Selectable(port.c_str(), selected))
                {
                    config_.device.port = port;
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Scan ports"))
        {
            serialPorts_.clear();
            for (const auto& port : MakcuController::EnumerateSerialPorts())
                serialPorts_.push_back(port.portName);
            for (const auto& port : FerrumController::EnumerateSerialPorts())
                if (std::find(serialPorts_.begin(), serialPorts_.end(), port.portName) == serialPorts_.end())
                    serialPorts_.push_back(port.portName);
        }
        if (serialPorts_.empty())
            ImGui::TextDisabled("No active serial ports found.");
        changed |= ImGui::Checkbox("Connect on startup", &config_.device.connectOnStartup);

        ImGui::Spacing();
        SectionTitle("Connection", nullptr);
        const bool connected = inputDevice.IsConnected();
        ImGui::BeginDisabled(connected || config_.device.port.empty());
        if (ImGui::Button("Connect", ImVec2(150.0f, 32.0f)))
        {
            config_.device.type = selectedType;
            if (selectedType == SerialDeviceType::Makcu)
                makcu.Connect(config_.device.port.c_str());
            else
                ferrum.Connect(config_.device.port.c_str());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!connected);
        if (ImGui::Button("Disconnect", ImVec2(150.0f, 32.0f)))
            inputDevice.Disconnect();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(connected ? ImVec4(0.24f, 0.90f, 0.38f, 1.0f) : ImVec4(0.62f, 0.62f, 0.62f, 1.0f), connected ? "%s connected" : "Disconnected",
                           inputDevice.GetConnectedName());
        if (changed)
            SaveConfig();
    }
    else if (devicePage_ == DevicePage::Aim)
    {
        bool changed = false;
        SectionTitle("Aim", nullptr);
        changed |= ImGui::Checkbox("Enable", &config_.aim.enabled);
        ImGui::SameLine(0.0f, 28.0f);
        ImGui::TextUnformatted("Activation key");
        ImGui::SameLine(0.0f, 10.0f);
        const std::string keyLabel = capturingAimKey_ ? "Press a key or mouse button..." : VirtualKeyName(config_.aim.activationKey);
        ImGui::BeginDisabled(!memory_.KeyboardReady());
        const bool beginKeyCapture = ImGui::Button(keyLabel.c_str(), ImVec2(250.0f, 30.0f));
        ImGui::EndDisabled();
        if (beginKeyCapture)
        {
            capturingAimKey_ = true;
            memory_.BeginKeyCapture();
        }
        if (capturingAimKey_)
        {
            if (!memory_.KeyboardReady())
                capturingAimKey_ = false;
            else
            {
                const std::uint32_t key = memory_.GetFirstPressedKey();
                if (key == VK_ESCAPE)
                    capturingAimKey_ = false;
                else if (key != 0)
                {
                    config_.aim.activationKey = static_cast<int>(key);
                    capturingAimKey_ = false;
                    changed = true;
                }
            }
            ImGui::TextDisabled("Press a key on the game PC. Escape cancels.");
        }

        ImGui::BeginDisabled(!config_.aim.enabled);
        ImGui::SetNextItemWidth(340.0f);
        changed |= ImGui::SliderFloat("Aim radius", &config_.aim.radiusPixels, 10.0f, 600.0f, "%.0f px");
        ImGui::BeginDisabled(config_.aim.autoFire && !config_.aim.autoAimAssist);
        ImGui::SetNextItemWidth(340.0f);
        changed |= ImGui::SliderFloat("Aim strength", &config_.aim.strength, 0.01f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(340.0f);
        changed |= ImGui::SliderInt("Maximum step", &config_.aim.maxStepPixels, 1, 100, "%d px");
        ImGui::EndDisabled();

        ImGui::Spacing();
        SectionTitle("Target bone", nullptr);
        changed |= ImGui::Checkbox("Closest Bone", &config_.aim.closestBone);
        ImGui::SameLine(0.0f, 28.0f);
        ImGui::BeginDisabled(config_.aim.closestBone);
        config_.aim.targetBoneIndex = (std::clamp)(config_.aim.targetBoneIndex, 0, IM_ARRAYSIZE(kAimBoneNames) - 1);
        ImGui::SetNextItemWidth(280.0f);
        changed |= ImGui::Combo("Fixed target bone", &config_.aim.targetBoneIndex, kAimBoneNames, IM_ARRAYSIZE(kAimBoneNames));
        ImGui::EndDisabled();
        changed |= ImGui::Checkbox("Fireport Aim", &config_.aim.fireportAim);
        ImGui::BeginDisabled(!config_.aim.fireportAim);
        changed |= ImGui::Checkbox("Auto Click", &config_.aim.autoFire);
        ImGui::SameLine(0.0f, 28.0f);
        ImGui::BeginDisabled(!config_.aim.autoFire);
        changed |= ImGui::Checkbox("Aim Assist", &config_.aim.autoAimAssist);
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::Spacing();
        SectionTitle("Mouse calibration", nullptr);
        const bool ferrumSelected = selectedType == SerialDeviceType::Ferrum;
        float& calibrationX = ferrumSelected ? config_.device.ferrumMouseUnitsPerScreenPixelX : config_.device.makcuMouseUnitsPerScreenPixelX;
        float& calibrationY = ferrumSelected ? config_.device.ferrumMouseUnitsPerScreenPixelY : config_.device.makcuMouseUnitsPerScreenPixelY;
        ImGui::SetNextItemWidth(280.0f);
        changed |= ImGui::DragFloat("X units per pixel", &calibrationX, 0.001f, 0.001f, 5.0f, "%.4f");
        ImGui::SetNextItemWidth(280.0f);
        changed |= ImGui::DragFloat("Y units per pixel", &calibrationY, 0.001f, 0.001f, 5.0f, "%.4f");
        if (ImGui::Button("Reset calibration"))
        {
            calibrationX = calibrationY = 1.0f;
            changed = true;
        }
        ImGui::EndDisabled();

        makcu.mouseUnitsPerScreenPixelX = config_.device.makcuMouseUnitsPerScreenPixelX;
        makcu.mouseUnitsPerScreenPixelY = config_.device.makcuMouseUnitsPerScreenPixelY;
        ferrum.mouseUnitsPerScreenPixelX = config_.device.ferrumMouseUnitsPerScreenPixelX;
        ferrum.mouseUnitsPerScreenPixelY = config_.device.ferrumMouseUnitsPerScreenPixelY;
        const bool aimError = aim_.State() == AimState::MoveFailed || aim_.State() == AimState::AutoFireFailed || aim_.State() == AimState::DeviceDisconnected ||
                              aim_.State() == AimState::CameraUnavailable || aim_.State() == AimState::FireportUnavailable || aim_.State() == AimState::DmaKeyboardUnavailable;
        ImGui::TextColored(aimError ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.62f, 0.62f, 0.62f, 1.0f), "Status: %s", aim_.StateText());
        if (config_.aim.fireportAim)
            ImGui::TextDisabled("Fireport: %s | %s", aim_.Fireport().StateText(), aim_.Fireport().Path());
        if (config_.aim.autoFire)
            ImGui::TextDisabled("Auto: %s | assist: %s | distance %.1f px | holds %llu", aim_.AutoHolding() ? "holding" : (aim_.AutoAligned() ? "aligned" : "waiting"),
                                config_.aim.autoAimAssist ? "on" : "off", aim_.LastTargetDistancePixels(), static_cast<unsigned long long>(aim_.AutoHoldCount()));
        ImGui::TextDisabled("Input: %s | key: %s", memory_.KeyboardReady() ? "DMA" : "DMA unavailable", aim_.ActivationHeld() ? "held" : "up");
        if (aim_.LastTarget())
        {
            const int bone = aim_.LastBoneIndex();
            ImGui::TextDisabled("Active target: 0x%llX | %s", static_cast<unsigned long long>(aim_.LastTarget()),
                                bone >= 0 && bone < IM_ARRAYSIZE(kAimBoneNames) ? kAimBoneNames[bone] : "unknown bone");
        }
        if (changed)
            SaveConfig();
    }
    else
    {
        const bool showFerrum = connectedType == InputDeviceType::Ferrum || (connectedType == InputDeviceType::None && selectedType == SerialDeviceType::Ferrum);
        const auto makcuStatus = makcu.GetDiagnostics();
        const auto ferrumStatus = ferrum.GetDiagnostics();
        const bool connected = showFerrum ? ferrumStatus.connected : makcuStatus.connected;
        const std::string& port = showFerrum ? ferrumStatus.connectedPort : makcuStatus.connectedPort;
        const std::uint32_t baud = showFerrum ? ferrumStatus.connectedBaudRate : makcuStatus.connectedBaudRate;
        const std::string& firmware = showFerrum ? ferrumStatus.firmwareVersion : makcuStatus.firmwareVersion;
        const std::string& reply = showFerrum ? ferrumStatus.lastReply : makcuStatus.lastReply;
        const std::string& error = showFerrum ? ferrumStatus.lastError : makcuStatus.lastError;

        SectionTitle("Device diagnostics", showFerrum ? "Ferrum controller" : "MAKCU controller");
        ImGui::TextColored(connected ? ImVec4(0.24f, 0.90f, 0.38f, 1.0f) : ImVec4(0.62f, 0.62f, 0.62f, 1.0f), connected ? "Connected" : "Disconnected");
        ImGui::Text("Port: %s", port.empty() ? "-" : port.c_str());
        ImGui::Text("Baud: %u", baud);
        ImGui::Text("Firmware: %s", firmware.empty() ? "-" : firmware.c_str());
        ImGui::BeginDisabled(!connected);
        if (ImGui::Button("Refresh diagnostics", ImVec2(180.0f, 30.0f)))
            deviceTestMessage_ = (showFerrum ? ferrum.RefreshDiagnostics() : makcu.RefreshDiagnostics()) ? "Diagnostics refreshed." : "Diagnostics refresh failed.";
        ImGui::SameLine();
        if (ImGui::Button("Test mouse movement", ImVec2(180.0f, 30.0f)))
            deviceTestMessage_ = (showFerrum ? ferrum.TestMouseMovement() : makcu.TestMouseMovement()) ? "Mouse movement test completed." : "Mouse movement test failed.";
        ImGui::EndDisabled();
        if (!deviceTestMessage_.empty())
            ImGui::TextDisabled("%s", deviceTestMessage_.c_str());
        if (!reply.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Last reply: %s", reply.c_str());
        }
        if (!error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Last error: %s", error.c_str());

        ImGui::Spacing();
        SectionTitle("Aim state", nullptr);
        ImGui::Text("Enabled: %s", config_.aim.enabled ? "Yes" : "No");
        ImGui::Text("Fireport aim: %s", config_.aim.fireportAim ? "Yes" : "No");
        ImGui::Text("Auto: %s | assist: %s | aligned: %s | holding: %s | holds: %llu", config_.aim.autoFire ? "Yes" : "No", config_.aim.autoAimAssist ? "Yes" : "No",
                    aim_.AutoAligned() ? "Yes" : "No", aim_.AutoHolding() ? "Yes" : "No", static_cast<unsigned long long>(aim_.AutoHoldCount()));
        ImGui::Text("Activation: %s", VirtualKeyName(config_.aim.activationKey).c_str());
        ImGui::Text("Target bone: %s",
                    config_.aim.closestBone ? "Closest to crosshair" : kAimBoneNames[(std::clamp)(config_.aim.targetBoneIndex, 0, IM_ARRAYSIZE(kAimBoneNames) - 1)]);
        ImGui::Text("Camera: %s", camera_.Ready() ? "Ready" : "Unavailable");
        ImGui::Text("DMA keyboard: %s", memory_.KeyboardStatus().c_str());
        ImGui::Text("DMA key-state address: 0x%llX", static_cast<unsigned long long>(memory_.KeyboardAddress()));
        ImGui::Text("Live status: %s", aim_.StateText());
        ImGui::Text("Activation held through DMA: %s", aim_.ActivationFromDma() ? "Yes" : "No");
        ImGui::Text("Projected candidates: %d", aim_.ProjectedCandidates());
        ImGui::Text("Last movement: %d, %d", aim_.LastMoveX(), aim_.LastMoveY());
    }
}

void MainWindow::RenderDebugTab()
{
    const MemoryStatus status = memory_.GetStatus();
    const auto logs = Log::Recent();
    const auto players = players_.GetPlayers();
    const double failureRate = status.readOperations ? 100.0 * static_cast<double>(status.readFailures) / static_cast<double>(status.readOperations) : 0.0;

    SectionTitle("Live connection", "Current values update while the DMA worker runs.");
    if (ImGui::BeginTable("##DebugConnection", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        const auto row = [](const char* label, const char* format, auto value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(format, value);
        };
        row("State", "%s", StateName(status.state));
        row("PID", "%u", status.processId);
        row("GameAssembly base", "0x%016llX", static_cast<unsigned long long>(status.moduleBase));
        row("GameAssembly size", "%llu bytes", static_cast<unsigned long long>(status.moduleSize));
        row("Read operations", "%llu", static_cast<unsigned long long>(status.readOperations));
        row("Bytes returned", "%llu", static_cast<unsigned long long>(status.readBytes));
        row("Read failures", "%llu", static_cast<unsigned long long>(status.readFailures));
        row("Failure rate", "%.2f%%", failureRate);
        ImGui::EndTable();
    }
    ImGui::TextWrapped("Status: %s", status.message.c_str());

    ImGui::Spacing();
    SectionTitle("Resolved runtime", "Zero means that stage has not resolved yet.");
    if (ImGui::BeginTable("##DebugRuntime", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
    {
        const auto hexRow = [](const char* label, std::uint64_t value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("0x%llX", static_cast<unsigned long long>(value));
        };
        hexRow("TypeInfoTable RVA", ArenaOffsets::Special::TypeInfoTableRva);
        hexRow("GamePlayerOwner type index", ArenaOffsets::Special::GamePlayerOwnerTypeIndex);
        hexRow("CameraManager.get_Instance RVA", ArenaOffsets::CameraManager::GetInstanceRva);
        hexRow("Unity AllCameras RVA", UnityOffsets::AllCamerasRva);
        hexRow("AllCameras object offset", UnityOffsets::GoObjectClass);
        hexRow("GameObject name offset", UnityOffsets::GoName);
        hexRow("Camera view matrix", UnityOffsets::Camera::ViewMatrix);
        hexRow("Camera FOV", UnityOffsets::Camera::Fov);
        hexRow("Camera aspect", UnityOffsets::Camera::AspectRatio);
        hexRow("World address", worldSource_.WorldAddress());
        hexRow("Camera address", camera_.Address());
        hexRow("DMA keyboard address", memory_.KeyboardAddress());
        ImGui::EndTable();
    }
    ImGui::TextWrapped("DMA keyboard: %s", memory_.KeyboardStatus().c_str());

    ImGui::Spacing();
    SectionTitle("Fireport aim", "Live local weapon transform used as the aim reference.");
    const auto& fireport = aim_.Fireport();
    ImGui::Text("State: %s | path: %s", fireport.StateText(), fireport.Path());
    ImGui::Text("Local player: 0x%llX | hands: 0x%llX | transform: 0x%llX", static_cast<unsigned long long>(fireport.LocalPlayer()),
                static_cast<unsigned long long>(fireport.HandsController()), static_cast<unsigned long long>(fireport.TransformAddress()));
    const auto& fireportOrigin = fireport.Origin();
    const auto& fireportForward = fireport.Forward();
    const auto& fireportReference = fireport.AimReference();
    ImGui::Text("Origin: %.3f, %.3f, %.3f | forward: %.3f, %.3f, %.3f", fireportOrigin.x, fireportOrigin.y, fireportOrigin.z, fireportForward.x, fireportForward.y,
                fireportForward.z);
    ImGui::Text("Screen reference: %.1f, %.1f", fireportReference.x, fireportReference.y);
    ImGui::Text("Auto: %s | assist: %s | aligned: %s | holding: %s | target distance: %.2f px | holds: %llu", config_.aim.autoFire ? "Enabled" : "Disabled",
                config_.aim.autoAimAssist ? "Enabled" : "Disabled", aim_.AutoAligned() ? "Yes" : "No", aim_.AutoHolding() ? "Yes" : "No", aim_.LastTargetDistancePixels(),
                static_cast<unsigned long long>(aim_.AutoHoldCount()));
    ImGui::Text("Offsets: Player hands 0x%X | PWA 0x%X | bones 0x%X", ArenaOffsets::Player::HandsController, ArenaOffsets::Player::ProceduralWeaponAnimation,
                ArenaOffsets::Player::PlayerBones);
    ImGui::Text("Firearm: fireport 0x%X | gun base 0x%X | firearms 0x%X", ArenaOffsets::FirearmController::Fireport, ArenaOffsets::FirearmController::GunBaseTransform,
                ArenaOffsets::FirearmController::Firearms);

    ImGui::Spacing();
    SectionTitle("Workers", nullptr);
    ImGui::Text("Players: %llu | successful polls: %llu | failed polls: %llu | last poll: %.2f ms", static_cast<unsigned long long>(players.size()),
                static_cast<unsigned long long>(players_.GetSuccessfulSamples()), static_cast<unsigned long long>(players_.GetFailedSamples()), players_.GetLastPollMs());
    if (!players_.GetLastError().empty())
        ImGui::TextWrapped("Player worker: %s", players_.GetLastError().c_str());
    ImGui::Text("Camera: %s | switches: %llu | rejected samples: %llu%s", camera_.Ready() ? "Ready" : "Unavailable", static_cast<unsigned long long>(camera_.CameraSwitches()),
                static_cast<unsigned long long>(camera_.RejectedSamples()), camera_.HoldingPreviousFrame() ? " | holding last valid matrix" : "");
    if (!cameraError_.empty())
        ImGui::TextWrapped("Camera worker: %s", cameraError_.c_str());

    if (ImGui::BeginTable("##DebugPlayers", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Position source");
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Head");
        ImGui::TableSetupColumn("Transform");
        ImGui::TableSetupColumn("Skeleton");
        ImGui::TableHeadersRow();
        for (const auto& player : players)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(player.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(!player.hasPosition ? "pending" : player.positionFromBones ? "bones" : "cached transform");
            ImGui::TableSetColumnIndex(2);
            if (player.hasPosition)
                ImGui::Text("%.2f, %.2f, %.2f", player.x, player.y, player.z);
            else
                ImGui::TextDisabled("0, 0, 0 sentinel");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(player.hasHead ? "ready" : "pending");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("stage %u / 6, idx %u", player.transformStage, player.transformIndex);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%u/8 %u bones | c%u h%u i%u a%u g%u max%u", player.skeletonStage, player.readyBones, player.skeletonComponents, player.skeletonHierarchies,
                        player.skeletonIndices, player.skeletonArrays, player.skeletonReadyGroups, player.skeletonMaxIndex);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    SectionTitle("Event log", "This is also written to logs/meatyarena.log beside the executable.");
    ImGui::Checkbox("Auto-scroll", &debugAutoScroll_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_COPY "  Copy diagnostics"))
    {
        std::ostringstream report;
        report << "MeatyArena " << AppVersion::Number << " diagnostics\nState: " << StateName(status.state) << "\nStatus: " << status.message << "\nPID: " << status.processId
               << "\nGameAssembly: 0x" << std::hex << std::uppercase << status.moduleBase << " size 0x" << status.moduleSize << "\nTypeInfoTable RVA: 0x"
               << ArenaOffsets::Special::TypeInfoTableRva << "\nReads: " << std::dec << status.readOperations << " (" << status.readFailures << " failures)\n";
        report << "\nPlayers:\n";
        report << "\nFireport: " << fireport.StateText() << " path=" << fireport.Path() << " local=0x" << std::hex << fireport.LocalPlayer() << " hands=0x"
               << fireport.HandsController() << " transform=0x" << fireport.TransformAddress() << std::dec << " origin=(" << fireportOrigin.x << ", " << fireportOrigin.y << ", "
               << fireportOrigin.z << ") forward=(" << fireportForward.x << ", " << fireportForward.y << ", " << fireportForward.z << ") screen=(" << fireportReference.x << ", "
               << fireportReference.y << ")\n";
        for (const auto& player : players)
            report << player.name << ": position=" << (player.hasPosition ? (player.positionFromBones ? "bones" : "cached") : "pending") << " (" << player.x << ", " << player.y
                   << ", " << player.z << ")"
                   << ", transform=" << static_cast<unsigned>(player.transformStage) << "/6 idx=" << player.transformIndex
                   << ", skeleton=" << static_cast<unsigned>(player.skeletonStage) << "/8 bones=" << player.readyBones << " components=" << player.skeletonComponents
                   << " hierarchies=" << player.skeletonHierarchies << " indices=" << player.skeletonIndices << " arrays=" << player.skeletonArrays
                   << " groups=" << player.skeletonReadyGroups << " maxIndex=" << player.skeletonMaxIndex << '\n';
        report << "\nLog:\n";
        for (const auto& line : logs)
            report << line << '\n';
        ImGui::SetClipboardText(report.str().c_str());
    }
    const float logHeight = (std::max)(130.0f, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild("##DebugLog", ImVec2(0.0f, logHeight), true, ImGuiWindowFlags_HorizontalScrollbar))
    {
        for (const auto& line : logs)
            ImGui::TextUnformatted(line.c_str());
        if (debugAutoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void MainWindow::RenderFuserOutput()
{
    const auto monitors = DisplayMonitors::Enumerate();
    if (!monitors.empty())
    {
        const int index = std::clamp(config_.fuser.monitorIndex, 0, static_cast<int>(monitors.size()) - 1);
        const RECT bounds = monitors[index].bounds;
        if (config_.fuser.fullscreen)
        {
            ImGui::SetNextWindowPos(ImVec2(static_cast<float>(bounds.left), static_cast<float>(bounds.top)), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(bounds.right - bounds.left), static_cast<float>(bounds.bottom - bounds.top)), ImGuiCond_Always);
            fuserPlacementPending_ = false;
        }
        else if (fuserPlacementPending_)
        {
            const float width = (std::min)(960.0f, static_cast<float>(bounds.right - bounds.left));
            const float height = (std::min)(540.0f, static_cast<float>(bounds.bottom - bounds.top));
            ImGui::SetNextWindowPos(ImVec2(bounds.left + (bounds.right - bounds.left - width) * 0.5f, bounds.top + (bounds.bottom - bounds.top - height) * 0.5f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
            fuserPlacementPending_ = false;
        }
    }
    else
        ImGui::SetNextWindowSize(ImVec2(960.0f, 540.0f), ImGuiCond_FirstUseEver);
    bool open = fuser_.IsRunning();
    ImGuiWindowClass outputClass;
    outputClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&outputClass);
    ImGui::SetNextWindowBgAlpha(config_.fuser.transparentBackground ? 0.0f : 1.0f);
    const auto& background = config_.fuser.backgroundColor;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(background[0], background[1], background[2], 1.0f));
    const ImGuiWindowFlags flags = config_.fuser.fullscreen ? ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
                                                            : ImGuiWindowFlags_NoCollapse;
    const ImGuiWindowFlags backgroundFlags =
        config_.fuser.transparentBackground ? ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings : 0;
    if (ImGui::Begin("MeatyArena Fuser Output", config_.fuser.fullscreen || config_.fuser.transparentBackground ? nullptr : &open, flags | backgroundFlags))
    {
        ImGuiViewport* outputViewport = ImGui::GetWindowViewport();
        HWND outputWindow = static_cast<HWND>(outputViewport->PlatformHandle);
        if (outputWindow && outputWindow != window_ && (outputWindow != fuserWindowHandle_ || fuserTransparencyApplied_ != config_.fuser.transparentBackground))
        {
            LONG_PTR style = GetWindowLongPtrW(outputWindow, GWL_EXSTYLE);
            if (config_.fuser.transparentBackground)
            {
                SetWindowLongPtrW(outputWindow, GWL_EXSTYLE, style | WS_EX_LAYERED);
                SetLayeredWindowAttributes(outputWindow, RGB(0, 0, 0), 0, LWA_COLORKEY);
            }
            else
                SetWindowLongPtrW(outputWindow, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
            SetWindowPos(outputWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            fuserWindowHandle_ = outputWindow;
            fuserTransparencyApplied_ = config_.fuser.transparentBackground;
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##FuserCanvas", size);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
        char fpsText[32]{};
        std::snprintf(fpsText, sizeof(fpsText), "FPS %.1f", fuserOutputFps_);
        const ImVec2 fpsSize = ImGui::CalcTextSize(fpsText);
        draw->AddText(ImVec2(origin.x + size.x - fpsSize.x - 12.0f, origin.y + 10.0f), IM_COL32(240, 240, 238, 220), fpsText);
        if (!worldSource_.InRaid() || !camera_.Ready())
        {
            const char* inactive = "Window inactive";
            ImFont* font = ImGui::GetFont();
            const float fontSize = 20.0f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, inactive);
            draw->AddText(font, fontSize, ImVec2(origin.x + (size.x - textSize.x) * 0.5f, origin.y + 12.0f), IM_COL32(240, 240, 238, 255), inactive);
        }
        else
            FuserDrawing::DrawPlayers(*draw, origin, size, camera_, players_.GetPlayers(), config_.fuser);
        draw->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    if (!open)
    {
        fuser_.Stop();
        fuserWindowHandle_ = nullptr;
    }
}

void MainWindow::SaveConfig()
{
    std::string error;
    configMessage_ = configStore_.Save(config_, error) ? "Configuration saved." : "Save failed: " + error;
}
