#pragma once

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::refresh
{
    class RefreshPipelineController
    {
    public:
        static void RefreshFixedWidgets(vrui::VRMenuManager& manager);
        static void UpdatePanelTransforms(vrui::VRMenuManager& manager);
        static void ApplyFullRefresh(vrui::VRMenuManager& manager);
        static void ProcessPending(vrui::VRMenuManager& manager);
    };
}
