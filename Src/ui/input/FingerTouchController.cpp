#include "pch.h"
#include "FingerTouchController.h"

#include "integrations/vrik/VrikFingerPose.h"
#include "ui/pointer/PointerVisualController.h"
#include "ui/rml/RmlPanelHost.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace dragonboard::ui::input
{
    namespace
    {
        struct FingerTipPose
        {
            RE::NiPoint3 position{};
            bool leftHand = false;
        };

        struct WidgetPointSample
        {
            std::shared_ptr<vrui::VRUIWidget> widget;
            float signedDistance = 0.0f;
            float absoluteDistance = std::numeric_limits<float>::max();
        };

        RE::NiAVObject* FindFirst(
            RE::NiNode* root,
            const std::array<const char*, 3>& names)
        {
            if (!root) return nullptr;
            for (const char* name : names) {
                if (auto* object = root->GetObjectByName(name)) {
                    return object;
                }
            }
            return nullptr;
        }

        bool ResolveDominantFingerTip(
            RE::NiNode* root,
            bool useLeftHandAsMenu,
            float extension,
            const RE::NiPoint3& localOffset,
            FingerTipPose& result)
        {
            if (!root) return false;
            result.leftHand = !useLeftHandAsMenu;

            const std::array<const char*, 3> previousNames = result.leftHand ?
                std::array{
                    "NPC L Finger11 [LF11]",
                    "NPC L Finger10 [LF10]",
                    "NPC L Finger12 [LF12]" } :
                std::array{
                    "NPC R Finger11 [RF11]",
                    "NPC R Finger10 [RF10]",
                    "NPC R Finger12 [RF12]" };
            const std::array<const char*, 3> tipNames = result.leftHand ?
                std::array{
                    "NPC L Finger12 [LF12]",
                    "NPC L Finger11 [LF11]",
                    "NPC L Finger10 [LF10]" } :
                std::array{
                    "NPC R Finger12 [RF12]",
                    "NPC R Finger11 [RF11]",
                    "NPC R Finger10 [RF10]" };

            auto* previous = FindFirst(root, previousNames);
            auto* tip = FindFirst(root, tipNames);
            if (!tip) return false;

            result.position = tip->world.translate;
            if (previous) {
                RE::NiPoint3 direction = tip->world.translate - previous->world.translate;
                const float length = direction.Length();
                if (length > 1.0e-4f) {
                    result.position = result.position + direction / length * extension;
                }
            }
            result.position = result.position + tip->world.rotate * localOffset;
            return true;
        }

        bool SampleWidgetPoint(
            const std::shared_ptr<vrui::VRUIWidget>& widget,
            const RE::NiPoint3& worldPoint,
            float boundsPadding,
            float maximumWorldHalfExtent,
            float& signedDistance)
        {
            if (!widget || !widget->isVisible() || !widget->getNode()) return false;
            const auto& transform = widget->getNode()->world;
            const float scale = std::abs(transform.scale);
            if (scale <= 1.0e-5f) return false;

            const RE::NiPoint3 local =
                transform.rotate.Transpose() *
                (worldPoint - transform.translate) / scale;
            const float halfWidth =
                std::min(widget->getWidth() * scale * 0.5f, maximumWorldHalfExtent) /
                scale + boundsPadding / scale;
            const float halfHeight =
                std::min(widget->getHeight() * scale * 0.5f, maximumWorldHalfExtent) /
                scale + boundsPadding / scale;
            if (std::abs(local.x) > halfWidth || std::abs(local.z) > halfHeight) {
                return false;
            }

            signedDistance = local.y * scale;
            return true;
        }

        void FindClosestPanelSurface(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const RE::NiPoint3& point,
            float padding,
            float& closestDistance)
        {
            closestDistance = std::numeric_limits<float>::max();
            for (const auto& panel : panels) {
                if (!panel || !panel->isActive() || !panel->isShown()) continue;
                const auto& name = panel->getName();
                if (name != "Background_Panel" &&
                    name != "Persistent_Panel" &&
                    name != "MainPanel") {
                    continue;
                }
                float signedDistance = 0.0f;
                if (SampleWidgetPoint(
                        panel,
                        point,
                        padding,
                        std::numeric_limits<float>::max(),
                        signedDistance)) {
                    closestDistance = std::min(closestDistance, std::abs(signedDistance));
                }
            }
        }

        void FindClosestButtonRecursive(
            const std::shared_ptr<vrui::VRUIWidget>& widget,
            const RE::NiPoint3& point,
            float hoverDistance,
            WidgetPointSample& result)
        {
            if (!widget || !widget->isVisible()) return;

            if (auto* button = dynamic_cast<vrui::VRUIButton*>(widget.get());
                button && !button->isDashboardPinned()) {
                float signedDistance = 0.0f;
                if (SampleWidgetPoint(
                        widget,
                        point,
                        0.0f,
                        std::numeric_limits<float>::max(),
                        signedDistance)) {
                    const float absoluteDistance = std::abs(signedDistance);
                    if (absoluteDistance <= hoverDistance &&
                        absoluteDistance < result.absoluteDistance) {
                        result = { widget, signedDistance, absoluteDistance };
                    }
                }
            }

            for (const auto& child : widget->getChildren()) {
                FindClosestButtonRecursive(child, point, hoverDistance, result);
            }
        }

        WidgetPointSample FindClosestButton(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const RE::NiPoint3& point,
            float hoverDistance)
        {
            WidgetPointSample result;
            for (const auto& panel : panels) {
                if (panel && panel->isActive() && panel->isShown()) {
                    FindClosestButtonRecursive(panel, point, hoverDistance, result);
                }
            }
            return result;
        }

        bool HasHiggsProximityForHandRecursive(
            const std::shared_ptr<vrui::VRUIWidget>& widget,
            bool leftHand)
        {
            if (!widget || !widget->isVisible()) return false;
            if (auto* button = dynamic_cast<vrui::VRUIButton*>(widget.get());
                button && button->isDashboardPinned() &&
                button->isInHiggsProximityForHand(leftHand)) {
                return true;
            }
            for (const auto& child : widget->getChildren()) {
                if (HasHiggsProximityForHandRecursive(child, leftHand)) return true;
            }
            return false;
        }

        bool HasHiggsProximityForHand(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            bool leftHand)
        {
            for (const auto& panel : panels) {
                if (panel && panel->isActive() && panel->isShown() &&
                    HasHiggsProximityForHandRecursive(panel, leftHand)) {
                    return true;
                }
            }
            return false;
        }
    }

    FingerTouchController& FingerTouchController::GetSingleton()
    {
        static FingerTouchController instance;
        return instance;
    }

    bool FingerTouchController::Update(vrui::VRMenuManager& manager, float deltaTime)
    {
        const auto& settings = vrui::VRUISettings::get();
        auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
        if (!settings.enableFingerTouch || !manager._menuSession.IsOpen()) {
            rmlHost.SetFingerTouchInput(
                false, false, 0.0f, 0.0f, false, false, false);
            Deactivate(manager);
            return false;
        }

        FingerTipPose finger;
        if (!ResolveDominantFingerTip(
                manager.getPlayerSkeletonRoot(),
                settings.useLeftHandAsMenu,
                settings.fingerTouchTipExtension,
                RE::NiPoint3{
                    settings.fingerTouchOffsetX,
                    settings.fingerTouchOffsetY,
                    settings.fingerTouchOffsetZ },
                finger)) {
            rmlHost.SetFingerTouchInput(
                false, false, 0.0f, 0.0f, false, false, false);
            Deactivate(manager);
            return false;
        }

        float closestPanelDistance = std::numeric_limits<float>::max();
        FindClosestPanelSurface(
            manager._panelRegistry.GetPanels(), finger.position, 4.0f, closestPanelDistance);

        float rmlU = 0.0f;
        float rmlV = 0.0f;
        float rmlSignedDistance = std::numeric_limits<float>::max();
        const bool rmlInBounds = rmlHost.IsOpen() &&
            rmlHost.SampleFingerTouchSurface(
                finger.position, rmlU, rmlV, rmlSignedDistance);
        if (rmlInBounds) {
            closestPanelDistance = std::min(
                closestPanelDistance, std::abs(rmlSignedDistance));
        }

        const float modeLimit = _active ?
            settings.fingerTouchExitDistance : settings.fingerTouchEnterDistance;
        const bool shouldBeActive = closestPanelDistance <= modeLimit;
        if (!shouldBeActive) {
            rmlHost.SetFingerTouchInput(
                false, false, 0.0f, 0.0f, false, false, finger.leftHand);
            Deactivate(manager);
            return false;
        }

        if (!_active) {
            _active = true;
            _contactLatched = false;
            manager.triggerHaptic(
                true,
                settings.hapticIntensity * 0.45f,
                settings.hapticDuration * 0.65f);
            logger::info("DragonBoardVR: finger touch mode active; laser suspended.");
        }

        const bool higgsProximity = HasHiggsProximityForHand(
            manager._panelRegistry.GetPanels(), finger.leftHand);
        if (higgsProximity) {
            dragonboard::integrations::vrik::RestoreTouchHandPose();

            // HIGGS owns the hand while a pinned item is in proximity. End
            // every in-flight touch state, including the virtual grip used by
            // RmlUi scrolling, so the old surface cannot keep following the
            // hand after control changes systems.
            ReleasePressedWidget();
            _contactLatched = false;
            _awaitingWithdrawal = false;
            _rmlTouchScrolling = false;
            _rmlTapPulseDown = false;
            rmlHost.SetFingerTouchInput(
                true, false, 0.0f, 0.0f, false, false, finger.leftHand);
            const auto transition = manager._interactionFocus.UpdateHover(
                nullptr, deltaTime, 0.0f);
            if (transition.changed && transition.previous) {
                transition.previous->onRayExit();
            }
            dragonboard::ui::pointer::PointerVisualController::Hide(manager);
            return true;
        } else {
            dragonboard::integrations::vrik::ApplyTouchPointingPose(finger.leftHand);
        }
        dragonboard::ui::pointer::PointerVisualController::Hide(manager);

        WidgetPointSample button = FindClosestButton(
            manager._panelRegistry.GetPanels(),
            finger.position,
            settings.fingerTouchHoverDistance);

        const auto processClassicButton = [&](WidgetPointSample candidate) {
            if (_contactLatched) {
                candidate.widget.reset();
            }

            const auto hoverTransition = manager._interactionFocus.UpdateHover(
                candidate.widget, deltaTime, 0.04f);
            if (hoverTransition.changed) {
                if (hoverTransition.previous) {
                    hoverTransition.previous->onRayExit();
                }
                if (hoverTransition.current) {
                    hoverTransition.current->onRayEnter();
                    manager.triggerHaptic(
                        true,
                        settings.hapticIntensity * 0.55f,
                        settings.hapticDuration * 0.70f);
                }
            }

            if (!_contactLatched && candidate.widget &&
                candidate.absoluteDistance <= settings.fingerTouchPressDistance) {
                _frontSign = candidate.signedDistance < 0.0f ? -1.0f : 1.0f;
                _contactLatched = true;
                _pressedWidget = candidate.widget;
                _pressedLeftHand = finger.leftHand;
                const auto hand = finger.leftHand ?
                    vrui::EquipHand::kLeft : vrui::EquipHand::kRight;
                candidate.widget->onTriggerPress(hand);
                manager.triggerHaptic(
                    true,
                    settings.hapticIntensity,
                    settings.hapticDuration);
                candidate.widget->onRayExit();
                manager._interactionFocus.ClearHover();
            } else if (_contactLatched) {
                float signedDistance = 0.0f;
                auto pressed = _pressedWidget.lock();
                float maximumHalfExtent = std::numeric_limits<float>::max();
                if (auto* pressedButton = pressed ?
                        dynamic_cast<vrui::VRUIButton*>(pressed.get()) : nullptr;
                    pressedButton && pressedButton->isDashboardPinned()) {
                    maximumHalfExtent = 2.0f;
                }
                const bool stillNear = pressed && SampleWidgetPoint(
                    pressed,
                    finger.position,
                    0.0f,
                    maximumHalfExtent,
                    signedDistance);
                if (!stillNear ||
                    signedDistance * _frontSign >= settings.fingerTouchReleaseDistance) {
                    ReleasePressedWidget();
                    _contactLatched = false;
                }
            }
        };

        if (rmlHost.IsOpen()) {
            if (!_pressedWidget.expired()) {
                ReleasePressedWidget();
                _contactLatched = false;
                _awaitingWithdrawal = true;
            }
            if (_awaitingWithdrawal) {
                const bool nearRml = rmlInBounds &&
                    std::abs(rmlSignedDistance) < settings.fingerTouchReleaseDistance;
                const bool nearButton = button.widget &&
                    button.absoluteDistance < settings.fingerTouchReleaseDistance;
                const bool withdrawn = !nearRml && !nearButton;
                if (!withdrawn) {
                    rmlHost.SetFingerTouchInput(
                        true, false, rmlU, rmlV, false, false, finger.leftHand);
                    return true;
                }
                _awaitingWithdrawal = false;
            }

            // A RmlUi touch tap is committed on release. Complete the
            // one-frame down/up pulse before accepting another gesture.
            if (_rmlTapPulseDown) {
                rmlHost.SetFingerTouchInput(
                    true,
                    true,
                    _rmlTapU,
                    _rmlTapV,
                    false,
                    false,
                    finger.leftHand);
                _rmlTapPulseDown = false;
                return true;
            }

            // Persistent DragonBoard buttons surround the RmlUi surface and
            // must remain touchable while a document is open. They take
            // priority only when the fingertip is actually inside their small
            // hover volume; otherwise the RmlUi surface receives the touch.
            if (button.widget) {
                _rmlTouchScrolling = false;
                rmlHost.SetFingerTouchInput(
                    true, false, rmlU, rmlV, false, false, finger.leftHand);
                processClassicButton(std::move(button));
                return true;
            }

            const bool hover = rmlInBounds &&
                std::abs(rmlSignedDistance) <= settings.fingerTouchHoverDistance;
            if (hover && !_contactLatched) {
                _frontSign = rmlSignedDistance < 0.0f ? -1.0f : 1.0f;
            }
            const float orientedDistance = rmlSignedDistance * _frontSign;
            if (!_contactLatched && hover &&
                std::abs(rmlSignedDistance) <= settings.fingerTouchPressDistance) {
                _contactLatched = true;
                _rmlTouchScrolling = false;
                _rmlTouchStartU = rmlU;
                _rmlTouchStartV = rmlV;
            } else if (_contactLatched &&
                       (!rmlInBounds || orientedDistance >= settings.fingerTouchReleaseDistance)) {
                const bool commitTap = !_rmlTouchScrolling && rmlInBounds;
                _contactLatched = false;
                _rmlTouchScrolling = false;
                if (commitTap) {
                    _rmlTapU = rmlU;
                    _rmlTapV = rmlV;
                    _rmlTapPulseDown = true;
                    rmlHost.SetFingerTouchInput(
                        true,
                        true,
                        _rmlTapU,
                        _rmlTapV,
                        true,
                        false,
                        finger.leftHand);
                    return true;
                }
            }

            if (_contactLatched && !_rmlTouchScrolling) {
                constexpr float logicalHeight = 1080.0f;
                const float verticalMovementPixels =
                    std::abs(rmlV - _rmlTouchStartV) * logicalHeight;
                _rmlTouchScrolling =
                    verticalMovementPixels >= settings.fingerTouchScrollDeadzone;
            }

            const bool pointerOnPanel = rmlInBounds && (hover || _contactLatched);
            rmlHost.SetFingerTouchInput(
                true,
                pointerOnPanel,
                rmlU,
                rmlV,
                false,
                _contactLatched && _rmlTouchScrolling,
                finger.leftHand);
            const auto transition = manager._interactionFocus.UpdateHover(nullptr, deltaTime, 0.0f);
            if (transition.changed && transition.previous) {
                transition.previous->onRayExit();
            }
            return true;
        }

        _rmlTouchScrolling = false;
        _rmlTapPulseDown = false;
        rmlHost.SetFingerTouchInput(
            true, false, 0.0f, 0.0f, false, false, finger.leftHand);
        processClassicButton(std::move(button));

        return true;
    }

    void FingerTouchController::Deactivate(vrui::VRMenuManager& manager)
    {
        if (!_active) return;
        ReleasePressedWidget();
        _contactLatched = false;
        _awaitingWithdrawal = false;
        _rmlTouchScrolling = false;
        _rmlTapPulseDown = false;
        const auto transition = manager._interactionFocus.UpdateHover(nullptr, 0.0f, 0.0f);
        if (transition.changed && transition.previous) {
            transition.previous->onRayExit();
        }
        _active = false;
        dragonboard::integrations::vrik::RestoreTouchHandPose();
        logger::info("DragonBoardVR: finger touch mode inactive; laser restored.");
    }

    void FingerTouchController::ReleasePressedWidget()
    {
        if (auto pressed = _pressedWidget.lock()) {
            pressed->onTriggerRelease(
                _pressedLeftHand ? vrui::EquipHand::kLeft : vrui::EquipHand::kRight);
        }
        _pressedWidget.reset();
    }
}
