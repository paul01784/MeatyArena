#pragma once

#include "../App/AppConfig.h"
#include "../Config/ConfigStore.h"
#include "../Core/InputDevice.h"
#include "../Core/Aim/ReadOnlyAim.h"
#include "../Fuser/FuserController.h"
#include "../Memory/MemoryClient.h"
#include "../GameWorld/WorldSource.h"
#include "../Players/PlayerService.h"
#include "../Unity/Camera.h"

#include <string>
#include <array>
#include <chrono>
#include <vector>

struct ImFont;
struct HWND__;

class MainWindow
{
public:
    MainWindow(AppConfig& config, ConfigStore& configStore, MemoryClient& memory, PlayerService& players, WorldSource& worldSource, FuserController& fuser);

    void Render();
    void SetWindowHandle(HWND__* window)
    {
        window_ = window;
    }
    void SetFonts(const std::array<std::array<ImFont*, 2>, 3>& fonts)
    {
        fonts_ = fonts;
    }
    void SetFuserOutputFps(float fps)
    {
        fuserOutputFps_ = fps;
    }

private:
    enum class Panel
    {
        None,
        Settings,
        Fuser,
        Devices,
        Debug
    };
    enum class DevicePage
    {
        Connection,
        Aim,
        Diagnostics
    };
    void RenderRightMenu();
    void RenderPanel();
    void RenderConnectionTab();
    void RenderPlayersTab();
    void RenderWaitingStatus(const MemoryStatus& status);
    void RenderFuserTab();
    void RenderDevicesTab();
    void RenderDebugTab();
    void RenderFuserOutput();
    void SaveConfig();
    ImFont* SelectedFont() const;
    void MoveApplicationToMonitor();

    AppConfig& config_;
    ConfigStore& configStore_;
    MemoryClient& memory_;
    PlayerService& players_;
    WorldSource& worldSource_;
    FuserController& fuser_;
    Unity::Camera camera_;
    ReadOnlyAim aim_;
    std::string cameraError_;
    std::chrono::steady_clock::time_point nextCameraPoll_{};
    std::uint64_t lastLocalPlayerAddress_ = 0;
    bool lastInRaid_ = false;
    std::vector<std::string> serialPorts_;
    std::string configMessage_;
    Panel activePanel_ = Panel::None;
    HWND__* window_ = nullptr;
    std::array<std::array<ImFont*, 2>, 3> fonts_{};
    bool fuserPlacementPending_ = true;
    HWND__* fuserWindowHandle_ = nullptr;
    bool fuserTransparencyApplied_ = false;
    bool debugAutoScroll_ = true;
    DevicePage devicePage_ = DevicePage::Connection;
    bool capturingAimKey_ = false;
    std::string deviceTestMessage_;
    float fuserOutputFps_ = 0.0f;
};
