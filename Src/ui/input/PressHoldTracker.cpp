#include "PressHoldTracker.h"

namespace dragonboard::ui::input
{
    PressHoldEvents PressHoldTracker::Update(
        bool pressed,
        float deltaTime,
        float longPressTime,
        bool allowLongPress)
    {
        PressHoldEvents events;

        if (_suppressUntilRelease) {
            if (!pressed) Reset();
            return events;
        }

        if (pressed && !_pressed) {
            _pressed = true;
            _holdTimer = 0.0f;
            _longPressTriggered = false;
        } else if (pressed) {
            _holdTimer += deltaTime;
            if (_holdTimer >= longPressTime && !_longPressTriggered && allowLongPress) {
                _longPressTriggered = true;
                events.longPress = true;
            }
        } else if (_pressed) {
            events.released = true;
            events.shortPress = !_longPressTriggered;
            Reset();
        }

        return events;
    }

    void PressHoldTracker::Reset()
    {
        _holdTimer = 0.0f;
        _pressed = false;
        _longPressTriggered = false;
        _suppressUntilRelease = false;
    }
}
