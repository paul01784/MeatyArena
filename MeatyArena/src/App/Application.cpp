#include "Application.h"
#include "Version.h"

#include "../UI/Theme.h"
#include "../UI/IconsFontAwesome7.h"
#include "../UI/DisplayMonitors.h"
#include "../Core/Makcu/Makcu.h"
#include "../Core/Ferrum/Ferrum.h"
#include "../Core/Logging/Log.h"

#include <d3d9.h>
#include <timeapi.h>
#include <imgui.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#pragma comment(lib, "winmm.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
    constexpr auto MainWindowFrameInterval = std::chrono::microseconds(16667);
    constexpr auto BackgroundLoopInterval = std::chrono::microseconds(16667);
    constexpr auto ActiveFuserLoopInterval = std::chrono::microseconds(4167);

    LPDIRECT3D9 g_D3D = nullptr;
    LPDIRECT3DDEVICE9 g_Device = nullptr;
    D3DPRESENT_PARAMETERS g_PresentParameters{};

    bool CreateDevice(HWND window)
    {
        g_D3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!g_D3D)
            return false;

        ZeroMemory(&g_PresentParameters, sizeof(g_PresentParameters));
        g_PresentParameters.Windowed = TRUE;
        g_PresentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        g_PresentParameters.BackBufferFormat = D3DFMT_UNKNOWN;
        g_PresentParameters.EnableAutoDepthStencil = TRUE;
        g_PresentParameters.AutoDepthStencilFormat = D3DFMT_D16;
        g_PresentParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        return g_D3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_PresentParameters, &g_Device) >= 0;
    }

    void CleanupDevice()
    {
        if (g_Device)
        {
            g_Device->Release();
            g_Device = nullptr;
        }
        if (g_D3D)
        {
            g_D3D->Release();
            g_D3D = nullptr;
        }
    }

    void ResetDevice()
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
        if (g_Device->Reset(&g_PresentParameters) == D3DERR_INVALIDCALL)
            IM_ASSERT(false);
        ImGui_ImplDX9_CreateDeviceObjects();
    }

    LRESULT WINAPI WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
            return true;
        switch (message)
        {
        case WM_SIZE:
            if (g_Device && wParam != SIZE_MINIMIZED)
            {
                g_PresentParameters.BackBufferWidth = LOWORD(lParam);
                g_PresentParameters.BackBufferHeight = HIWORD(lParam);
                ResetDevice();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
} // namespace

Application::Application()
    : configStore_(GetExecutableDirectory() / "configs" / "meatyarena.json"), worldSource_(memory_), mainWindow_(config_, configStore_, memory_, players_, worldSource_, fuser_)
{
    std::string error;
    Log::Initialize(GetExecutableDirectory() / "logs" / "meatyarena.log");
    if (!configStore_.Load(config_, error))
        configStore_.Save(config_, error);
    if (config_.fuser.startOnLaunch)
        fuser_.Start();
    makcu.mouseUnitsPerScreenPixelX = config_.device.makcuMouseUnitsPerScreenPixelX;
    makcu.mouseUnitsPerScreenPixelY = config_.device.makcuMouseUnitsPerScreenPixelY;
    ferrum.mouseUnitsPerScreenPixelX = config_.device.ferrumMouseUnitsPerScreenPixelX;
    ferrum.mouseUnitsPerScreenPixelY = config_.device.ferrumMouseUnitsPerScreenPixelY;
    if (config_.device.connectOnStartup && !config_.device.port.empty())
    {
        if (config_.device.type == SerialDeviceType::Makcu)
            makcu.Connect(config_.device.port.c_str());
        else if (config_.device.type == SerialDeviceType::Ferrum)
            ferrum.Connect(config_.device.port.c_str());
    }
}

int Application::Run(HINSTANCE instance, int showCommand)
{
    const WNDCLASSEXW windowClass = {sizeof(WNDCLASSEXW), CS_CLASSDC, WindowProcedure, 0L, 0L, instance, nullptr, nullptr, nullptr, nullptr, L"MeatyArenaWindow", nullptr};
    RegisterClassExW(&windowClass);
    const auto monitors = DisplayMonitors::Enumerate();
    const int monitorIndex = std::clamp(config_.window.monitorIndex, 0, (std::max)(0, static_cast<int>(monitors.size()) - 1));
    const RECT work = monitors.empty() ? RECT{100, 100, 1020, 740} : monitors[monitorIndex].workArea;
    const int x = work.left + ((work.right - work.left) - config_.window.width) / 2;
    const int y = work.top + ((work.bottom - work.top) - config_.window.height) / 2;
    HWND window = CreateWindowW(windowClass.lpszClassName, AppVersion::WindowTitle, WS_OVERLAPPEDWINDOW, x, y, config_.window.width, config_.window.height, nullptr, nullptr,
                                windowClass.hInstance, nullptr);
    if (!CreateDevice(window))
    {
        CleanupDevice();
        UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    mainWindow_.SetWindowHandle(window);
    ImFont* defaultFont = io.Fonts->AddFontDefault();
    const std::filesystem::path iconPath = GetExecutableDirectory() / "fonts" / "fa-solid-900.otf";
    static const ImWchar iconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    const auto mergeIcons = [&](ImFont* destination, float size)
    {
        if (!destination || !std::filesystem::is_regular_file(iconPath))
            return;
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.PixelSnapH = true;
        iconConfig.DstFont = destination;
        if (!io.Fonts->AddFontFromFileTTF(iconPath.string().c_str(), size, &iconConfig, iconRanges))
            Log::Write("Font Awesome could not be loaded from " + iconPath.string());
    };
    mergeIcons(defaultFont, 17.0f);
    const char* fontPaths[3][2] = {{"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf"},
                                   {"C:\\Windows\\Fonts\\arial.ttf", "C:\\Windows\\Fonts\\arialbd.ttf"},
                                   {"C:\\Windows\\Fonts\\tahoma.ttf", "C:\\Windows\\Fonts\\tahomabd.ttf"}};
    std::array<std::array<ImFont*, 2>, 3> fonts{};
    for (int family = 0; family < 3; ++family)
        for (int weight = 0; weight < 2; ++weight)
        {
            fonts[family][weight] = io.Fonts->AddFontFromFileTTF(fontPaths[family][weight], 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
            if (fonts[family][weight])
                mergeIcons(fonts[family][weight], 18.0f);
            else
                Log::Write(std::string("Application font unavailable: ") + fontPaths[family][weight]);
        }
    mainWindow_.SetFonts(fonts);
    if (!std::filesystem::is_regular_file(iconPath))
        Log::Write("Font Awesome file is missing: " + iconPath.string());
    Theme::ApplyMeaty();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX9_Init(g_Device);

    const bool highResolutionTimer = timeBeginPeriod(1) == TIMERR_NOERROR;
    bool done = false;
    auto nextLoopFrame = std::chrono::steady_clock::now();
    auto nextMainWindowFrame = nextLoopFrame;
    while (!done)
    {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        mainWindow_.Render();
        ImGui::EndFrame();

        const auto renderNow = std::chrono::steady_clock::now();
        const bool highRefreshFuser = fuser_.IsRunning() && worldSource_.InRaid();
        HRESULT result = S_OK;
        if (!highRefreshFuser || renderNow >= nextMainWindowFrame)
        {
            g_Device->SetRenderState(D3DRS_ZENABLE, FALSE);
            g_Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            g_Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            const D3DCOLOR clearColor = D3DCOLOR_RGBA(11, 13, 14, 255);
            g_Device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clearColor, 1.0f, 0);
            if (g_Device->BeginScene() >= 0)
            {
                ImGui::Render();
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                g_Device->EndScene();
            }
            result = g_Device->Present(nullptr, nullptr, nullptr, nullptr);
            nextMainWindowFrame = highRefreshFuser ? renderNow + MainWindowFrameInterval : renderNow;
        }
        else
            ImGui::Render();

        ImGui::UpdatePlatformWindows();
        const bool idleFuser = fuser_.IsRunning() && !worldSource_.InRaid();
        const auto platformNow = std::chrono::steady_clock::now();
        static std::chrono::steady_clock::time_point nextIdleFuserFrame{};
        if (!idleFuser || platformNow >= nextIdleFuserFrame)
        {
            ImGui::RenderPlatformWindowsDefault();
            nextIdleFuserFrame = idleFuser ? platformNow + std::chrono::seconds(1) : platformNow;
        }
        mainWindow_.SetFuserOutputFps(idleFuser ? 1.0f : ImGui::GetIO().Framerate);
        if (result == D3DERR_DEVICELOST && g_Device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            ResetDevice();

        nextLoopFrame += highRefreshFuser ? ActiveFuserLoopInterval : BackgroundLoopInterval;
        const auto loopNow = std::chrono::steady_clock::now();
        if (nextLoopFrame < loopNow - std::chrono::milliseconds(100))
            nextLoopFrame = loopNow;
        std::this_thread::sleep_until(nextLoopFrame);
    }

    if (highResolutionTimer)
        timeEndPeriod(1);

    std::string error;
    configStore_.Save(config_, error);
    Log::Write("Application shutting down");
    players_.Stop();
    memory_.Disconnect();
    inputDevice.Disconnect();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
}

std::filesystem::path Application::GetExecutableDirectory()
{
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length)
        return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}
