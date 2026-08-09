#include "ui/rml/RmlPanelHost.h"

#include "ui/menu/MenuComposition.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

#include <algorithm>
#include <cmath>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr float kPanelFaceCorrectionDegrees = 180.0f;
        constexpr float kRadiansToDegrees = 57.29577951308232f;
        constexpr float kBoardMinimumScale = 0.25f;
        constexpr float kBoardMaximumScale = 3.0f;

        float NormalizeDegrees(float degrees)
        {
            degrees = std::fmod(degrees + 180.0f, 360.0f);
            if (degrees < 0.0f) degrees += 360.0f;
            return degrees - 180.0f;
        }

        void ExtractEulerForSetXYZ(
            const RE::NiMatrix3& matrix,
            float& rotationX,
            float& rotationY,
            float& rotationZ)
        {
            const float m02 = std::clamp(matrix.entry[0][2], -1.0f, 1.0f);
            rotationY = -std::asin(m02);
            const float cosY = std::cos(rotationY);
            if (std::abs(cosY) > 1.0e-5f) {
                rotationX = std::atan2(
                    matrix.entry[1][2], matrix.entry[2][2]);
                rotationZ = std::atan2(
                    matrix.entry[0][1], matrix.entry[0][0]);
            } else {
                rotationX = std::atan2(
                    -matrix.entry[2][1], matrix.entry[1][1]);
                rotationZ = 0.0f;
            }
        }

        float RotationMaximumError(
            const RE::NiMatrix3& expected,
            const RE::NiMatrix3& actual)
        {
            float error = 0.0f;
            for (int row = 0; row < 3; ++row) {
                for (int column = 0; column < 3; ++column) {
                    error = std::max(
                        error,
                        std::abs(
                            expected.entry[row][column] -
                            actual.entry[row][column]));
                }
            }
            return error;
        }

        RE::NiTransform ComposeWorldTransform(
            const RE::NiTransform& parentWorld,
            const RE::NiTransform& local)
        {
            RE::NiTransform world;
            world.translate = parentWorld.translate +
                (parentWorld.rotate * local.translate) * parentWorld.scale;
            world.rotate = parentWorld.rotate * local.rotate;
            world.scale = parentWorld.scale * local.scale;
            return world;
        }
    }

    void RmlPanelHost::CaptureSettingsGameThread()
    {
        auto& settings = vrui::VRUISettings::get();
        std::scoped_lock lock(_draftMutex);
        _draft.lockPins = settings.lockPins;
        _draft.showDevButton = settings.showDevButton;
        _draft.showTutorials = settings.showTutorials;
        _draft.statusWidgetVisible = settings.statusWidgetVisible;
        _draft.uiLanguage = settings.uiLanguage;
        _draft.worldPinned = vrui::VRMenuManager::get().isBoardWorldPinned();
        _draft.menuScale = settings.menuScale;
        _draft.buttonSpacingX = settings.buttonSpacingX;
        _draft.buttonSpacingY = settings.buttonSpacingY;
        _draft.menuOffsetX = settings.menuOffsetX;
        _draft.menuOffsetY = settings.menuOffsetY;
        _draft.menuOffsetZ = settings.menuOffsetZ;
        _draft.menuRotX = settings.menuRotX;
        _draft.menuRotY = settings.menuRotY;
        _draft.menuRotZ = settings.menuRotZ;
        _draft.buttonMeshScale = settings.buttonMeshScale;
        _draft.itemMeshScale = settings.itemMeshScale;
        _draft.containerGridOffsetZ = settings.containerGridOffsetZ;
        _draft.reticleScale = settings.reticleScaleX;
        _draft.itemWeaponScale = settings.itemWeaponScale;
        _draft.itemArmorScale = settings.itemArmorScale;
        _draft.itemPotionScale = settings.itemPotionScale;
        _draft.itemFoodScale = settings.itemFoodScale;
        _draft.itemMiscScale = settings.itemMiscScale;
        _draft.labelScale = settings.labelScale;
        _draft.labelSpacing = settings.labelSpacing;
        _draft.labelXOffset = settings.labelXOffset;
        _draft.labelYOffset = settings.labelYOffset;
        _draft.labelZOffset = settings.labelZOffset;
    }

    void RmlPanelHost::ApplyDraftGameThread(bool applyLanguage)
    {
        auto& settings = vrui::VRUISettings::get();
        std::scoped_lock lock(_draftMutex);
        const bool tutorialsReenabled =
            !settings.showTutorials && _draft.showTutorials;
        settings.lockPins = _draft.lockPins;
        settings.showDevButton = _draft.showDevButton;
        settings.showTutorials = _draft.showTutorials;
        settings.statusWidgetVisible = _draft.statusWidgetVisible;
        if (applyLanguage) settings.uiLanguage = _draft.uiLanguage;
        if (tutorialsReenabled) {
            settings.resetTutorialProgress();
            settings.tutorialsPreviouslyEnabled = true;
            settings.tutorialPositionResetRequested = true;
        }
        dragonboard::ui::menu::SetDeveloperButtonVisible(settings.showDevButton);
        settings.menuScale = _draft.menuScale;
        settings.buttonSpacingX = _draft.buttonSpacingX;
        settings.buttonSpacingY = _draft.buttonSpacingY;
        settings.menuOffsetX = _draft.menuOffsetX;
        settings.menuOffsetY = _draft.menuOffsetY;
        settings.menuOffsetZ = _draft.menuOffsetZ;
        settings.menuRotX = _draft.menuRotX;
        settings.menuRotY = _draft.menuRotY;
        settings.menuRotZ = _draft.menuRotZ;
        settings.buttonMeshScale = _draft.buttonMeshScale;
        settings.itemMeshScale = _draft.itemMeshScale;
        settings.containerGridOffsetZ = _draft.containerGridOffsetZ;
        settings.reticleScaleX = settings.reticleScaleY = settings.reticleScaleZ = _draft.reticleScale;
        settings.itemWeaponScale = _draft.itemWeaponScale;
        settings.itemArmorScale = _draft.itemArmorScale;
        settings.itemPotionScale = _draft.itemPotionScale;
        settings.itemFoodScale = _draft.itemFoodScale;
        settings.itemMiscScale = _draft.itemMiscScale;
        settings.labelScale = _draft.labelScale;
        settings.labelSpacing = _draft.labelSpacing;
        settings.labelXOffset = _draft.labelXOffset;
        settings.labelYOffset = _draft.labelYOffset;
        settings.labelZOffset = _draft.labelZOffset;
    }

    void RmlPanelHost::BeginPositionAdjustmentGameThread()
    {
        auto& manager = vrui::VRMenuManager::get();
        const auto backgroundPanel = manager.findPanelByName("Background_Panel");
        if (!backgroundPanel || !backgroundPanel->getNode()) {
            logger::warn(
                "DragonBoardVR: Position adjustment could not start because "
                "the board node is unavailable.");
            return;
        }
        const RE::NiTransform visibleWorldAtEntry =
            backgroundPanel->getNode()->world;

        {
            std::scoped_lock lock(_draftMutex);
            _positionAdjustmentStartDraft = _draft;
        }
        _positionAdjustmentStartedWorldPinned = manager.isBoardWorldPinned();
        if (_positionAdjustmentStartedWorldPinned) {
            manager.setBoardWorldPinned(false);
        }
        {
            const auto& settings = vrui::VRUISettings::get();
            std::scoped_lock lock(_draftMutex);
            _draft.worldPinned = false;
            _draft.menuOffsetX = settings.menuOffsetX;
            _draft.menuOffsetY = settings.menuOffsetY;
            _draft.menuOffsetZ = settings.menuOffsetZ;
            _draft.menuRotX = settings.menuRotX;
            _draft.menuRotY = settings.menuRotY;
            _draft.menuRotZ = settings.menuRotZ;
            _draft.menuScale = settings.menuScale;
        }

        _boardGrabController.Reset();
        _boardGripThumbScaleCaptured.store(false, std::memory_order_release);
        _boardGrabController.SetEnabled(true);
        manager.setPositionAdjustmentActive(true);
        manager.setPositionAdjustmentWorldTransform(visibleWorldAtEntry);
        _positionAdjustmentActive.store(true, std::memory_order_release);
        _rmlSettingsSyncPending.store(true, std::memory_order_release);
        logger::info(
            "DragonBoardVR: Position adjustment started without visual handoff "
            "at world position ({:.3f}, {:.3f}, {:.3f}).",
            visibleWorldAtEntry.translate.x,
            visibleWorldAtEntry.translate.y,
            visibleWorldAtEntry.translate.z);
    }

    void RmlPanelHost::CancelPositionAdjustmentGameThread()
    {
        if (!_positionAdjustmentActive.exchange(
                false, std::memory_order_acq_rel)) {
            return;
        }

        auto& manager = vrui::VRMenuManager::get();
        _boardGrabController.SetEnabled(false);
        _boardGripThumbScaleCaptured.store(false, std::memory_order_release);
        manager.setPositionAdjustmentActive(false);
        {
            std::scoped_lock lock(_draftMutex);
            _draft = _positionAdjustmentStartDraft;
        }
        ApplyDraftGameThread();
        if (manager.isBoardWorldPinned() != _positionAdjustmentStartedWorldPinned) {
            manager.setBoardWorldPinned(_positionAdjustmentStartedWorldPinned);
        }
        manager.refreshActivePanels();
        _rmlSettingsSyncPending.store(true, std::memory_order_release);
        logger::info(
            "DragonBoardVR: Position adjustment cancelled; restored the "
            "pre-adjustment hand-relative transform.");
    }

    void RmlPanelHost::FinishPositionAdjustmentGameThread()
    {
        if (!_positionAdjustmentActive.exchange(
                false, std::memory_order_acq_rel)) {
            return;
        }

        _boardGrabController.SetEnabled(false);
        _boardGripThumbScaleCaptured.store(false, std::memory_order_release);
        vrui::VRMenuManager::get().setPositionAdjustmentActive(false);
        _rmlSettingsSyncPending.store(true, std::memory_order_release);
        logger::info(
            "DragonBoardVR: Position adjustment accepted for persistent save.");
    }

    void RmlPanelHost::UpdatePositionAdjustmentGameThread(float deltaTime)
    {
        if (!_positionAdjustmentActive.load(std::memory_order_acquire)) return;

        auto& manager = vrui::VRMenuManager::get();
        if (manager.isBoardWorldPinned()) {
            manager.setBoardWorldPinned(false);
            std::scoped_lock lock(_draftMutex);
            _draft.worldPinned = false;
            _rmlSettingsSyncPending.store(true, std::memory_order_release);
        }
        const auto backgroundPanel = manager.findPanelByName("Background_Panel");
        auto* boardNode = backgroundPanel ? backgroundPanel->getNode() : nullptr;
        auto* boardAnchor = boardNode && boardNode->parent ?
            boardNode->parent->AsNode() : nullptr;
        if (!boardNode || !boardAnchor) {
            logger::warn(
                "DragonBoardVR: Position adjustment cancelled because the "
                "board node or its hand anchor became unavailable.");
            CancelPositionAdjustmentGameThread();
            return;
        }

        float thumbX = 0.0f;
        float thumbY = 0.0f;
        manager.getDominantThumbstick(thumbX, thumbY);
        const auto result = _boardGrabController.Update(
            boardNode,
            RmlSurfaceGrabController::Input{
                manager.getDominantHandNode(),
                manager.getNonDominantHandNode(),
                manager.isDominantGripButtonDown(),
                manager.isOffhandGripButtonDown(),
                true,
                kBoardMinimumScale,
                kBoardMaximumScale,
                false,
                0.0f,
                false,
                thumbY },
            deltaTime);
        _boardGripThumbScaleCaptured.store(
            _boardGrabController.IsGrabbed(),
            std::memory_order_release);
        if (result.grabStarted) {
            manager.setPositionAdjustmentWorldTransform(boardNode->world);
            manager.triggerHaptic(true, 0.55f, 0.08f);
            logger::info(
                "DragonBoardVR: whole-board grab started from anchor '{}'.",
                boardAnchor->name.c_str());
        }
        if (result.grabEnded) {
            manager.triggerHaptic(true, 0.35f, 0.05f);
            logger::info(
                "DragonBoardVR: whole-board grab released; keeping the "
                "current hand-local transform pending Save.");
        }

        if (result.transformChanged) {
            manager.setPositionAdjustmentWorldTransform(
                ComposeWorldTransform(boardAnchor->world, boardNode->local));
        } else if (!manager.hasPositionAdjustmentWorldTransform()) {
            return;
        }

        const auto& sharedWorld =
            manager.getPositionAdjustmentWorldTransform();
        auto& settings = vrui::VRUISettings::get();
        auto* trackingHand = manager.getMenuHandNode();

        RE::NiPoint3 menuOffset = boardNode->local.translate;
        RE::NiMatrix3 menuRotation = boardNode->local.rotate;
        if (settings.bEnableMenuLerp && trackingHand) {
            // This is the exact inverse of VRUIPanel's normal lerped follow:
            // worldPos = handPos + handRot * menuOffset
            // worldRot = handRot * menuRotation
            const auto inverseTrackingRotation =
                trackingHand->world.rotate.Transpose();
            menuOffset = inverseTrackingRotation *
                (sharedWorld.translate - trackingHand->world.translate);
            menuRotation =
                inverseTrackingRotation * sharedWorld.rotate;
        }

        float rotationX = 0.0f;
        float rotationY = 0.0f;
        float rotationZ = 0.0f;
        ExtractEulerForSetXYZ(
            menuRotation, rotationX, rotationY, rotationZ);

        const float offsetX = menuOffset.x;
        const float offsetY = menuOffset.y;
        const float offsetZ = menuOffset.z;
        const float parentScale =
            std::abs(boardAnchor->world.scale) > 1.0e-5f ?
                boardAnchor->world.scale : 1.0f;
        const float menuScale = std::clamp(
            sharedWorld.scale / parentScale,
            kBoardMinimumScale,
            kBoardMaximumScale);
        const float menuRotX = NormalizeDegrees(rotationX * kRadiansToDegrees);
        const float menuRotY = NormalizeDegrees(rotationY * kRadiansToDegrees);
        const float menuRotZ = NormalizeDegrees(
            rotationZ * kRadiansToDegrees - kPanelFaceCorrectionDegrees);

        if (result.grabEnded) {
            RE::NiMatrix3 reconstructedRotation{};
            reconstructedRotation.SetEulerAnglesXYZ(
                menuRotX / kRadiansToDegrees,
                menuRotY / kRadiansToDegrees,
                (menuRotZ + kPanelFaceCorrectionDegrees) / kRadiansToDegrees);
            logger::info(
                "DragonBoardVR: released board converted to menu transform "
                "offset=({:.3f}, {:.3f}, {:.3f}) "
                "Euler=({:.2f}, {:.2f}, {:.2f}); "
                "tracking='{}' reconstruction max error={:.6f}.",
                offsetX,
                offsetY,
                offsetZ,
                menuRotX,
                menuRotY,
                menuRotZ,
                trackingHand && !trackingHand->name.empty() ?
                    trackingHand->name.c_str() : "<none>",
                RotationMaximumError(
                    menuRotation, reconstructedRotation));
        }

        {
            std::scoped_lock lock(_draftMutex);
            _draft.menuOffsetX = offsetX;
            _draft.menuOffsetY = offsetY;
            _draft.menuOffsetZ = offsetZ;
            _draft.menuRotX = menuRotX;
            _draft.menuRotY = menuRotY;
            _draft.menuRotZ = menuRotZ;
            _draft.menuScale = menuScale;
        }

        settings.menuOffsetX = offsetX;
        settings.menuOffsetY = offsetY;
        settings.menuOffsetZ = offsetZ;
        settings.menuRotX = menuRotX;
        settings.menuRotY = menuRotY;
        settings.menuRotZ = menuRotZ;
        settings.menuScale = menuScale;
    }

}
