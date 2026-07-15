#pragma once

#include "VRUIButton.h"

namespace vrui
{
    /// A toggle button that maintains on/off state.
    /// Visually shows active state with an indicator.
    class VRUIToggleButton : public VRUIButton
    {
    public:
        using ToggleCallback = std::function<void(VRUIToggleButton*, bool)>;

        explicit VRUIToggleButton(const std::string& label, bool initialState = false,
                                   const std::string& nifPath = "",
                                   float width = 3.0f, float height = 1.5f);

        bool isToggled() const { return _toggled; }
        void setToggled(bool state);
        void setOnToggleHandler(ToggleCallback callback) { _onToggle = std::move(callback); }

    private:
        void handlePress(VRUIButton* btn);
        void applyToggleVisual();

        bool _toggled;
        ToggleCallback _onToggle;
    };
}
