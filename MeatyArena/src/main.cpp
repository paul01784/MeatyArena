#include "App/Application.h"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    Application application;
    return application.Run(instance, showCommand);
}
