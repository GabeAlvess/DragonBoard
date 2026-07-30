#include "pch.h"

#include "ui/rml/RmlInputBridge.h"

#include <cmath>

namespace dragonboard::ui::rml
{
    void RmlInputBridge::Reset()
    {
        _triggerDown.store(false, std::memory_order_release);
        _leftTriggerDown.store(false, std::memory_order_release);
        _rightTriggerDown.store(false, std::memory_order_release);
        _fingerTouchActive.store(false, std::memory_order_release);
        _fingerTouchTriggerDown.store(false, std::memory_order_release);
        _fingerTouchScrollDown.store(false, std::memory_order_release);
        _gripDown.store(false, std::memory_order_release);
        _stickX.store(0.0f, std::memory_order_release);
        _stickY.store(0.0f, std::memory_order_release);
    }

    void RmlInputBridge::SetFingerTouchActive(bool active)
    {
        _fingerTouchActive.store(active, std::memory_order_release);
    }

    void RmlInputBridge::SetFingerTouchTrigger(bool leftHand, bool pressed)
    {
        if (pressed) {
            _lastTriggerWasLeft.store(leftHand, std::memory_order_release);
            logger::info(
                "DragonBoardVR: finger touch click from {} hand.",
                leftHand ? "left" : "right");
        }
        _fingerTouchTriggerDown.store(pressed, std::memory_order_release);
    }

    void RmlInputBridge::SetFingerTouchScroll(bool scrolling)
    {
        _fingerTouchScrollDown.store(scrolling, std::memory_order_release);
    }

    void RmlInputBridge::ResetPresentTracking()
    {
        _presentStateInitialized = false;
    }

    void RmlInputBridge::OnButtonEvent(
        bool leftHand,
        bool triggerButton,
        bool gripButton,
        bool pressed)
    {
        if (triggerButton) {
            _lastTriggerWasLeft.store(leftHand, std::memory_order_release);
            if (leftHand) {
                _leftTriggerDown.store(pressed, std::memory_order_release);
            } else {
                _rightTriggerDown.store(pressed, std::memory_order_release);
            }
            const bool anyTriggerDown =
                _leftTriggerDown.load(std::memory_order_acquire) ||
                _rightTriggerDown.load(std::memory_order_acquire);
            const bool previous = _triggerDown.exchange(
                anyTriggerDown, std::memory_order_acq_rel);
            if (previous != anyTriggerDown) {
                logger::info(
                    "DragonBoardVR: local panel {} trigger {}.",
                    leftHand ? "left" : "right",
                    anyTriggerDown ? "down" : "up");
            }
        } else if (gripButton) {
            const bool previous = _gripDown.exchange(
                pressed, std::memory_order_acq_rel);
            if (previous != pressed) {
                logger::info(
                    "DragonBoardVR: local panel dominant grip {}.",
                    pressed ? "down" : "up");
            }
        }
    }

    void RmlInputBridge::SetThumbstick(float x, float y)
    {
        _stickX.store(x, std::memory_order_release);
        _stickY.store(y, std::memory_order_release);
    }

    void RmlInputBridge::SetPointer(float u, float v, bool onPanel)
    {
        if (onPanel) {
            _pointerU.store(u, std::memory_order_release);
            _pointerV.store(v, std::memory_order_release);
        }
        _pointerOnPanel.store(onPanel, std::memory_order_release);
    }

    void RmlInputBridge::SetPointerOffPanel()
    {
        _pointerOnPanel.store(false, std::memory_order_release);
    }

