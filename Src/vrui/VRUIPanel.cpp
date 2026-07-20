#include "VRUIPanel.h"
#include "VRUIButton.h"
#include "VRUISettings.h"
#include "VRUILayoutManager.h"
#include "VRMenuManager.h"
#include <RE/Skyrim.h>

// Cleaned macros

namespace vrui
{
    namespace
    {
        constexpr float kPanelFaceCorrectionDegrees = 180.0f;

        RE::NiTransform MakeRelativeTransform(
            const RE::NiTransform& parentWorld,
            const RE::NiTransform& childWorld)
        {
            const float parentScale = parentWorld.scale > 0.0001f ?
                parentWorld.scale : 1.0f;
            const RE::NiMatrix3 inverseParentRotation = parentWorld.rotate.Transpose();

            RE::NiTransform relative;
            relative.translate =
                inverseParentRotation * (childWorld.translate - parentWorld.translate) /
                parentScale;
            relative.rotate = inverseParentRotation * childWorld.rotate;
            relative.scale = childWorld.scale / parentScale;
            return relative;
        }

        RE::NiTransform ComposeTransform(
            const RE::NiTransform& parentWorld,
            const RE::NiTransform& relative)
        {
            RE::NiTransform result;
            result.translate = parentWorld.translate +
                parentWorld.rotate * relative.translate * parentWorld.scale;
            result.rotate = parentWorld.rotate * relative.rotate;
            result.scale = parentWorld.scale * relative.scale;
            return result;
        }

        void ApplyWorldTransform(RE::NiNode* node, const RE::NiTransform& worldTransform)
        {
            if (!node || !node->parent) return;

            const auto local = MakeRelativeTransform(node->parent->world, worldTransform);
            node->local = local;

            RE::NiUpdateData updateData;
            updateData.flags = RE::NiUpdateData::Flag::kDirty;
            node->Update(updateData);
        }
    }

    VRUIPanel::VRUIPanel(const std::string& name, float scale, bool drawsBackground)
        : VRUIContainer(name, ContainerLayout::Free, 0.4f, scale),
          _active(false),
          _drawsBackground(drawsBackground),
          _pointerSurface(drawsBackground)
    {
    }

    void VRUIPanel::attachToHandNode(RE::NiNode* handNode, const RE::NiPoint3& offset)
    {
        const bool sameTrackingNode = _trackingHandNode.get() == handNode;
        const bool alreadyAttached = _node && _node->parent == handNode;
        const bool preserveWorld =
            _preserveWorldOnNextHandAttach && _node && _node->parent;
        const RE::NiTransform preservedWorld = preserveWorld ?
            _node->world : RE::NiTransform{};
        _preserveWorldOnNextHandAttach = false;
        _offset = offset;
        _trackingHandNode.reset(handNode);
        _hasParkedTrackingOffset = false;

        if (handNode) {
            if (!alreadyAttached) {
                attachToNode(handNode);
                setLocalPosition(offset);
            }

            if (preserveWorld) {
                ApplyWorldTransform(_node.get(), preservedWorld);

                auto& settings = VRUISettings::get();
                RE::NiMatrix3 userRotation;
                userRotation.SetEulerAnglesXYZ(
                    settings.menuRotX * kDegToRad,
                    settings.menuRotY * kDegToRad,
                    (settings.menuRotZ + kPanelFaceCorrectionDegrees) * kDegToRad);
                _currentWorldRot = preservedWorld.rotate * userRotation.Transpose();
                _currentWorldPos = preservedWorld.translate;
                _hasTargetTransform = true;
                _smoothHandoffPosition = settings.bEnableMenuLerp;
                _handoffElapsed = 0.0f;

                RE::NiPoint3 handTarget;
                handTarget.x = handNode->world.translate.x +
                    _currentWorldRot.entry[0][0] * _offset.x +
                    _currentWorldRot.entry[0][1] * _offset.y +
                    _currentWorldRot.entry[0][2] * _offset.z;
                handTarget.y = handNode->world.translate.y +
                    _currentWorldRot.entry[1][0] * _offset.x +
                    _currentWorldRot.entry[1][1] * _offset.y +
                    _currentWorldRot.entry[1][2] * _offset.z;
                handTarget.z = handNode->world.translate.z +
                    _currentWorldRot.entry[2][0] * _offset.x +
                    _currentWorldRot.entry[2][1] * _offset.y +
                    _currentWorldRot.entry[2][2] * _offset.z;
                _handoffPositionCorrection = preservedWorld.translate - handTarget;
            }

            // Only reset smoothing when the actual anchor changes.
            if (!preserveWorld && (!sameTrackingNode || !alreadyAttached)) {
                _hasTargetTransform = false;
                _smoothHandoffPosition = false;
                _handoffElapsed = 0.0f;
            }

            logger::trace("DragonBoardVR: Panel '{}' attached directly to hand node '{}'", getName(), handNode->name.c_str());
        } else {
            logger::error("DragonBoardVR: Cannot attach panel '{}' - no target hand node provided", getName());
        }
    }

