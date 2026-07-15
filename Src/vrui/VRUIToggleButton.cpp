#include "VRUIToggleButton.h"

namespace vrui
{
    VRUIToggleButton::VRUIToggleButton(const std::string& label, bool initialState,
                                       const std::string& nifPath,
                                       float width, float height)
        : VRUIButton(label, nifPath, "", width, height)
        , _toggled(initialState)
    {
        // Wire the base button press to toggle behavior
        setOnPressHandler([this](VRUIButton* btn, EquipHand) { handlePress(btn); });
        applyToggleVisual();
    }

    void VRUIToggleButton::setToggled(bool state)
    {
        _toggled = state;
        applyToggleVisual();
    }

    void VRUIToggleButton::handlePress([[maybe_unused]] VRUIButton* btn)
    {
        _toggled = !_toggled;
        applyToggleVisual();
        if (_onToggle) {
            _onToggle(this, _toggled);
        }
    }

    void VRUIToggleButton::applyToggleVisual()
    {
        if (!getNode()) return;

        // Visual indicator: shift the button slightly on Y when toggled (depth press effect)
        // And change scale subtly to indicate active state
        if (_toggled) {
            getNode()->local.translate.y = 0.15f;  // Slight push-in
        } else {
            getNode()->local.translate.y = 0.0f;
        }
    }
}