    RmlInputBridge::PresentUpdate RmlInputBridge::CapturePresentUpdate(
        int logicalWidth,
        int logicalHeight)
    {
        PresentUpdate update;
        update.state.pointerOnPanel = _pointerOnPanel.load(std::memory_order_acquire);
        update.state.pointerU = _pointerU.load(std::memory_order_acquire);
        update.state.pointerV = _pointerV.load(std::memory_order_acquire);
        update.state.fingerTouchActive =
            _fingerTouchActive.load(std::memory_order_acquire);
        update.state.fingerTouchScrolling =
            update.state.fingerTouchActive &&
            _fingerTouchScrollDown.load(std::memory_order_acquire);
        const bool physicalTriggerDown =
            _triggerDown.load(std::memory_order_acquire);
        if (update.state.fingerTouchActive) {
            update.state.triggerDown =
                _fingerTouchTriggerDown.load(std::memory_order_acquire);
            update.state.gripDown =
                update.state.fingerTouchScrolling &&
                !update.state.triggerDown;
            update.state.stickX = 0.0f;
            update.state.stickY = 0.0f;
        } else {
            update.state.triggerDown = physicalTriggerDown;
            update.state.gripDown =
                _gripDown.load(std::memory_order_acquire);
            update.state.stickX = _stickX.load(std::memory_order_acquire);
            update.state.stickY = _stickY.load(std::memory_order_acquire);
        }

        const float pointerThresholdU = logicalWidth > 0 ?
            0.5f / static_cast<float>(logicalWidth) : 0.0f;
        const float pointerThresholdV = logicalHeight > 0 ?
            0.5f / static_cast<float>(logicalHeight) : 0.0f;
        update.pointerChanged = !_presentStateInitialized ||
            update.state.pointerOnPanel != _lastPresentState.pointerOnPanel ||
            std::abs(update.state.pointerU - _lastPresentState.pointerU) >= pointerThresholdU ||
            std::abs(update.state.pointerV - _lastPresentState.pointerV) >= pointerThresholdV ||
            update.state.triggerDown != _lastPresentState.triggerDown ||
            update.state.fingerTouchActive !=
                _lastPresentState.fingerTouchActive;
        update.scrollChanged = !_presentStateInitialized ||
            update.state.gripDown != _lastPresentState.gripDown ||
            update.state.fingerTouchScrolling !=
                _lastPresentState.fingerTouchScrolling ||
            std::abs(update.state.stickX - _lastPresentState.stickX) >= 0.01f ||
            std::abs(update.state.stickY - _lastPresentState.stickY) >= 0.01f;
        _presentStateInitialized = true;
        _lastPresentState = update.state;
        return update;
    }

    bool RmlInputBridge::IsPointerOnPanel() const
    {
        return _pointerOnPanel.load(std::memory_order_acquire);
    }

    bool RmlInputBridge::WasLastTriggerLeft() const
    {
        return _lastTriggerWasLeft.load(std::memory_order_acquire);
    }

    bool RmlInputBridge::IsTriggerDown() const
    {
        if (_fingerTouchActive.load(std::memory_order_acquire)) {
            return _fingerTouchTriggerDown.load(std::memory_order_acquire);
        }
        return _triggerDown.load(std::memory_order_acquire);
    }

    bool RmlInputBridge::IsScrollActive() const
    {
        if (_fingerTouchActive.load(std::memory_order_acquire)) {
            return _fingerTouchScrollDown.load(std::memory_order_acquire) &&
                !_fingerTouchTriggerDown.load(std::memory_order_acquire);
        }
        return _gripDown.load(std::memory_order_acquire) &&
            !_triggerDown.load(std::memory_order_acquire);
    }

    bool RmlInputBridge::DidTriggerReleaseSinceLastCheck()
    {
        const bool triggerDown = IsTriggerDown();
        const bool released = !triggerDown && _previousTriggerDown;
        _previousTriggerDown = triggerDown;
        return released;
    }

    void RmlInputBridge::SetPreviewInteractionZoneHovered(bool hovered)
    {
        _previewInteractionZoneHovered.store(hovered, std::memory_order_release);
    }

    bool RmlInputBridge::IsPreviewInteractionZoneHovered() const
    {
        return _previewInteractionZoneHovered.load(std::memory_order_acquire);
    }

    void RmlInputBridge::SetHaptic(std::uint8_t cue)
    {
        _pendingHapticCue.store(cue, std::memory_order_release);
    }

    void RmlInputBridge::QueueHaptic(std::uint8_t cue)
    {
        auto pending = _pendingHapticCue.load(std::memory_order_relaxed);
        while (cue > pending &&
               !_pendingHapticCue.compare_exchange_weak(
                   pending,
                   cue,
                   std::memory_order_release,
                   std::memory_order_relaxed)) {
        }
    }

    std::uint8_t RmlInputBridge::ConsumeHaptic()
    {
        if (IsScrollActive()) {
            _pendingHapticCue.exchange(0, std::memory_order_acq_rel);
            return 0;
        }
        return _pendingHapticCue.exchange(0, std::memory_order_acq_rel);
    }
}
