#pragma once

namespace dragonboard::ui::input
{
    struct PressHoldEvents
    {
        bool longPress = false;
        bool released = false;
        bool shortPress = false;
    };

    class PressHoldTracker
    {
    public:
        PressHoldEvents Update(bool pressed, float deltaTime, float longPressTime, bool allowLongPress = true);
        void Reset();
        void SuppressUntilRelease() { _suppressUntilRelease = true; }
        bool IsPressed() const { return _pressed; }

    private:
        float _holdTimer = 0.0f;
        bool _pressed = false;
        bool _longPressTriggered = false;
        bool _suppressUntilRelease = false;
    };
}
