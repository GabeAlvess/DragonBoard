#pragma once

#include "VRUIWidget.h"
#include "VRUIPanel.h"
#include <vector>
#include <memory>
#include <RE/N/NiPoint3.h>

namespace vrui {
    struct RaycastHitResult {
        bool hitSomething = false;
        std::shared_ptr<VRUIWidget> touchedWidget = nullptr;
        std::shared_ptr<VRUIPanel> closestPanel = nullptr;
        float closestVisualDist = 50.0f;
        float closestInteractDist = 50.0f;
    };

    class VRUIRaycaster {
    public:
        // Performs both the strict interactive raycast and the expanded visual raycast
        // to find the hovered widget and the panel hit point.
        static RaycastHitResult performRaycast(
            const RE::NiPoint3& rayOrigin,
            const RE::NiPoint3& rayDir,
            const std::vector<std::shared_ptr<VRUIPanel>>& panels,
            float maxInteractDist = 50.0f
        );
    };
}
