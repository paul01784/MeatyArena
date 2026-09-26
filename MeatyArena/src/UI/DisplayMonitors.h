#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace DisplayMonitors
{
    struct Monitor
    {
        RECT bounds{};
        RECT workArea{};
        std::string label;
    };

    inline std::vector<Monitor> Enumerate()
    {
        std::vector<Monitor> monitors;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR handle, HDC, LPRECT, LPARAM context) -> BOOL
            {
                auto& list = *reinterpret_cast<std::vector<Monitor>*>(context);
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (!GetMonitorInfoW(handle, &info))
                    return TRUE;

                DEVMODEW mode{};
                mode.dmSize = sizeof(mode);
                const bool hasMode = EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) != 0;
                Monitor monitor;
                monitor.bounds = info.rcMonitor;
                monitor.workArea = info.rcWork;
                const int width = info.rcMonitor.right - info.rcMonitor.left;
                const int height = info.rcMonitor.bottom - info.rcMonitor.top;
                monitor.label = "Display " + std::to_string(list.size() + 1) + "  " + std::to_string(width) + " x " + std::to_string(height);
                if (hasMode && mode.dmDisplayFrequency > 1)
                    monitor.label += " @ " + std::to_string(mode.dmDisplayFrequency) + " Hz";
                monitor.label += "  (" + std::to_string(info.rcMonitor.left) + ", " + std::to_string(info.rcMonitor.top) + ")";
                if (info.dwFlags & MONITORINFOF_PRIMARY)
                    monitor.label += "  Primary";
                list.push_back(std::move(monitor));
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors));
        return monitors;
    }
} 
