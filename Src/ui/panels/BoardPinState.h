#pragma once

#include <RE/N/NiMatrix3.h>
#include <RE/N/NiPoint3.h>

#include <cstdint>

namespace dragonboard::ui::panels
{
    class BoardPinState
    {
    public:
        void Reset(float menuScale);
        bool SetPinned(bool pinned);
        void Advance(float deltaTime);
        bool IsGraceExpired() const { return _graceTimer <= 0.0f; }
        bool AdvanceDebugFrame();

        void RememberHandScale(float scale) { _handMenuScale = scale; }
        void Capture(
            const RE::NiPoint3& position,
            const RE::NiMatrix3& rotation,
            float worldScale,
            float menuScaleBase);
        void SetFallbackScale(float menuScale);

        bool IsPinned() const { return _pinned; }
        const RE::NiPoint3& GetPosition() const { return _position; }
        const RE::NiMatrix3& GetRotation() const { return _rotation; }
        float GetWorldScale() const { return _worldScale; }
        float GetMenuScaleBase() const { return _menuScaleBase; }
        float GetHandMenuScale() const { return _handMenuScale; }

    private:
        bool _pinned = false;
        float _graceTimer = 0.0f;
        RE::NiPoint3 _position;
        RE::NiMatrix3 _rotation;
        float _worldScale = 1.0f;
        float _menuScaleBase = 1.0f;
        float _handMenuScale = 1.0f;
        std::uint32_t _debugFrameCounter = 0;
    };
}
