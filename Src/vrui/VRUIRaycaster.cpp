#include "VRUIRaycaster.h"
#include "VRUIButton.h"
#include "VRUIContainer.h"

namespace vrui {

    RaycastHitResult VRUIRaycaster::performRaycast(
        const RE::NiPoint3& rayOrigin,
        const RE::NiPoint3& rayDir,
        const std::vector<std::shared_ptr<VRUIPanel>>& panels,
        float maxInteractDist)
    {
        RaycastHitResult result;
        result.closestVisualDist = maxInteractDist;
        result.closestInteractDist = maxInteractDist;

        std::function<void(std::shared_ptr<VRUIWidget>, std::shared_ptr<VRUIPanel>)> recursiveRaycast;
        recursiveRaycast = [&](std::shared_ptr<VRUIWidget> widget, std::shared_ptr<VRUIPanel> ownerPanel) {
            if (!widget || !widget->isVisible()) return;

            float hitDist = 0.0f;
            const auto* panelWidget = dynamic_cast<VRUIPanel*>(widget.get());
            const bool isNonSurfaceContainer =
                dynamic_cast<VRUIContainer*>(widget.get()) != nullptr &&
                (!panelWidget || !panelWidget->isPointerSurface());
            if (!isNonSurfaceContainer && widget->hitTest(rayOrigin, rayDir, hitDist)) {
                if (hitDist > 0.0f && hitDist <= maxInteractDist) {
                    result.hitSomething = true;
                    if (hitDist < result.closestVisualDist) {
                        result.closestVisualDist = hitDist;
                        result.closestPanel = ownerPanel;
                    }
                    
                    // Interaction focus for buttons OR draggable components
                    if (dynamic_cast<VRUIButton*>(widget.get())) {
                        if (hitDist < result.closestInteractDist) {
                            result.closestInteractDist = hitDist;
                            result.touchedWidget = widget;
                        }
                    }
                }
            }

            // Always Recurse
            for (auto& child : widget->getChildren()) {
                recursiveRaycast(child, ownerPanel);
            }
        };

        for (auto& panel : panels) {
            if (panel->isActive() && panel->isShown()) {
                recursiveRaycast(panel, panel);
            }
        }

        // Secondary expanded pass for the VISUAL laser/reticle only (+3.0f padding on each side)
        // This makes the reticle appear even slightly outside the logical button boundaries
        if (!result.hitSomething) {
            std::function<void(std::shared_ptr<VRUIWidget>, std::shared_ptr<VRUIPanel>)> expandedRaycast;
            expandedRaycast = [&](std::shared_ptr<VRUIWidget> widget, std::shared_ptr<VRUIPanel> ownerPanel) {
                if (!widget || !widget->isVisible()) return;
                
                // Only test panels/backgrounds for the expanded pass! Buttons/Icons don't need artificial visual magnetism 
                // as that causes floating reticles when dealing with standalone/pinned floating icons without a backplane.
                const auto* panelWidget = dynamic_cast<VRUIPanel*>(widget.get());
                const bool isNonSurfaceContainer =
                    dynamic_cast<VRUIContainer*>(widget.get()) != nullptr &&
                    (!panelWidget || !panelWidget->isPointerSurface());
                if (widget->getNode() &&
                    !dynamic_cast<VRUIButton*>(widget.get()) &&
                    !isNonSurfaceContainer) {
                    const auto& t = widget->getNode()->world;
                    float wS = (t.scale > 0.001f) ? t.scale : 1.0f;
                    RE::NiPoint3 diff(rayOrigin.x-t.translate.x, rayOrigin.y-t.translate.y, rayOrigin.z-t.translate.z);
                    // Origin: rotated AND divided by scale (position transform)
                    RE::NiPoint3 lO(
                        (t.rotate.entry[0][0]*diff.x + t.rotate.entry[1][0]*diff.y + t.rotate.entry[2][0]*diff.z) / wS,
                        (t.rotate.entry[0][1]*diff.x + t.rotate.entry[1][1]*diff.y + t.rotate.entry[2][1]*diff.z) / wS,
                        (t.rotate.entry[0][2]*diff.x + t.rotate.entry[1][2]*diff.y + t.rotate.entry[2][2]*diff.z) / wS
                    );
                    // Direction: only rotated, NOT divided by scale (direction transform)
                    RE::NiPoint3 lD(
                        t.rotate.entry[0][0]*rayDir.x + t.rotate.entry[1][0]*rayDir.y + t.rotate.entry[2][0]*rayDir.z,
                        t.rotate.entry[0][1]*rayDir.x + t.rotate.entry[1][1]*rayDir.y + t.rotate.entry[2][1]*rayDir.z,
                        t.rotate.entry[0][2]*rayDir.x + t.rotate.entry[1][2]*rayDir.y + t.rotate.entry[2][2]*rayDir.z
                    );
                    float localScale = widget->getNode()->local.scale > 0.001f ? widget->getNode()->local.scale : 1.0f;
                    float hw = ((widget->getWidth()  * 0.5f) + 3.0f) / localScale;
                    float hh = ((widget->getHeight() * 0.5f) + 3.0f) / localScale;
                    float hd = 1.0f / localScale;
                    
                    vrui::AABB expAABB;
                    expAABB.min = { -hw, -hd, -hh };
                    expAABB.max = {  hw,  hd,  hh };
                    float localHitDist = 0.0f;
                    if (expAABB.intersectsRay(lO, lD, localHitDist)) {
                        // Convert local-space distance back to world-space
                        float worldHitDist = localHitDist * wS;
                        if (worldHitDist > 0.0f && worldHitDist <= maxInteractDist) {
                            result.hitSomething = true;
                            if (worldHitDist < result.closestVisualDist) {
                                result.closestVisualDist = worldHitDist;
                                result.closestPanel = ownerPanel;
                            }
                        }
                    }
                }
                for (auto& child : widget->getChildren()) expandedRaycast(child, ownerPanel);
            };
            for (auto& panel : panels) {
                if (panel->isActive() && panel->isShown()) {
                    expandedRaycast(panel, panel);
                }
            }
        }

        return result;
    }

}
