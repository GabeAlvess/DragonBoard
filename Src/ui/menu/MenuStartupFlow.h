#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vrui
{
    class VRUIPanel;
}

namespace dragonboard::ui::menu
{
    struct MenuStartupResult
    {
        std::string action;
    };

    class MenuStartupFlow
    {
    public:
        static MenuStartupResult Prepare(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const std::string& configuredAction);
        static void TriggerEntranceAnimations(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels);
    };
}
