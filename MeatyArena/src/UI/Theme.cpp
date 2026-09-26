#include "Theme.h"

#include <imgui.h>

void Theme::ApplyMeaty()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.WindowRounding = 7.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 5.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.91f, 0.91f, 0.89f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.48f, 0.46f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.050f, 0.055f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.065f, 0.070f, 0.075f, 0.96f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.090f, 0.095f, 0.100f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.28f, 0.29f, 0.29f, 0.82f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.26f, 0.27f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.34f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.40f, 0.39f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.070f, 0.075f, 0.080f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.105f, 0.045f, 0.050f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.090f, 0.095f, 0.100f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.035f, 0.040f, 0.045f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.43f, 0.43f, 0.42f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.19f, 0.23f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.73f, 0.16f, 0.19f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.18f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.43f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.72f, 0.13f, 0.16f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.36f, 0.08f, 0.11f, 0.90f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.53f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.72f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.12f, 0.14f, 0.72f);
    colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.48f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.62f, 0.13f, 0.16f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.38f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.17f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30f, 0.31f, 0.31f, 0.82f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.21f, 0.22f, 0.82f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.11f, 0.12f, 0.13f, 0.52f);
}
