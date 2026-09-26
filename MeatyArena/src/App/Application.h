#pragma once

#include "AppConfig.h"
#include "../Config/ConfigStore.h"
#include "../Core/InputDevice.h"
#include "../Fuser/FuserController.h"
#include "../Memory/MemoryClient.h"
#include "../GameWorld/WorldSource.h"
#include "../Players/PlayerService.h"
#include "../UI/MainWindow.h"

#include <Windows.h>

#include <filesystem>

class Application
{
public:
    Application();
    int Run(HINSTANCE instance, int showCommand);

private:
    static std::filesystem::path GetExecutableDirectory();

    AppConfig config_;
    ConfigStore configStore_;
    MemoryClient memory_;
    PlayerService players_;
    WorldSource worldSource_;
    FuserController fuser_;
    MainWindow mainWindow_;
};
