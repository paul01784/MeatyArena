#include "ConfigStore.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace
{
    const char* DeviceTypeName(SerialDeviceType type)
    {
        switch (type)
        {
        case SerialDeviceType::Makcu:
            return "makcu";
        case SerialDeviceType::Ferrum:
            return "ferrum";
        default:
            return "none";
        }
    }

    SerialDeviceType ParseDeviceType(const std::string& name)
    {
        if (name == "makcu")
            return SerialDeviceType::Makcu;
        if (name == "ferrum")
            return SerialDeviceType::Ferrum;
        return SerialDeviceType::None;
    }

    const char* AutoClickModeName(AutoClickMode mode)
    {
        return mode == AutoClickMode::Single ? "single" : "auto";
    }

    AutoClickMode ParseAutoClickMode(const std::string& name)
    {
        return name == "single" ? AutoClickMode::Single : AutoClickMode::Auto;
    }
} // namespace

ConfigStore::ConfigStore(std::filesystem::path path) : path_(std::move(path))
{
}

bool ConfigStore::Load(AppConfig& config, std::string& error) const
{
    try
    {
        std::ifstream file(path_);
        if (!file.is_open())
        {
            error = "Config file not found: " + path_.string();
            return false;
        }

        const nlohmann::json root = nlohmann::json::parse(file);
        const int configVersion = root.value("version", 0);
        const auto& connection = root.value("connection", nlohmann::json::object());
        config.connection.deviceUri = connection.value("deviceUri", config.connection.deviceUri);
        config.connection.memoryMapPath = connection.value("memoryMapPath", config.connection.memoryMapPath);
        config.connection.useMemoryMap = connection.value("useMemoryMap", config.connection.useMemoryMap);
        config.connection.debugOutput = connection.value("debugOutput", config.connection.debugOutput);
        config.connection.waitForProcessSeconds = connection.value("waitForProcessSeconds", config.connection.waitForProcessSeconds);

        const auto& players = root.value("players", nlohmann::json::object());
        config.players.pollIntervalMs = players.value("pollIntervalMs", config.players.pollIntervalMs);
        if (configVersion < 4 && config.players.pollIntervalMs == 100)
            config.players.pollIntervalMs = 33;
        config.players.showInactive = players.value("showInactive", config.players.showInactive);

        const auto& fuser = root.value("fuser", nlohmann::json::object());
        config.fuser.startOnLaunch = fuser.value("startOnLaunch", config.fuser.startOnLaunch);
        config.fuser.showNames = fuser.value("showNames", config.fuser.showNames);
        config.fuser.showSkeleton = fuser.value("showSkeleton", config.fuser.showSkeleton);
        config.fuser.scale = fuser.value("scale", config.fuser.scale);
        config.fuser.transparentBackground = fuser.value("transparentBackground", config.fuser.transparentBackground);
        config.fuser.backgroundColor = fuser.value("backgroundColor", config.fuser.backgroundColor);
        config.fuser.viewportWidth = fuser.value("viewportWidth", config.fuser.viewportWidth);
        config.fuser.viewportHeight = fuser.value("viewportHeight", config.fuser.viewportHeight);
        config.fuser.monitorIndex = fuser.value("monitorIndex", config.fuser.monitorIndex);
        config.fuser.fullscreen = fuser.value("fullscreen", config.fuser.fullscreen);

        const auto& device = root.value("device", nlohmann::json::object());
        config.device.type = ParseDeviceType(device.value("type", std::string("none")));
        config.device.port = device.value("port", config.device.port);
        config.device.connectOnStartup = device.value("connectOnStartup", config.device.connectOnStartup);
        config.device.makcuMouseUnitsPerScreenPixelX = device.value("makcuMouseUnitsPerScreenPixelX", config.device.makcuMouseUnitsPerScreenPixelX);
        config.device.makcuMouseUnitsPerScreenPixelY = device.value("makcuMouseUnitsPerScreenPixelY", config.device.makcuMouseUnitsPerScreenPixelY);
        config.device.ferrumMouseUnitsPerScreenPixelX = device.value("ferrumMouseUnitsPerScreenPixelX", config.device.ferrumMouseUnitsPerScreenPixelX);
        config.device.ferrumMouseUnitsPerScreenPixelY = device.value("ferrumMouseUnitsPerScreenPixelY", config.device.ferrumMouseUnitsPerScreenPixelY);

        const auto& aim = root.value("aim", nlohmann::json::object());
        config.aim.enabled = aim.value("enabled", config.aim.enabled);
        config.aim.fireportAim = aim.value("fireportAim", config.aim.fireportAim);
        config.aim.autoFire = aim.value("autoFire", config.aim.autoFire);
        config.aim.autoClickMode = ParseAutoClickMode(aim.value("autoClickMode", std::string(AutoClickModeName(config.aim.autoClickMode))));
        config.aim.autoAimAssist = aim.value("autoAimAssist", config.aim.autoAimAssist);
        config.aim.activationKey = aim.value("activationKey", config.aim.activationKey);
        config.aim.radiusPixels = aim.value("radiusPixels", config.aim.radiusPixels);
        config.aim.strength = aim.value("strength", config.aim.strength);
        config.aim.maxStepPixels = aim.value("maxStepPixels", config.aim.maxStepPixels);
        config.aim.closestBone = aim.value("closestBone", config.aim.closestBone);
        config.aim.targetBoneIndex = aim.value("targetBoneIndex", config.aim.targetBoneIndex);

        const auto& window = root.value("window", nlohmann::json::object());
        config.window.width = window.value("width", config.window.width);
        config.window.height = window.value("height", config.window.height);
        config.window.alpha = window.value("alpha", config.window.alpha);
        config.window.monitorIndex = window.value("monitorIndex", config.window.monitorIndex);
        config.window.fontIndex = window.value("fontIndex", config.window.fontIndex);
        config.window.fontBold = window.value("fontBold", config.window.fontBold);
        config.window.textScale = window.value("textScale", config.window.textScale);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool ConfigStore::Save(const AppConfig& config, std::string& error) const
{
    try
    {
        std::filesystem::create_directories(path_.parent_path());
        nlohmann::json root;
        root["version"] = AppConfig::CurrentVersion;
        root["connection"] = {{"deviceUri", config.connection.deviceUri},
                              {"memoryMapPath", config.connection.memoryMapPath},
                              {"useMemoryMap", config.connection.useMemoryMap},
                              {"debugOutput", config.connection.debugOutput},
                              {"waitForProcessSeconds", config.connection.waitForProcessSeconds}};
        root["players"] = {{"pollIntervalMs", config.players.pollIntervalMs}, {"showInactive", config.players.showInactive}};
        root["fuser"] = {{"startOnLaunch", config.fuser.startOnLaunch},
                         {"showNames", config.fuser.showNames},
                         {"showSkeleton", config.fuser.showSkeleton},
                         {"scale", config.fuser.scale},
                         {"transparentBackground", config.fuser.transparentBackground},
                         {"backgroundColor", config.fuser.backgroundColor},
                         {"viewportWidth", config.fuser.viewportWidth},
                         {"viewportHeight", config.fuser.viewportHeight},
                         {"monitorIndex", config.fuser.monitorIndex},
                         {"fullscreen", config.fuser.fullscreen}};
        root["device"] = {{"type", DeviceTypeName(config.device.type)},
                          {"port", config.device.port},
                          {"connectOnStartup", config.device.connectOnStartup},
                          {"makcuMouseUnitsPerScreenPixelX", config.device.makcuMouseUnitsPerScreenPixelX},
                          {"makcuMouseUnitsPerScreenPixelY", config.device.makcuMouseUnitsPerScreenPixelY},
                          {"ferrumMouseUnitsPerScreenPixelX", config.device.ferrumMouseUnitsPerScreenPixelX},
                          {"ferrumMouseUnitsPerScreenPixelY", config.device.ferrumMouseUnitsPerScreenPixelY}};
        root["aim"] = {{"enabled", config.aim.enabled},
                       {"fireportAim", config.aim.fireportAim},
                       {"autoFire", config.aim.autoFire},
                       {"autoClickMode", AutoClickModeName(config.aim.autoClickMode)},
                       {"autoAimAssist", config.aim.autoAimAssist},
                       {"activationKey", config.aim.activationKey},
                       {"radiusPixels", config.aim.radiusPixels},
                       {"strength", config.aim.strength},
                       {"maxStepPixels", config.aim.maxStepPixels},
                       {"closestBone", config.aim.closestBone},
                       {"targetBoneIndex", config.aim.targetBoneIndex}};
        root["window"] = {{"width", config.window.width},         {"height", config.window.height},
                          {"alpha", config.window.alpha},         {"monitorIndex", config.window.monitorIndex},
                          {"fontIndex", config.window.fontIndex}, {"fontBold", config.window.fontBold},
                          {"textScale", config.window.textScale}};

        std::ofstream file(path_);
        if (!file.is_open())
        {
            error = "Could not open config for writing: " + path_.string();
            return false;
        }

        file << root.dump(2) << '\n';
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
