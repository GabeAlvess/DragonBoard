#include "ui/imgui/StandaloneImGuiStyle.h"

#include <algorithm>
#include <array>
#include <filesystem>

#include <imgui.h>

namespace dragonboard::ui::imgui::standalone
{
    void ConfigureFonts(ImGuiIO& io)
    {
        // The packaged path is preferred. The Windows path is a development
        // fallback and keeps this checkout immediately testable.
        constexpr std::array<const char*, 2> fontCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/DragonBoardVR_Font.ttf",
            "C:/Windows/Fonts/BarlowCondensed-Regular.ttf"
        };
        for (const auto* path : fontCandidates) {
            if (!std::filesystem::exists(path)) continue;
            // The physical 1920x1080 texture is viewed at arm's length in VR;
            // use a display font large enough to occupy that surface.
            if (io.Fonts->AddFontFromFileTTF(path, 56.0f)) {
                io.FontGlobalScale = 1.0f;
                return;
            }
        }

        // Preserve the previously accepted text size when no condensed font
        // is available on an end user's system.
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = 4.3f;
    }

    void ApplyStyle()
    {
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(16.0f, 12.0f);
        style.FramePadding = ImVec2(18.0f, 12.0f);
        style.CellPadding = ImVec2(10.0f, 8.0f);
        style.ItemSpacing = ImVec2(16.0f, 16.0f);
        style.ItemInnerSpacing = ImVec2(12.0f, 10.0f);
        style.ScrollbarSize = 24.0f;
        style.GrabMinSize = 18.0f;
        style.MouseCursorScale = 2.0f;
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.WindowBorderSize = 2.0f;
        style.ChildBorderSize = 2.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 1.0f;

        auto& colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.96f, 0.95f, 0.92f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.57f, 0.54f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.022f, 0.020f, 0.76f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.020f, 0.018f, 0.017f, 0.50f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.035f, 0.032f, 0.030f, 0.90f);
        colors[ImGuiCol_Border] = ImVec4(0.52f, 0.51f, 0.49f, 0.95f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.08f, 0.075f, 0.55f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.19f, 0.18f, 0.66f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.29f, 0.28f, 0.26f, 0.76f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.025f, 0.022f, 0.020f, 0.70f);
        colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBg];
        colors[ImGuiCol_Button] = ImVec4(0.07f, 0.065f, 0.06f, 0.32f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.21f, 0.20f, 0.62f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.33f, 0.31f, 0.76f);
        colors[ImGuiCol_Header] = ImVec4(0.12f, 0.11f, 0.10f, 0.54f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.24f, 0.22f, 0.68f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.36f, 0.35f, 0.32f, 0.80f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.94f, 0.90f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.68f, 0.67f, 0.63f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.91f, 0.87f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.50f, 0.49f, 0.47f, 0.90f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.77f, 0.75f, 0.70f, 1.00f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.95f, 0.93f, 0.87f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.015f, 0.014f, 0.013f, 0.48f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.32f, 0.31f, 0.29f, 0.82f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.48f, 0.47f, 0.44f, 0.92f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.64f, 0.62f, 0.58f, 1.00f);
    }

    bool BeginPanel(const char* id, const char* heading, bool* open)
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::Begin(id, open, flags)) return false;
        ImGui::TextUnformatted(heading);
        ImGui::Separator();
        return true;
    }

    bool SidebarItem(const char* label, bool selected, float height)
    {
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.83f, 0.78f, 1.0f));
        }
        const float responsiveHeight = std::max(height, ImGui::GetFrameHeight() + 18.0f);
        const bool pressed = ImGui::Button(label, ImVec2(-1.0f, responsiveHeight));
        if (selected) ImGui::PopStyleColor(2);
        return pressed;
    }

    void SectionHeading(const char* heading)
    {
        const float width = ImGui::CalcTextSize(heading).x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
            ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - width) * 0.5f));
        ImGui::TextUnformatted(heading);
        ImGui::Separator();
    }
}
