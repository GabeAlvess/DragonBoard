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
            RE::NiPoint3 physicalPosition{};
            bool leftHand = false;
            bool skeletonLeftHand = false;
        };

        struct WidgetPointSample
        {
            std::shared_ptr<vrui::VRUIWidget> widget;
            float signedDistance = 0.0f;
            float absoluteDistance = std::numeric_limits<float>::max();
            float frontSign = 1.0f;
        };

        constexpr float kBoardHalfWidth = 17.0f;
        constexpr float kBoardHalfHeight = 11.0f;

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
            bool nativeLeftHandedMode,
            float extension,
            const RE::NiPoint3& localOffset,
            FingerTipPose& result)
        {
            if (!root) return false;
            result.leftHand = !useLeftHandAsMenu;
            result.skeletonLeftHand =
                result.leftHand != nativeLeftHandedMode;

            const std::array<const char*, 3> previousNames = result.skeletonLeftHand ?
                std::array{
                    "NPC L Finger11 [LF11]",
                    "NPC L Finger10 [LF10]",
                    "NPC L Finger12 [LF12]" } :
                std::array{
                    "NPC R Finger11 [RF11]",
                    "NPC R Finger10 [RF10]",
                    "NPC R Finger12 [RF12]" };
            const std::array<const char*, 3> tipNames = result.skeletonLeftHand ?
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

            result.physicalPosition =
                tip->world.translate + tip->world.rotate * localOffset;
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
            float& signedDistance,
            const RE::NiPoint3* frontReferencePoint = nullptr,
            float* frontSign = nullptr)
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
            if (frontReferencePoint) {
                const RE::NiPoint3 referenceLocal =
                    transform.rotate.Transpose() *
                    (*frontReferencePoint - transform.translate) / scale;
                // DragonBoard button geometry presents its visible face on the
                // side opposite the HMD in the widget's local depth basis.
                const float resolvedFrontSign =
                    referenceLocal.y < 0.0f ? 1.0f : -1.0f;
                if (frontSign) *frontSign = resolvedFrontSign;
                if (signedDistance * resolvedFrontSign < 0.0f) return false;
            }
            return true;
        }

        bool ResolvePanelFrontDirection(
            const std::shared_ptr<vrui::VRUIPanel>& panel,
            const RE::NiPoint3& frontReferencePoint,
            RE::NiPoint3& frontDirection)
        {
            if (!panel || !panel->isActive() || !panel->isShown() ||
                !panel->getNode()) {
                return false;
            }

            const auto& panelTransform = panel->getNode()->world;
            const float panelScale = std::abs(panelTransform.scale);
            if (panelScale <= 1.0e-5f) {
                return false;
            }

            const RE::NiPoint3 referenceLocal =
                panelTransform.rotate.Transpose() *
                (frontReferencePoint - panelTransform.translate) /
                panelScale;
            const float panelFrontSign =
                referenceLocal.y < 0.0f ? 1.0f : -1.0f;
            frontDirection = {
                panelTransform.rotate.entry[0][1] * panelFrontSign,
                panelTransform.rotate.entry[1][1] * panelFrontSign,
                panelTransform.rotate.entry[2][1] * panelFrontSign };
            return true;
        }

        bool ResolveBoardFrontDirection(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const RE::NiPoint3& frontReferencePoint,
            RE::NiPoint3& frontDirection)
        {
            constexpr std::array<std::string_view, 3> preferredPanels{
                "Background_Panel", "Persistent_Panel", "MainPanel" };
            for (const auto preferredName : preferredPanels) {
                for (const auto& panel : panels) {
                    if (panel && panel->getName() == preferredName &&
                        ResolvePanelFrontDirection(
                            panel, frontReferencePoint, frontDirection)) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool SampleBoardDistance(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const RE::NiPoint3& worldPoint,
            float& distance)
        {
            constexpr std::array<std::string_view, 3> preferredPanels{
                "Background_Panel", "Persistent_Panel", "MainPanel" };
            for (const auto preferredName : preferredPanels) {
                for (const auto& panel : panels) {
                    if (!panel || panel->getName() != preferredName ||
                        !panel->isActive() || !panel->isShown() ||
                        !panel->getNode()) {
                        continue;
                    }

                    const auto& transform = panel->getNode()->world;
                    const float scale = std::abs(transform.scale);
                    if (scale <= 1.0e-5f) {
                        continue;
                    }

                    const RE::NiPoint3 local =
                        transform.rotate.Transpose() *
                        (worldPoint - transform.translate) / scale;
                    const float outsideX =
                        std::max(std::abs(local.x) - kBoardHalfWidth, 0.0f) *
                        scale;
                    const float outsideZ =
                        std::max(std::abs(local.z) - kBoardHalfHeight, 0.0f) *
                        scale;
                    const float depth = std::abs(local.y) * scale;
                    distance = std::sqrt(
                        outsideX * outsideX +
                        outsideZ * outsideZ +
                        depth * depth);
                    return true;
                }
            }
            return false;
        }

        void FindClosestButtonRecursive(
            const std::shared_ptr<vrui::VRUIWidget>& widget,
            const RE::NiPoint3& point,
            const RE::NiPoint3& panelFrontDirection,
            float hoverDistance,
            WidgetPointSample& result)
        {
            if (!widget || !widget->isVisible()) return;

            if (auto* button = dynamic_cast<vrui::VRUIButton*>(widget.get());
                button && !button->isDashboardPinned()) {
                float signedDistance = 0.0f;
                float frontSign = 1.0f;
                if (SampleWidgetPoint(
                        widget,
                        point,
                        0.0f,
                        std::numeric_limits<float>::max(),
                        signedDistance)) {
                    const auto& widgetRotation = widget->getNode()->world.rotate;
                    const RE::NiPoint3 widgetDepthAxis(
                        widgetRotation.entry[0][1],
                        widgetRotation.entry[1][1],
                        widgetRotation.entry[2][1]);
                    const float alignment =
                        widgetDepthAxis.x * panelFrontDirection.x +
                        widgetDepthAxis.y * panelFrontDirection.y +
                        widgetDepthAxis.z * panelFrontDirection.z;
                    frontSign = alignment < 0.0f ? -1.0f : 1.0f;
                    const float absoluteDistance = std::abs(signedDistance);
                    if (signedDistance * frontSign >= 0.0f &&
                        absoluteDistance <= hoverDistance &&
                        absoluteDistance < result.absoluteDistance) {
                        result = {
                            widget, signedDistance, absoluteDistance, frontSign };
                    }
                }
            }

            for (const auto& child : widget->getChildren()) {
                FindClosestButtonRecursive(
                    child, point, panelFrontDirection, hoverDistance, result);
            }
        }

        WidgetPointSample FindClosestButton(
            const std::vector<std::shared_ptr<vrui::VRUIPanel>>& panels,
            const RE::NiPoint3& point,
            const RE::NiPoint3& frontReferencePoint,
            float hoverDistance)
        {
            WidgetPointSample result;
            for (const auto& panel : panels) {
                if (panel && panel->isActive() && panel->isShown()) {
                    RE::NiPoint3 panelFrontDirection;
                    if (!ResolvePanelFrontDirection(
                            panel, frontReferencePoint, panelFrontDirection)) {
                        continue;
                    }
                    FindClosestButtonRecursive(
                        panel, point, panelFrontDirection, hoverDistance, result);
                }
            }
            return result;
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
                settings.isNativeLeftHandedMode(),
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
        auto* headNode = manager.getHeadNode();
        if (!headNode) {
            rmlHost.SetFingerTouchInput(
                false, false, 0.0f, 0.0f, false, false, finger.leftHand);
            Deactivate(manager);
            return false;
        }
        const RE::NiPoint3 frontReferencePoint = headNode->world.translate;
        const auto& panels = manager._panelRegistry.GetPanels();

        float boardDistance = std::numeric_limits<float>::max();
        const bool hasBoardSurface =
            SampleBoardDistance(panels, finger.physicalPosition, boardDistance);
        const bool boardWithinEntry =
            hasBoardSurface &&
            boardDistance <= settings.fingerTouchEnterDistance;
        if (!_active && !boardWithinEntry) {
            rmlHost.SetFingerTouchInput(
                false, false, 0.0f, 0.0f, false, false, finger.leftHand);
            Deactivate(manager);
            return false;
        }

        if (!_active) {
            _active = true;
            _contactLatched = false;
            _contactIsRml = false;
            _rmlFrontApproachArmed = false;
            _frontApproachArmedWidget.reset();
            logger::info(
                "DragonBoardVR: finger touch mode active within {:.1f} units "
                "of the board with physicalHand={}, skeletonHand={}; "
                "laser suspended.",
                settings.fingerTouchEnterDistance,
                finger.leftHand ? "left" : "right",
                finger.skeletonLeftHand ? "left" : "right");
        }

        float rmlU = 0.0f;
        float rmlV = 0.0f;
        float rmlSignedDistance = std::numeric_limits<float>::max();
        float rmlFrontSign = 1.0f;
        RE::NiPoint3 boardFrontDirection;
        const bool hasBoardFrontDirection = ResolveBoardFrontDirection(
            panels,
            frontReferencePoint,
            boardFrontDirection);
        const bool rmlInBounds = rmlHost.IsOpen() &&
            hasBoardFrontDirection &&
            rmlHost.SampleFingerTouchSurface(
                finger.position,
                boardFrontDirection,
                rmlU,
                rmlV,
                rmlSignedDistance,
                rmlFrontSign);
        const bool rmlOnFront =
            rmlInBounds && rmlSignedDistance * rmlFrontSign >= 0.0f;
        WidgetPointSample proximityButton = FindClosestButton(
            panels,
            finger.position,
            frontReferencePoint,
            settings.fingerTouchHoverDistance);

        WidgetPointSample button;
        if (proximityButton.widget) {
            button = proximityButton;
        }

        static float touchDiagnosticCooldown = 0.0f;
        touchDiagnosticCooldown -= (std::max)(deltaTime, 0.0f);
        if (_active && touchDiagnosticCooldown <= 0.0f) {
            touchDiagnosticCooldown = 1.0f;
            logger::info(
                "DragonBoardVR: finger touch sample physicalHand={}, "
                "skeletonHand={}, boardDistance={:.2f}, "
                "rmlInBounds={}, rmlOnFront={}, u={:.3f}, v={:.3f}, "
                "signedDistance={:.2f}, frontSign={:.0f}, button={}.",
                finger.leftHand ? "left" : "right",
                finger.skeletonLeftHand ? "left" : "right",
                boardDistance,
                rmlInBounds,
                rmlOnFront,
                rmlU,
                rmlV,
                rmlSignedDistance,
                rmlFrontSign,
                button.widget != nullptr);
        }

        const bool boardWithinExit =
            hasBoardSurface &&
            boardDistance <= settings.fingerTouchExitDistance;
        const bool rmlWithinExit =
            rmlInBounds &&
            std::abs(rmlSignedDistance) <= settings.fingerTouchExitDistance;
        const bool buttonWithinExit =
            proximityButton.widget &&
            proximityButton.absoluteDistance <= settings.fingerTouchExitDistance;
        const bool gestureOwnsTouch =
            _contactLatched || _rmlTapPulseDown || _awaitingWithdrawal;
        if (!boardWithinExit && !rmlWithinExit && !buttonWithinExit &&
            !gestureOwnsTouch) {
            rmlHost.SetFingerTouchInput(
                false, false, 0.0f, 0.0f, false, false, finger.leftHand);
            Deactivate(manager);
            return false;
        }

        dragonboard::integrations::vrik::ApplyTouchPointingPose(
            finger.skeletonLeftHand);
        dragonboard::ui::pointer::PointerVisualController::Hide(manager);

        const auto processClassicButton = [&](WidgetPointSample candidate) {
            if (_contactLatched) {
                candidate.widget.reset();
            } else if (!candidate.widget) {
                _frontApproachArmedWidget.reset();
            } else {
                const float orientedDistance =
                    candidate.signedDistance * candidate.frontSign;
                if (orientedDistance > settings.fingerTouchPressDistance) {
                    _frontApproachArmedWidget = candidate.widget;
                }
            }

            const auto hoverTransition = manager._interactionFocus.UpdateHover(
                candidate.widget, deltaTime, 0.04f);
            if (hoverTransition.changed) {
                if (hoverTransition.previous) {
                    hoverTransition.previous->onRayExit();
                }
                if (hoverTransition.current) {
                    hoverTransition.current->onRayEnter();
                }
            }

            const auto armedWidget = _frontApproachArmedWidget.lock();
            if (!_contactLatched && candidate.widget &&
                armedWidget == candidate.widget &&
                candidate.absoluteDistance <= settings.fingerTouchPressDistance) {
                _frontSign = candidate.frontSign;
                _contactLatched = true;
                _contactIsRml = false;
                _frontApproachArmedWidget.reset();
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
                    _contactIsRml = false;
                }
            }
        };

        if (rmlHost.IsOpen()) {
            if (!_pressedWidget.expired()) {
                ReleasePressedWidget();
                _contactLatched = false;
                _contactIsRml = false;
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
                _rmlFrontApproachArmed = false;
                _rmlTouchScrolling = false;
                rmlHost.SetFingerTouchInput(
                    true, false, rmlU, rmlV, false, false, finger.leftHand);
                processClassicButton(std::move(button));
                return true;
            }
            _frontApproachArmedWidget.reset();

            const bool hover = rmlOnFront &&
                rmlSignedDistance * rmlFrontSign <=
                    settings.fingerTouchHoverDistance;
            if (hover && !_contactLatched) _frontSign = rmlFrontSign;
            const float orientedDistance = rmlSignedDistance * _frontSign;
            if (!_contactLatched && !rmlOnFront) {
                _rmlFrontApproachArmed = false;
            } else if (!_contactLatched && hover &&
                       orientedDistance > settings.fingerTouchPressDistance) {
                _rmlFrontApproachArmed = true;
            }
            if (!_contactLatched && _rmlFrontApproachArmed && hover &&
                orientedDistance <= settings.fingerTouchPressDistance) {
                _contactLatched = true;
                _contactIsRml = true;
                _rmlFrontApproachArmed = false;
                _rmlTouchScrolling = false;
                _rmlTouchStartU = rmlU;
                _rmlTouchStartV = rmlV;
            } else if (_contactLatched &&
                       (!rmlInBounds || orientedDistance >= settings.fingerTouchReleaseDistance)) {
                const bool commitTap = !_rmlTouchScrolling && rmlInBounds;
                _contactLatched = false;
                _contactIsRml = false;
                _rmlFrontApproachArmed = false;
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
        _contactIsRml = false;
        _awaitingWithdrawal = false;
        _rmlTouchScrolling = false;
        _rmlTapPulseDown = false;
        _rmlFrontApproachArmed = false;
        _frontApproachArmedWidget.reset();
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