    bool VRUIPanel::parkAtWorldNode(RE::NiNode* worldRoot, RE::NiNode* trackingAnchor)
    {
        if (!worldRoot || !_node || !_node->parent) {
            return false;
        }

        const RE::NiTransform preservedWorld = _node->world;
        // The temporary bridge may use an independent OpenVR controller node
        // even though the panel is normally attached to an animated hand bone.
        _hasParkedTrackingOffset = trackingAnchor != nullptr;
        if (_hasParkedTrackingOffset) {
            _parkedTrackingOffset = MakeRelativeTransform(
                trackingAnchor->world,
                preservedWorld);
        }
        _trackingHandNode.reset();
        _smoothHandoffPosition = false;
        _handoffElapsed = 0.0f;

        detachFromParent();
        worldRoot->AttachChild(_node.get());

        ApplyWorldTransform(_node.get(), preservedWorld);
        return true;
    }

    void VRUIPanel::prepareSmoothHandHandoff()
    {
        _preserveWorldOnNextHandAttach =
            _hasParkedTrackingOffset && _node && _node->parent;
    }

    bool VRUIPanel::updateParkedTracking(RE::NiNode* trackingAnchor)
    {
        if (!_hasParkedTrackingOffset || !trackingAnchor || !_node || !_node->parent) {
            return false;
        }

        ApplyWorldTransform(
            _node.get(),
            ComposeTransform(trackingAnchor->world, _parkedTrackingOffset));
        return true;
    }

    void VRUIPanel::detachFromHandNode()
    {
        _trackingHandNode.reset();
        _hasParkedTrackingOffset = false;
        detachFromParent(); // Safely detaches _node from the player skeleton
        setVisible(false);
        _shown = false;
        _active = false;
        logger::trace("DragonBoardVR: Panel '{}' forcefully detached from hand", getName());
    }

    void VRUIPanel::show()
    {
        if (!_active) return;

        // Reset background state every time the panel shows so the NIF is retried
        // if it previously failed (e.g. after a path correction).
        if (_drawsBackground) {
            if (_backgroundLoadFailed || !_backgroundNode) {
                if (_backgroundNode && _node) {
                    _node->DetachChild(_backgroundNode.get());
                }
                _backgroundNode = nullptr;
                _backgroundLoadFailed = false;
            }
        }

        if (!_shown) {
            _shown = true;
            _fadeTimer = kFadeDuration;
            
            // Pre-warm transforms at 0.0 alpha before rendering un-culled
            update(0.0f);
            setVisible(true);
            logger::trace("DragonBoardVR: Showing panel '{}'", getName());
        }
    }

    void VRUIPanel::hide()
    {
        if (_shown) {
            _shown = false;
            _fadeTimer = kFadeDuration;
            logger::trace("DragonBoardVR: Hiding panel '{}'", getName());
        }
    }

