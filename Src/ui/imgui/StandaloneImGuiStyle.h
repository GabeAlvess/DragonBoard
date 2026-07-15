#pragma once

#include <cstdint>

struct ImGuiIO;

namespace dragonboard::ui::imgui::standalone
{
    inline constexpr std::uint32_t kPanelWidth = 1920;
    inline constexpr std::uint32_t kPanelHeight = 1080;
    inline constexpr float kSceneScreenSizeScale = 0.85f;

    void ConfigureFonts(ImGuiIO& io);
    void ApplyStyle();
    bool BeginPanel(const char* id, const char* heading, bool* open);
    bool SidebarItem(const char* label, bool selected, float height = 64.0f);
    void SectionHeading(const char* heading);
}
