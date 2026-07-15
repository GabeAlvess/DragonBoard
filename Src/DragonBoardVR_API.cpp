#include "DragonBoardVR_API.h"

#include "ui/rml/RmlPanelHost.h"

namespace
{
    using namespace DragonBoardVR_API;

    class DragonBoardVRAPIImpl final : public IDragonBoardVR
    {
    public:
        PanelHandle RegisterPanel(const PanelDescriptor* descriptor) noexcept override
        {
            if (!descriptor) return InvalidPanel;
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .RegisterExternalPanel(*descriptor);
        }

        void UnregisterPanel(PanelHandle panel) noexcept override
        {
            dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .UnregisterExternalPanel(panel);
        }

        bool ShowPanel(PanelHandle panel) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .ShowExternalPanel(panel);
        }

        void HidePanel(PanelHandle panel) noexcept override
        {
            dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .HideExternalPanel(panel);
        }

        bool IsPanelVisible(PanelHandle panel) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .IsExternalPanelVisible(panel);
        }

        bool SetElementText(
            PanelHandle panel, const char* elementId, const char* text) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .SetExternalElementText(panel, elementId, text);
        }

        bool SetElementAttribute(
            PanelHandle panel,
            const char* elementId,
            const char* name,
            const char* value) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .SetExternalElementAttribute(panel, elementId, name, value);
        }

        bool SetElementClass(
            PanelHandle panel,
            const char* elementId,
            const char* className,
            bool enabled) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .SetExternalElementClass(panel, elementId, className, enabled);
        }
    };

    DragonBoardVRAPIImpl g_api;
}

DragonBoardVR_API::IDragonBoardVR* GetDragonBoardVRAPI()
{
    return &g_api;
}