    void VRUIPanel::update(float deltaTime)
    {
        // Handle fade animation
        if (_fadeTimer > 0.0f) {
            _fadeTimer -= deltaTime;
            if (_fadeTimer <= 0.0f) {
                _fadeTimer = 0.0f;
                if (!_shown) {
                    setVisible(false);
                }
            }
        }

        // Apply transforms from settings if correctly attached
        if (_node && _node->parent) {
            auto& settings = VRUISettings::get();
            auto& manager = VRMenuManager::get();

            if (manager.isBoardWorldPinned()) {
                applyPinnedWorldTransform();
            } else if (_trackingHandNode && settings.bEnableMenuLerp) {
                // Calculate target world transform from hand node
                RE::NiPoint3 targetWorldPos = _trackingHandNode->world.translate;
                
                // Rotation offset from settings
                RE::NiMatrix3 userRot;
                userRot.SetEulerAnglesXYZ(
                    settings.menuRotX * (kDegToRad),
                    settings.menuRotY * (kDegToRad),
                    (settings.menuRotZ + kPanelFaceCorrectionDegrees) * (kDegToRad)
                );
                
                RE::NiMatrix3 handRot = _trackingHandNode->world.rotate;

                if (!_hasTargetTransform) {
                    _currentWorldRot = handRot;
                    _hasTargetTransform = true;
                } else {
                    // Smooth lerp (Exponential smoothing)
                    float t = 1.0f - std::exp(-settings.fMenuLerpSpeed * deltaTime);
                    if (t > 1.0f) t = 1.0f;
                    if (t < 0.0f) t = 0.0f;

                    // Simple matrix lerp and orthogonalization for rotation smoothing
                    for (int r = 0; r < 3; ++r) {
                        for (int c = 0; c < 3; ++c) {
                            _currentWorldRot.entry[r][c] += (handRot.entry[r][c] - _currentWorldRot.entry[r][c]) * t;
                        }
                    }

                    // Gram-Schmidt Orthogonalization
                    RE::NiPoint3 col0(_currentWorldRot.entry[0][0], _currentWorldRot.entry[1][0], _currentWorldRot.entry[2][0]);
                    float mag0 = std::sqrt(col0.x*col0.x + col0.y*col0.y + col0.z*col0.z);
                    if (mag0 > 0.0001f) { col0.x /= mag0; col0.y /= mag0; col0.z /= mag0; }

                    RE::NiPoint3 col1(_currentWorldRot.entry[0][1], _currentWorldRot.entry[1][1], _currentWorldRot.entry[2][1]);
                    float dot01 = col0.x*col1.x + col0.y*col1.y + col0.z*col1.z;
                    col1.x -= dot01 * col0.x; col1.y -= dot01 * col0.y; col1.z -= dot01 * col0.z;
                    float mag1 = std::sqrt(col1.x*col1.x + col1.y*col1.y + col1.z*col1.z);
                    if (mag1 > 0.0001f) { col1.x /= mag1; col1.y /= mag1; col1.z /= mag1; }

                    RE::NiPoint3 col2;
                    col2.x = col0.y * col1.z - col0.z * col1.y;
                    col2.y = col0.z * col1.x - col0.x * col1.z;
                    col2.z = col0.x * col1.y - col0.y * col1.x;
                    
                    float mag2 = std::sqrt(col2.x*col2.x + col2.y*col2.y + col2.z*col2.z);
                    if (mag2 > 0.0001f) { col2.x /= mag2; col2.y /= mag2; col2.z /= mag2; }

                    _currentWorldRot.entry[0][0] = col0.x; _currentWorldRot.entry[1][0] = col0.y; _currentWorldRot.entry[2][0] = col0.z;
                    _currentWorldRot.entry[0][1] = col1.x; _currentWorldRot.entry[1][1] = col1.y; _currentWorldRot.entry[2][1] = col1.z;
                    _currentWorldRot.entry[0][2] = col2.x; _currentWorldRot.entry[1][2] = col2.y; _currentWorldRot.entry[2][2] = col2.z;
                }

                // Calculate the final smoothed World Position by pivoting rigidly around the LIVE hand position,
                // but projecting the offset outward using the SMOOTHED base hand rotation.
                // This puts the menu at the correct center point.
                RE::NiPoint3 liveHandPos = _trackingHandNode->world.translate;
                
                RE::NiPoint3 targetWorldPosWithOffset;
                targetWorldPosWithOffset.x = liveHandPos.x + _currentWorldRot.entry[0][0] * _offset.x + _currentWorldRot.entry[0][1] * _offset.y + _currentWorldRot.entry[0][2] * _offset.z;
                targetWorldPosWithOffset.y = liveHandPos.y + _currentWorldRot.entry[1][0] * _offset.x + _currentWorldRot.entry[1][1] * _offset.y + _currentWorldRot.entry[1][2] * _offset.z;
                targetWorldPosWithOffset.z = liveHandPos.z + _currentWorldRot.entry[2][0] * _offset.x + _currentWorldRot.entry[2][1] * _offset.y + _currentWorldRot.entry[2][2] * _offset.z;

                if (_smoothHandoffPosition) {
                    // Follow current hand translation immediately. Only the
                    // positional discontinuity introduced by reparenting is
                    // blended out, over a fixed and bounded interval.
                    constexpr float kHandoffDurationSeconds = 0.08f;
                    _handoffElapsed += (std::max)(deltaTime, 0.0f);
                    const float progress = (std::clamp)(
                        _handoffElapsed / kHandoffDurationSeconds,
                        0.0f,
                        1.0f);
                    const float smoothProgress =
                        progress * progress * (3.0f - 2.0f * progress);
                    _currentWorldPos = targetWorldPosWithOffset +
                        _handoffPositionCorrection * (1.0f - smoothProgress);

                    if (progress >= 1.0f) {
                        _smoothHandoffPosition = false;
                        _handoffElapsed = 0.0f;
                    }
                } else {
                    _currentWorldPos = targetWorldPosWithOffset;
                }

                // Now apply user rotation to the menu's local center
                RE::NiMatrix3 finalWorldRot = _currentWorldRot * userRot;

                // Transform smoothed world transform back to local space of the parent node
                RE::NiNode* parentNode = _node->parent;
                
                RE::NiPoint3 diff;
                diff.x = _currentWorldPos.x - parentNode->world.translate.x;
                diff.y = _currentWorldPos.y - parentNode->world.translate.y;
                diff.z = _currentWorldPos.z - parentNode->world.translate.z;

                const auto& rootScale = parentNode->world.scale;
                const auto& rootRot = parentNode->world.rotate;
                
                RE::NiPoint3 localPos;
                localPos.x = (rootRot.entry[0][0]*diff.x + rootRot.entry[1][0]*diff.y + rootRot.entry[2][0]*diff.z) / rootScale;
                localPos.y = (rootRot.entry[0][1]*diff.x + rootRot.entry[1][1]*diff.y + rootRot.entry[2][1]*diff.z) / rootScale;
                localPos.z = (rootRot.entry[0][2]*diff.x + rootRot.entry[1][2]*diff.y + rootRot.entry[2][2]*diff.z) / rootScale;

                RE::NiMatrix3 rootRotTransposed = rootRot.Transpose();
                
                RE::NiMatrix3 localRot = rootRotTransposed * finalWorldRot;

                _node->local.translate = localPos;
                _node->local.rotate = localRot;
                _node->local.scale = settings.menuScale;
            } else if (_trackingHandNode) {
                // Direct attach mode (Standard rigid following)
                _node->local.translate = _offset;
                
                RE::NiMatrix3 userRot;
                userRot.SetEulerAnglesXYZ(
                    settings.menuRotX * (kDegToRad),
                    settings.menuRotY * (kDegToRad),
                    (settings.menuRotZ + kPanelFaceCorrectionDegrees) * (kDegToRad)
                );
                _node->local.rotate = userRot;
                _node->local.scale = settings.menuScale;
            }

            // --- Update Background ---
            if (settings.showBackground && _drawsBackground) {
                if (!_backgroundNode && !_backgroundLoadFailed) {
                    logger::trace("DragonBoardVR: Loading background NIF '{}'", settings.backgroundNifPath);
                    // Background can come from complex/custom NIFs. Keep original shader behavior
                    // to avoid global flicker caused by aggressive UI shader flag overrides.
                    _backgroundNode = VRUIWidget::loadModelFromNif(settings.backgroundNifPath, false);
                    if (_backgroundNode) {
                        // Basic sanitization only (no UI shader flag mutations).
                        VRUIWidget::sanitizeModel(_backgroundNode.get(), false);

                        _node->AttachChild(_backgroundNode.get());
                        _backgroundLoadFailed = false;
                        logger::trace("DragonBoardVR: Background NIF loaded successfully (raw shader mode).");
                    } else {
                        _backgroundLoadFailed = true;
                        logger::warn("DragonBoardVR: Failed to load background NIF '{}'. Tablet will not be shown.", settings.backgroundNifPath);
                    }
                }

                if (_backgroundNode) {
                    _backgroundNode->local.translate = { settings.backgroundOffsetX, settings.backgroundOffsetY, settings.backgroundOffsetZ };
                    _backgroundNode->local.scale = settings.backgroundScale;
                    _backgroundNode->local.rotate.SetEulerAnglesXYZ(
                        settings.backgroundRotX * (kDegToRad),
                        settings.backgroundRotY * (kDegToRad),
                        settings.backgroundRotZ * (kDegToRad)
                    );

                    // --- Override with JSON Layout if available ---
                    auto bgLayout = VRUILayoutManager::getContainer("MainTablet");
                    if (bgLayout) {
                        _backgroundNode->local.translate = { bgLayout->transform.px, bgLayout->transform.py, bgLayout->transform.pz };
                        _backgroundNode->local.scale = bgLayout->transform.scale;
                        RE::NiMatrix3 jsonRot;
                        VRUILayoutManager::setMatrixEuler(jsonRot, 
                            bgLayout->transform.rx * kDegToRad,
                            bgLayout->transform.ry * kDegToRad,
                            bgLayout->transform.rz * kDegToRad);
                        _backgroundNode->local.rotate = jsonRot;
                    } else {
                        // NEW: Register the default background transform in the JSON if it's missing
                        VRUILayoutManager::registerDefaultContainer("MainTablet", "Background",
                            _backgroundNode->local.translate,
                            _backgroundNode->local.rotate,
                            _backgroundNode->local.scale);
                    }

                    float alpha = _shown ? (1.0f - (_fadeTimer / kFadeDuration)) : (_fadeTimer / kFadeDuration);
                    alpha = std::max(0.0f, std::min(alpha, 1.0f));

                    // Keep background in raw shader mode: avoid per-frame material alpha writes,
                    // which can cause lighting flicker if materials are shared.
                    _lastAlpha = alpha;
                }
            } else if (_backgroundNode) {
                _node->DetachChild(_backgroundNode.get());
                _backgroundNode = nullptr;
            }

            RE::NiUpdateData updateData;
            _node->Update(updateData);
        }

        // Always update container to allow deferred loading in the background
        VRUIContainer::update(deltaTime);
    }

