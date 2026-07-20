#pragma once

#include <RE/N/NiNode.h>

namespace dragonboard::diagnostics
{
    class FingerTrackingProbe
    {
    public:
        static FingerTrackingProbe& GetSingleton();

        void Update(
            RE::NiNode* skeletonRoot,
            bool isVrikInstalled,
            bool boardOpen,
            bool enabled,
            bool showMarkers,
            float markerScale,
            float tipExtension,
            const RE::NiPoint3& touchLocalOffset,
            float logIntervalSeconds,
            float deltaTime);

        void Reset();

    private:
        FingerTrackingProbe() = default;

        RE::NiNode* _lastSkeletonRoot = nullptr;
        RE::NiPointer<RE::NiNode> _leftMarker;
        RE::NiPointer<RE::NiNode> _rightMarker;
        float _elapsed = 0.0f;
        bool _active = false;

        void DetachMarkers();
        void HideMarkers();
    };
}
