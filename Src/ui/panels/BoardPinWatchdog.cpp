#include "BoardPinWatchdog.h"

#include "BoardPinState.h"
#include "vrui/VRUIPanel.h"

#include <RE/N/NiNode.h>

namespace dragonboard::ui::panels
{
    namespace
    {
        std::shared_ptr<vrui::VRUIPanel> FindShownPanel(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const char* name)
        {
            for (const auto& panel : panels) {
                if (panel && panel->getName() == name && panel->isShown() && panel->getNode()) {
                    return panel;
                }
            }
            return nullptr;
        }
    }

    bool BoardPinWatchdog::ShouldReturnToHand(
        BoardPinState& state,
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
        RE::NiNode* menuHandNode,
        float deltaTime,
        bool verboseLogging,
        float maxDistance)
    {
        if (!state.IsPinned()) return false;

        state.Advance(deltaTime);
        const auto backgroundPanel = FindShownPanel(panels, "Background_Panel");
        if (verboseLogging && state.AdvanceDebugFrame() && backgroundPanel) {
            const auto currentPosition = backgroundPanel->getWorldPosition();
            const RE::NiPoint3 delta = currentPosition - state.GetPosition();
            const char* parentName =
                (backgroundPanel->getNode()->parent && !backgroundPanel->getNode()->parent->name.empty()) ?
                    backgroundPanel->getNode()->parent->name.c_str() : "<unnamed>";
            logger::trace(
                "DragonBoardVR: PinnedDebug panel='{}' parent='{}' savedPos=({:.2f},{:.2f},{:.2f}) currentPos=({:.2f},{:.2f},{:.2f}) delta={:.3f} savedScale={:.3f} currentScale={:.3f}",
                backgroundPanel->getName(),
                parentName,
                state.GetPosition().x, state.GetPosition().y, state.GetPosition().z,
                currentPosition.x, currentPosition.y, currentPosition.z,
                delta.Length(),
                state.GetWorldScale(),
                backgroundPanel->getWorldScale());
        }

        if (!menuHandNode) return false;

        RE::NiPoint3 boardPosition = state.GetPosition();
        if (backgroundPanel) {
            boardPosition = backgroundPanel->getWorldPosition();
        } else if (const auto mainPanel = FindShownPanel(panels, "MainPanel")) {
            boardPosition = mainPanel->getWorldPosition();
        }

        if (state.IsGraceExpired() &&
            (boardPosition - menuHandNode->world.translate).Length() > maxDistance) {
            logger::trace(
                "DragonBoardVR: Board world pin exceeded {:.1f} units from player. Returning to hand.",
                maxDistance);
            return true;
        }

        return false;
    }
}