    void VRUIPanel::applyPinnedWorldTransform()
    {
        if (!_node || !_node->parent) {
            return;
        }

        auto& manager = VRMenuManager::get();
        RE::NiNode* parentNode = _node->parent;
        if (!parentNode) {
            return;
        }

        RE::NiPoint3 worldPos = manager.getBoardWorldPosition();
        RE::NiMatrix3 worldRot = manager.getBoardWorldRotation();
        float worldScale = manager.getBoardWorldScale();
        float currentMenuScale = std::max(0.001f, VRUISettings::get().menuScale);
        if (float pinnedBaseScale = std::max(0.001f, manager.getBoardPinnedMenuScaleBase()); pinnedBaseScale > 0.0f) {
            worldScale *= (currentMenuScale / pinnedBaseScale);
        }

        RE::NiPoint3 diff;
        diff.x = worldPos.x - parentNode->world.translate.x;
        diff.y = worldPos.y - parentNode->world.translate.y;
        diff.z = worldPos.z - parentNode->world.translate.z;

        float parentScale = parentNode->world.scale > 0.0001f ? parentNode->world.scale : 1.0f;
        RE::NiMatrix3 parentRotT = parentNode->world.rotate.Transpose();

        RE::NiPoint3 localPos;
        localPos.x = (parentRotT.entry[0][0] * diff.x + parentRotT.entry[0][1] * diff.y + parentRotT.entry[0][2] * diff.z) / parentScale;
        localPos.y = (parentRotT.entry[1][0] * diff.x + parentRotT.entry[1][1] * diff.y + parentRotT.entry[1][2] * diff.z) / parentScale;
        localPos.z = (parentRotT.entry[2][0] * diff.x + parentRotT.entry[2][1] * diff.y + parentRotT.entry[2][2] * diff.z) / parentScale;

        _node->local.translate = localPos;
        _node->local.rotate = parentRotT * worldRot;
        _node->local.scale = worldScale;
    }

