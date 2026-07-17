#include "PointerInteractionController.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUIRaycaster.h"
#include "vrui/VRUISettings.h"
#include "ui/rml/RmlPanelHost.h"
#include "ui/pointer/PointerVisualController.h"

#include <cmath>

namespace dragonboard::ui::input
{
    namespace
    {
        std::shared_ptr<vrui::VRUIPanel> ResolveVisualSurface(
            const std::shared_ptr<vrui::VRUIPanel>& hitPanel,
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const RE::NiPoint3& rayOrigin,
            const RE::NiPoint3& rayDirection,
            float maxInteractDistance)
        {
            if (!hitPanel) return nullptr;

            std::shared_ptr<vrui::VRUIPanel> largestSurface;
            float largestSurfaceArea = 0.0f;
            for (const auto& panel : panels) {
                if (panel && panel->isPointerSurface() &&
                    panel->isActive() && panel->isShown() && panel->getNode()) {
                    float surfaceHitDistance = 0.0f;
                    if (panel->hitTest(rayOrigin, rayDirection, surfaceHitDistance) &&
                        surfaceHitDistance > 0.0f &&
                        surfaceHitDistance <= maxInteractDistance) {
                        const float surfaceArea = panel->getWidth() * panel->getHeight();
                        if (!largestSurface || surfaceArea > largestSurfaceArea) {
                            largestSurface = panel;
                            largestSurfaceArea = surfaceArea;
                        }
                    }
                }
            }

            if (largestSurface) return largestSurface;

            return hitPanel;
        }

        RE::NiPoint3 ResolvePanelSurfaceHit(
            const RE::NiPoint3& rayOrigin,
            const RE::NiPoint3& rayDirection,
            RE::NiNode* panelNode,
            float fallbackDistance)
        {
            if (panelNode) {
                const auto& rotation = panelNode->world.rotate;
                const RE::NiPoint3 normal(
                    rotation.entry[0][1], rotation.entry[1][1], rotation.entry[2][1]);
                const float denominator =
                    rayDirection.x * normal.x +
                    rayDirection.y * normal.y +
                    rayDirection.z * normal.z;

                if (std::abs(denominator) > 1e-5f) {
                    const RE::NiPoint3 toPlane(
                        panelNode->world.translate.x - rayOrigin.x,
                        panelNode->world.translate.y - rayOrigin.y,
                        panelNode->world.translate.z - rayOrigin.z);
                    const float distance =
                        (toPlane.x * normal.x + toPlane.y * normal.y + toPlane.z * normal.z) /
                        denominator;
                    if (std::isfinite(distance) && distance > 0.0f) {
                        return {
                            rayOrigin.x + rayDirection.x * distance,
                            rayOrigin.y + rayDirection.y * distance,
                            rayOrigin.z + rayDirection.z * distance
                        };
                    }
                }
            }

            return {
                rayOrigin.x + rayDirection.x * fallbackDistance,
                rayOrigin.y + rayDirection.y * fallbackDistance,
                rayOrigin.z + rayDirection.z * fallbackDistance
            };
        }
    }

    void PointerInteractionController::Process(vrui::VRMenuManager& manager, float deltaTime)
    {
        auto* dominantHand = manager.getDominantHandNode();
        if (!dominantHand) return;

        auto& settings = vrui::VRUISettings::get();
        RE::NiPoint3 rayOrigin = dominantHand->world.translate;

        RE::NiMatrix3& rot = dominantHand->world.rotate;
        RE::NiPoint3 rayDir(rot.entry[0][2], rot.entry[1][2], rot.entry[2][2]);

        const float maxInteractDist = manager._boardPinState.IsPinned() ? 500.0f : 50.0f;
        auto raycastResult = vrui::VRUIRaycaster::performRaycast(
            rayOrigin, rayDir, manager._panelRegistry.GetPanels(), maxInteractDist);

        const bool hitSomething = raycastResult.hitSomething;
        std::shared_ptr<vrui::VRUIWidget> touchedWidget = raycastResult.touchedWidget;
        if (auto previewTarget =
                dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                    .GetPreviewInteractionTarget()) {
            touchedWidget = std::move(previewTarget);
        }
        const std::shared_ptr<vrui::VRUIPanel> visualSurface = ResolveVisualSurface(
            raycastResult.closestPanel,
            manager._panelRegistry.GetPanels(),
            rayOrigin,
            rayDir,
            maxInteractDist);
        const float closestVisualDist = raycastResult.closestVisualDist;
        [[maybe_unused]] const float closestInteractDist = raycastResult.closestInteractDist;

        if (auto grabbed = manager._interactionFocus.GetGrabbed()) {
            touchedWidget = grabbed;
        }

        const auto hoverTransition = manager._interactionFocus.UpdateHover(
            touchedWidget, deltaTime, vrui::VRMenuManager::kHoverLockTime);
        if (hoverTransition.changed) {
            if (hoverTransition.previous) {
                hoverTransition.previous->onRayExit();
            }

            if (hoverTransition.current) {
                hoverTransition.current->onRayEnter();

                if (auto* button = dynamic_cast<vrui::VRUIButton*>(hoverTransition.current.get())) {
                    std::string label = button->getLabel();
                    if (!label.empty()) {
                        // Reserved for an optional hover notification.
                    }
                }

                if (settings.hapticOnHover) {
                    manager.triggerHaptic(
                        true, settings.hapticIntensity * 0.5f, settings.hapticDuration);
                }
            }
        }

        if (hitSomething && visualSurface && visualSurface->getNode()) {
            const RE::NiPoint3 hitPoint = ResolvePanelSurfaceHit(
                rayOrigin, rayDir, visualSurface->getNode(), closestVisualDist);
            dragonboard::ui::pointer::PointerVisualController::Update(
                manager, dominantHand, hitPoint, visualSurface->getNode());
        } else {
            dragonboard::ui::pointer::PointerVisualController::Hide(manager);
        }
    }
}
