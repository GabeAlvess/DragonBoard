#pragma once

namespace dragonboard::ui::input
{
    class InputButtonState
    {
    public:
        void SetGrip(bool pressed) { _grip = pressed; }
        void SetDominantGrip(bool pressed) { _dominantGrip = pressed; }
        void SetOffhandGrip(bool pressed) { _offhandGrip = pressed; }
        void SetTrigger(bool pressed) { _trigger = pressed; }
        void SetOffhandTrigger(bool pressed) { _offhandTrigger = pressed; }
        void SetThumbstick(bool pressed) { _thumbstick = pressed; }
        void SetSecondary(bool pressed) { _secondary = pressed; }
        void SetDominantSecondary(bool pressed) { _dominantSecondary = pressed; }
        void SetHotkey8(bool pressed) { _hotkey8 = pressed; }
        void SetActivationTrigger(bool pressed) { _activationTrigger = pressed; }

        bool Grip() const { return _grip; }
        bool DominantGrip() const { return _dominantGrip; }
        bool OffhandGrip() const { return _offhandGrip; }
        bool Trigger() const { return _trigger; }
        bool OffhandTrigger() const { return _offhandTrigger; }
        bool Thumbstick() const { return _thumbstick; }
        bool Secondary() const { return _secondary; }
        bool DominantSecondary() const { return _dominantSecondary; }
        bool Hotkey8() const { return _hotkey8; }
        bool ActivationTrigger() const { return _activationTrigger; }

    private:
        bool _grip = false;
        bool _dominantGrip = false;
        bool _offhandGrip = false;
        bool _trigger = false;
        bool _offhandTrigger = false;
        bool _thumbstick = false;
        bool _secondary = false;
        bool _dominantSecondary = false;
        bool _hotkey8 = false;
        bool _activationTrigger = false;
    };
}