    void VRUIPanel::collectButtons(std::vector<VRUIButton*>& outButtons)
    {
        collectButtonsRecursive(this, outButtons);
    }

    void VRUIPanel::collectButtonsRecursive(VRUIWidget* widget, std::vector<VRUIButton*>& outButtons)
    {
        if (!widget) return;
        
        auto* button = dynamic_cast<VRUIButton*>(widget);
        if (button) {
            outButtons.push_back(button);
        }

        for (auto& child : widget->getChildren()) {
            collectButtonsRecursive(child.get(), outButtons);
        }
    }

    void VRUIPanel::onChildLayoutChanged(VRUIWidget*)
    {
        recalculateLayout();
    }

    void VRUIPanel::recalculateLayout()
    {
        // Delegate layout to the base container class.
        // NOTE: We intentionally do NOT apply any automatic minZ shift here.
        // Previously, shifting all children by minZ caused every fixed (sidebar) button
        // to offset whenever a sibling's bounding box changed (e.g. after being grabbed).
        // The panel's vertical anchor is controlled solely by fMainPanelOffsetZ in the INI.
        VRUIContainer::recalculateLayout();
    }

    RE::NiPoint3 VRUIPanel::getWorldPosition() const
    {
        return _node ? _node->world.translate : RE::NiPoint3{};
    }

    RE::NiMatrix3 VRUIPanel::getWorldRotation() const
    {
        return _node ? _node->world.rotate : RE::NiMatrix3{};
    }

    float VRUIPanel::getWorldScale() const
    {
        return _node ? _node->world.scale : 1.0f;
    }
}
