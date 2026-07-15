#pragma once

#include <RE/Skyrim.h>

namespace dragonboard::ui::pointer
{
    class PointerVisualState
    {
    public:
        RE::NiPointer<RE::NiNode>& Beam() { return _beam; }
        const RE::NiPointer<RE::NiNode>& Beam() const { return _beam; }

        RE::NiPointer<RE::NiNode>& Reticle() { return _reticle; }
        const RE::NiPointer<RE::NiNode>& Reticle() const { return _reticle; }

        bool IsActive() const { return _active; }
        void SetActive(bool active) { _active = active; }
        bool IsReticleSuppressed() const { return _reticleSuppressed; }
        void SetReticleSuppressed(bool suppressed) { _reticleSuppressed = suppressed; }

        RE::NiPoint3& SmoothedPosition() { return _smoothedPosition; }
        const RE::NiPoint3& SmoothedPosition() const { return _smoothedPosition; }

    private:
        RE::NiPointer<RE::NiNode> _beam;
        RE::NiPointer<RE::NiNode> _reticle;
        bool _active = false;
        bool _reticleSuppressed = false;
        RE::NiPoint3 _smoothedPosition;
    };
}
