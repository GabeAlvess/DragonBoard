#include "PointerVisualController.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

#include <cmath>

namespace dragonboard::ui::pointer
{
    void PointerVisualController::Update(
        vrui::VRMenuManager& manager,
        RE::NiNode* dominantHand,
        const RE::NiPoint3& hitPosition,
        RE::NiNode* panelNode)
    {
        auto& beam = manager._pointerVisual.Beam();
        auto& reticle = manager._pointerVisual.Reticle();
        auto& smoothedPosition = manager._pointerVisual.SmoothedPosition();

        if (!beam || !reticle || !dominantHand || !panelNode) {
            return;
        }

        if (!manager._pointerVisual.IsActive()) {
            if (auto* controlMap = RE::ControlMap::GetSingleton()) {
                controlMap->ToggleControls(RE::ControlMap::UEFlag::kMainFour, false);
            }
        }
        manager._pointerVisual.SetActive(true);

        const bool parentChanged = reticle->parent != panelNode;
        if (parentChanged) {
            if (reticle->parent) {
                reticle->parent->DetachChild(reticle.get());
            }
            panelNode->AttachChild(reticle.get());
        }

        const float panelScale = panelNode->world.scale > 0.001f ? panelNode->world.scale : 1.0f;
        auto& rotation = panelNode->world.rotate;
        const RE::NiPoint3 delta(
            hitPosition.x - panelNode->world.translate.x,
            hitPosition.y - panelNode->world.translate.y,
            hitPosition.z - panelNode->world.translate.z);
        RE::NiPoint3 localHit(
            (rotation.entry[0][0] * delta.x + rotation.entry[1][0] * delta.y +
             rotation.entry[2][0] * delta.z) / panelScale,
            (rotation.entry[0][1] * delta.x + rotation.entry[1][1] * delta.y +
             rotation.entry[2][1] * delta.z) / panelScale,
            (rotation.entry[0][2] * delta.x + rotation.entry[1][2] * delta.y +
             rotation.entry[2][2] * delta.z) / panelScale);

        const auto safeFloat = [](float value, float fallback = 0.0f) {
            return (std::isfinite(value) && std::abs(value) < 1e5f) ? value : fallback;
        };
        localHit.x = safeFloat(localHit.x);
        localHit.y = safeFloat(localHit.y);
        localHit.z = safeFloat(localHit.z);

        auto& settings = vrui::VRUISettings::get();
        constexpr float degreesToRadians = 3.14159265f / 180.0f;
        constexpr float lerpSpeed = 20.0f;
        constexpr float deltaTime = 0.011f;
        const float lerpAmount = 1.0f - std::exp(-lerpSpeed * deltaTime);

        const RE::NiPoint3 targetPosition(
            localHit.x + settings.reticleOffsetX,
            settings.reticleOffsetY,
            localHit.z + settings.reticleOffsetZ);
        if (parentChanged) {
            smoothedPosition = targetPosition;
        } else {
            smoothedPosition.x +=
                (targetPosition.x - smoothedPosition.x) * lerpAmount;
            smoothedPosition.z +=
                (targetPosition.z - smoothedPosition.z) * lerpAmount;
        }
        smoothedPosition.y = targetPosition.y;

        reticle->local.translate = smoothedPosition;
        reticle->local.scale = settings.reticleScaleX;

        RE::NiMatrix3 reticleRotation;
        reticleRotation.SetEulerAnglesXYZ(
            settings.reticleRotX * degreesToRadians,
            settings.reticleRotY * degreesToRadians,
            settings.reticleRotZ * degreesToRadians);
        reticle->local.rotate = reticleRotation;

        if (manager._pointerVisual.IsReticleSuppressed()) {
            reticle->SetAppCulled(true);
            if (beam->parent) {
                beam->parent->DetachChild(beam.get());
            }
            beam->SetAppCulled(true);
            return;
        }

        reticle->SetAppCulled(false);

        if (beam->parent) {
            beam->parent->DetachChild(beam.get());
        }
        beam->SetAppCulled(true);

        RE::NiUpdateData updateData;
        updateData.time = 0.0f;
        updateData.flags = RE::NiUpdateData::Flag::kDirty;
        reticle->Update(updateData);
        reticle->UpdateWorldBound();
    }

    void PointerVisualController::Hide(vrui::VRMenuManager& manager)
    {
        if (!manager._pointerVisual.IsActive()) return;

        auto& beam = manager._pointerVisual.Beam();
        auto& reticle = manager._pointerVisual.Reticle();
        if (beam && beam->parent) {
            beam->parent->DetachChild(beam.get());
        }
        if (reticle && reticle->parent) {
            reticle->parent->DetachChild(reticle.get());
        }
        manager._pointerVisual.SetActive(false);

        if (auto* controlMap = RE::ControlMap::GetSingleton()) {
            controlMap->ToggleControls(RE::ControlMap::UEFlag::kMainFour, true);
        }
    }

    void PointerVisualController::SetReticleSuppressed(
        vrui::VRMenuManager& manager,
        bool suppressed)
    {
        if (manager._pointerVisual.IsReticleSuppressed() == suppressed) {
            return;
        }

        manager._pointerVisual.SetReticleSuppressed(suppressed);
        auto& reticle = manager._pointerVisual.Reticle();
        if (reticle) {
            reticle->SetAppCulled(suppressed);
        }
    }
}
