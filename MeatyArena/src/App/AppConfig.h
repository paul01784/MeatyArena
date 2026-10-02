#pragma once

#include <cstdint>
#include <array>
#include <string>

enum class SerialDeviceType
{
    None,
    Makcu,
    Ferrum
};

enum class AutoClickMode
{
    Single,
    Auto
};

struct ConnectionConfig
{
    std::string deviceUri = "fpga://algo=0";
    std::string memoryMapPath = "mmap.txt";
    bool useMemoryMap = true;
    bool debugOutput = false;
    int waitForProcessSeconds = 15;
};

struct PlayersConfig
{
    int pollIntervalMs = 33;
    bool showInactive = false;
};

struct FuserConfig
{
    bool startOnLaunch = false;
    bool showNames = true;
    bool showSkeleton = true;
    float scale = 1.0f;
    bool transparentBackground = false;
    std::array<float, 3> backgroundColor = {0.0f, 0.0f, 0.0f};
    int viewportWidth = 1920;
    int viewportHeight = 1080;
    int monitorIndex = 0;
    bool fullscreen = false;
};

struct DeviceConfig
{
    SerialDeviceType type = SerialDeviceType::None;
    std::string port;
    bool connectOnStartup = false;
    float makcuMouseUnitsPerScreenPixelX = 1.0f;
    float makcuMouseUnitsPerScreenPixelY = 1.0f;
    float ferrumMouseUnitsPerScreenPixelX = 1.0f;
    float ferrumMouseUnitsPerScreenPixelY = 1.0f;
};

struct AimConfig
{
    bool enabled = false;
    bool fireportAim = true;
    bool autoFire = false;
    AutoClickMode autoClickMode = AutoClickMode::Auto;
    float autoFireHoldBufferPixels = 10.0f;
    bool autoAimAssist = false;
    int activationKey = 0x06;
    float radiusPixels = 30.0f;
    float strength = 0.1f;
    int maxStepPixels = 5;
    bool closestBone = true;
    int targetBoneIndex = 0;
};

struct WindowConfig
{
    int width = 920;
    int height = 640;
    float alpha = 1.0f;
    int monitorIndex = 0;
    int fontIndex = 0;
    bool fontBold = true;
    float textScale = 1.0f;
};

struct AppConfig
{
    static constexpr int CurrentVersion = 11;

    ConnectionConfig connection;
    PlayersConfig players;
    FuserConfig fuser;
    DeviceConfig device;
    AimConfig aim;
    WindowConfig window;
};
