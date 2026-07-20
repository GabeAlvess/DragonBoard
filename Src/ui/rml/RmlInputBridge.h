#pragma once

#include <atomic>
#include <cstdint>

namespace dragonboard::ui::rml
{
    class RmlInputBridge
    {
    public:
        struct State
        {
            bool pointerOnPanel = false;
            float pointerU = 0.0f;
            float pointerV = 0.0f;
            bool triggerDown = false;
            bool gripDown = false;
            float stickX = 0.0f;
            float stickY = 0.0f;
        };

        struct PresentUpdate
        {
            State state;
            bool pointerChanged = false;
            bool scrollChanged = false;
        };

        void Reset();
        void ResetPresentTracking();
        void OnButtonEvent(
            bool leftHand,
            bool triggerButton,
            bool gripButton,
            bool pressed);
        void SetFingerTouchTrigger(bool leftHand, bool pressed);
        void SetFingerTouchScroll(bool scrolling);
        void SetThumbstick(float x, float y);
        void SetPointer(float u, float v, bool onPanel);
        void SetPointerOffPanel();
        [[nodiscard]] PresentUpdate CapturePresentUpdate(
            int logicalWidth,
            int logicalHeight);

        [[nodiscard]] bool IsPointerOnPanel() const;
        [[nodiscard]] bool WasLastTriggerLeft() const;
        [[nodiscard]] bool IsTriggerDown() const;
        [[nodiscard]] bool IsScrollActive() const;
        [[nodiscard]] bool DidTriggerReleaseSinceLastCheck();

        void SetPreviewInteractionZoneHovered(bool hovered);
        [[nodiscard]] bool IsPreviewInteractionZoneHovered() const;

        void SetHaptic(std::uint8_t cue);
        void QueueHaptic(std::uint8_t cue);
        [[nodiscard]] std::uint8_t ConsumeHaptic();

    private:
        std::atomic<float> _pointerU{ 0.0f };
        std::atomic<float> _pointerV{ 0.0f };
        std::atomic<bool> _pointerOnPanel{ false };
        std::atomic<bool> _triggerDown{ false };
        std::atomic<bool> _leftTriggerDown{ false };
        std::atomic<bool> _rightTriggerDown{ false };
        std::atomic<bool> _fingerTouchTriggerDown{ false };
        std::atomic<bool> _fingerTouchScrollDown{ false };
        std::atomic<bool> _lastTriggerWasLeft{ false };
        std::atomic<bool> _gripDown{ false };
        std::atomic<float> _stickX{ 0.0f };
        std::atomic<float> _stickY{ 0.0f };
        std::atomic<bool> _previewInteractionZoneHovered{ false };
        std::atomic<std::uint8_t> _pendingHapticCue{ 0 };

        bool _presentStateInitialized = false;
        State _lastPresentState;
        bool _previousTriggerDown = false;
    };
}
