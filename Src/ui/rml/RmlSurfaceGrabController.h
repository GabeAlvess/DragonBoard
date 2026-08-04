#pragma once

#include <RE/Skyrim.h>

namespace dragonboard::ui::rml
{
    class RmlSurfaceGrabController
    {
    public:
        struct Input
        {
            RE::NiNode* dominantHand = nullptr;
            RE::NiNode* offHand = nullptr;
            bool dominantGripDown = false;
            bool offHandGripDown = false;
            bool hovered = false;
            float minimumScale = 0.05f;
            float maximumScale = 20.0f;
            bool requireHover = true;
            float grabHoldSeconds = 1.0f;
            bool updateSceneGraph = true;
            float thumbstickY = 0.0f;
        };

        struct UpdateResult
        {
            bool grabStarted = false;
            bool transformChanged = false;
            bool grabEnded = false;
        };

        void SetEnabled(bool enabled);
        void Reset();
        [[nodiscard]] bool IsGrabbed() const { return _grabbed; }
        [[nodiscard]] UpdateResult Update(
            RE::NiNode* surfaceNode,
            const Input& input,
            float deltaTime);

    private:
        bool BeginGrab(RE::NiNode* surfaceNode, RE::NiNode* hand);

        bool _enabled = false;
        bool _grabbed = false;
        bool _thumbScaling = false;
        float _holdSeconds = 0.0f;
        RE::NiPoint3 _grabOffsetLocalHand{};
        RE::NiMatrix3 _grabInitialHandRotation{};
        RE::NiMatrix3 _grabInitialSurfaceWorldRotation{};
    };
}
