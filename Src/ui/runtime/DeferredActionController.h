#pragma once

#include <functional>
#include <string>

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::runtime
{
    class DeferredActionController
    {
    public:
        static void RequestSettingsSave(vrui::VRMenuManager& manager, float delay);
        static void SaveSettingsNow(vrui::VRMenuManager& manager);
        static void Schedule(
            vrui::VRMenuManager& manager,
            float delaySeconds,
            std::function<void()> task);
        static void Process(vrui::VRMenuManager& manager, float deltaTime);
        static void ExecuteConsoleCommand(
            vrui::VRMenuManager& manager,
            const std::string& command,
            bool isDangerous,
            const char* logLabel);
    };
}
