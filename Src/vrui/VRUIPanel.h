#pragma once

#include "VRUIContainer.h"
#include "VRUIButton.h"

namespace vrui
{
    /// Root-level panel that attaches to a player hand node.
    /// Manages the lifecycle of a complete menu attached to the non-dominant hand.
    class VRUIPanel : public VRUIContainer
    {
    public:
        /// @param name      Panel identifier
        explicit VRUIPanel(const std::string& name, float scale = 1.0f, bool drawsBackground = false);

        /// Attach this panel to a specific NiNode in the player skeleton
        void attachToHandNode(RE::NiNode* handNode, const RE::NiPoint3& offset = {0, 5, 10});
        void detachFromHandNode();

        /// Show/hide the panel (with optional animation)
        virtual void show();
        void hide();
        bool isShown() const { return _shown; }
        bool drawsBackground() const { return _drawsBackground; }
        void setPointerSurface(bool enabled) { _pointerSurface = enabled; }
        bool isPointerSurface() const { return _pointerSurface; }
        
        void setActive(bool active) { _active = active; if (!active) hide(); }
        bool isActive() const { return _active; }

        /// Update panel each frame
        void update(float deltaTime) override;

        void recalculateLayout() override;
        void onChildLayoutChanged(VRUIWidget* child) override;

        /// Collect all interactive buttons in this panel (recursive)
        void collectButtons(std::vector<VRUIButton*>& outButtons);
        RE::NiPoint3 getWorldPosition() const;
        RE::NiMatrix3 getWorldRotation() const;
        float getWorldScale() const;
        RE::NiNode* getBackgroundNode() const { return _backgroundNode.get(); }

    private:
        void collectButtonsRecursive(VRUIWidget* widget, std::vector<VRUIButton*>& outButtons);
        void applyPinnedWorldTransform();

        bool _shown = false;
        bool _active = true;
        bool _drawsBackground = false;
        bool _pointerSurface = false;
        bool _backgroundLoadFailed = false;
        RE::NiPointer<RE::NiNode> _trackingHandNode;
        RE::NiPointer<RE::NiNode> _backgroundNode;
        RE::NiPoint3 _offset;
        
        // Transform smoothing state
        RE::NiPoint3 _currentWorldPos;
        RE::NiMatrix3 _currentWorldRot;
        RE::NiPoint3 _lastPlayerPos;
        bool _hasTargetTransform = false;
        float _fadeTimer = 0.0f;
        float _lastAlpha = -1.0f;
        static constexpr float kFadeDuration = 0.2f;
    };
}
