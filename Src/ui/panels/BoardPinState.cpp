#include "BoardPinState.h"

#include <algorithm>

namespace dragonboard::ui::panels
{
    void BoardPinState::Reset(float menuScale)
    {
        _pinned = false;
        _graceTimer = 0.0f;
        _position = { 0.0f, 0.0f, 0.0f };
        _rotation.SetEulerAnglesXYZ(0.0f, 0.0f, 0.0f);
        _worldScale = menuScale;
        _menuScaleBase = menuScale;
        _handMenuScale = menuScale;
        _debugFrameCounter = 0;
    }

    bool BoardPinState::SetPinned(bool pinned)
    {
        if (_pinned == pinned) return false;

        _pinned = pinned;
        _graceTimer = pinned ? 0.5f : 0.0f;
        _debugFrameCounter = 0;
        return true;
    }

    void BoardPinState::Advance(float deltaTime)
    {
        if (_graceTimer > 0.0f) {
            _graceTimer -= deltaTime;
        }
    }

    bool BoardPinState::AdvanceDebugFrame()
    {
        return (++_debugFrameCounter % 30) == 0;
    }

    void BoardPinState::Capture(
        const RE::NiPoint3& position,
        const RE::NiMatrix3& rotation,
        float worldScale,
        float menuScaleBase)
    {
        _position = position;
        _rotation = rotation;
        _worldScale = worldScale;
        _menuScaleBase = std::max(0.001f, menuScaleBase);
    }

    void BoardPinState::SetFallbackScale(float menuScale)
    {
        _worldScale = menuScale;
        _menuScaleBase = std::max(0.001f, menuScale);
    }
}
