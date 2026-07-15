#pragma once

#include "VRUIWidget.h"
#include <functional>

namespace vrui
{
    /// A slider widget for selecting numerical values.
    class VRUISlider : public VRUIWidget
    {
    public:
        using ValueChangedCallback = std::function<void(float)>;

        VRUISlider(const std::string& name, float minValue, float maxValue, float defaultValue,
                   float width = 6.0f, float height = 1.0f);

        float getValue() const { return _currentValue; }
        void setValue(float value, bool triggerCallback = true);

        void setOnValueChanged(ValueChangedCallback callback) { _onValueChanged = std::move(callback); }

        // --- Input Events ---
        void onRayEnter() override;
        void onRayExit() override;
        void onTriggerPress(EquipHand hand = EquipHand::kRight) override;
        void onTriggerRelease(EquipHand hand = EquipHand::kRight) override;

        // --- Per-Frame ---
        void update(float deltaTime) override;

        void initializeVisuals() override;

    private:
        void updateHandlePosition();
        float calculateValueFromRay(const RE::NiPoint3& worldOrigin, const RE::NiPoint3& worldDir);

        float _minValue;
        float _maxValue;
        float _currentValue;
        bool _isDragging = false;
        bool _isHovered = false;

        RE::NiPointer<RE::NiNode> _backgroundTrack;
        RE::NiPointer<RE::NiNode> _handle;

        ValueChangedCallback _onValueChanged;
    };
}
