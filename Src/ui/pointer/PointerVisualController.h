#pragma once

namespace RE
{
    class NiNode;
    class NiPoint3;
}

namespace vrui
{
    class VRMenuManager;
}

namespace dragonboard::ui::pointer
{
    class PointerVisualController
    {
    public:
        static void Update(
            vrui::VRMenuManager& manager,
            RE::NiNode* dominantHand,
            const RE::NiPoint3& hitPosition,
            RE::NiNode* panelNode);
        static void Hide(vrui::VRMenuManager& manager);
        static void SetReticleSuppressed(vrui::VRMenuManager& manager, bool suppressed);
    };
}
