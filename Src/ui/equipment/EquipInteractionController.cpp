#include "EquipInteractionController.h"

#include "ui/refresh/PanelRefresher.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"

#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESFile.h>
#include <RE/T/TESForm.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string_view>

namespace dragonboard::ui::equipment
{
    namespace
    {
        struct SkeletonSwapState
        {
            bool active = false;
            float elapsed = 0.0f;
            RE::NiNode* stableSkeletonRoot = nullptr;
            RE::NiNode* stableHandAnchor = nullptr;
            RE::NiPointer<RE::NiNode> trackingAnchor;
            std::uint32_t observedEquipmentEvent = 0;
            std::uint32_t stableFrames = 0;
            std::size_t parkedPanels = 0;
            float equipmentQuietTime = 0.0f;
        };

        SkeletonSwapState skeletonSwap;
        std::atomic_uint32_t equipmentEventSerial{ 0 };

        constexpr float kMinimumWorldHoldSeconds = 0.035f;
        constexpr float kEquipmentCascadeQuietSeconds = 0.12f;
        constexpr float kMaximumWorldHoldSeconds = 1.0f;
        constexpr std::uint32_t kRequiredStableFrames = 3;
        constexpr std::string_view kNavigateVrPlugin =
            "Navigate VR - Equipable Dynamic Compass and Maps.esp";

        bool equalsIgnoreCase(std::string_view left, std::string_view right)
        {
            return left.size() == right.size() &&
                std::equal(left.begin(), left.end(), right.begin(),
                    [](unsigned char lhs, unsigned char rhs) {
                        return std::tolower(lhs) == std::tolower(rhs);
                    });
        }
    }

    bool EquipInteractionController::CanExecute(const vrui::VRMenuManager& manager)
    {
        return manager._equipCooldown.IsReady(vrui::VRUISettings::get().equipCooldown);
    }

    void EquipInteractionController::NotifyExecuted(vrui::VRMenuManager& manager)
    {
        manager._equipCooldown.Consume();
    }

    void EquipInteractionController::RequestRefresh(
        vrui::VRMenuManager& manager,
        float delay)
    {
        manager._equipRefreshScheduler.Request(delay);
    }

    bool EquipInteractionController::RequiresSkeletonBridge(const RE::TESForm* form)
    {
        if (!form) return false;
        if (form->Is(RE::FormType::Armor)) return true;
        if (!form->Is(RE::FormType::Weapon)) return false;

        const auto* sourceFile = form->GetFile(0);
        return sourceFile &&
            equalsIgnoreCase(sourceFile->GetFilename(), kNavigateVrPlugin);
    }

    void EquipInteractionController::PerformSkeletonSafeChange(
        vrui::VRMenuManager& manager,
        std::function<void()> change)
    {
        if (!change) return;

        if (!manager._menuSession.IsOpen()) {
            change();
            return;
        }

        if (!skeletonSwap.active) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            auto* loadedCell = cell ? cell->GetRuntimeData().loadedData : nullptr;
            RE::NiNode* worldRoot = loadedCell ? loadedCell->cell3D.get() : nullptr;
            if (!worldRoot) {
                auto* skeletonRoot = manager.getPlayerSkeletonRoot();
                worldRoot = skeletonRoot && skeletonRoot->parent ?
                    skeletonRoot->parent->AsNode() : nullptr;
            }
            if (!worldRoot) {
                logger::warn(
                    "DragonBoardVR: World root unavailable; performing armor change without visual bridge.");
                change();
                return;
            }

            std::size_t parkedPanels = 0;
            auto* trackingAnchor = manager._boardPinState.IsPinned() ?
                nullptr : manager.getMenuControllerNode();
            if (!trackingAnchor && !manager._boardPinState.IsPinned()) {
                trackingAnchor = manager.getMenuHandNode();
                logger::warn(
                    "DragonBoardVR: VR controller anchor unavailable; skeleton bridge is using the animated hand fallback.");
            }
            for (const auto& panel : manager._panelRegistry.GetPanels()) {
                if (!panel || !panel->isActive() || !panel->isShown()) continue;
                if (panel->parkAtWorldNode(worldRoot, trackingAnchor)) {
                    ++parkedPanels;
                }
            }

            if (parkedPanels == 0) {
                change();
                return;
            }

            skeletonSwap = {};
            skeletonSwap.active = true;
            skeletonSwap.parkedPanels = parkedPanels;
            skeletonSwap.trackingAnchor.reset(trackingAnchor);
            skeletonSwap.observedEquipmentEvent =
                equipmentEventSerial.load(std::memory_order_acquire);
            skeletonSwap.equipmentQuietTime = kEquipmentCascadeQuietSeconds;
            logger::info(
                "DragonBoardVR: parked {} visible panels using tracking anchor '{}' for skeleton-safe armor change.",
                parkedPanels,
                trackingAnchor ? trackingAnchor->name.c_str() : "<world>");
        } else {
            skeletonSwap.elapsed = 0.0f;
            skeletonSwap.stableFrames = 0;
            skeletonSwap.stableSkeletonRoot = nullptr;
            skeletonSwap.stableHandAnchor = nullptr;
            skeletonSwap.observedEquipmentEvent =
                equipmentEventSerial.load(std::memory_order_acquire);
            skeletonSwap.equipmentQuietTime = 0.0f;
        }

        change();
    }

    void EquipInteractionController::NotifyPlayerEquipmentChanged()
    {
        equipmentEventSerial.fetch_add(1, std::memory_order_release);
    }

    void EquipInteractionController::Update(vrui::VRMenuManager& manager, float deltaTime)
    {
        manager._equipCooldown.Advance(deltaTime);

        if (skeletonSwap.active) {
            const float frameTime = (std::max)(deltaTime, 0.0f);
            skeletonSwap.elapsed += frameTime;

            const auto currentEquipmentEvent =
                equipmentEventSerial.load(std::memory_order_acquire);
            if (currentEquipmentEvent != skeletonSwap.observedEquipmentEvent) {
                skeletonSwap.observedEquipmentEvent = currentEquipmentEvent;
                skeletonSwap.equipmentQuietTime = 0.0f;
                skeletonSwap.stableFrames = 0;
                skeletonSwap.stableSkeletonRoot = nullptr;
                skeletonSwap.stableHandAnchor = nullptr;
            } else {
                skeletonSwap.equipmentQuietTime += frameTime;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* skeletonRoot = manager.getPlayerSkeletonRoot();
            auto* handAnchor = manager.getMenuHandNode();
            auto* trackingAnchor = skeletonSwap.trackingAnchor.get();
            const bool anchorReady =
                player && player->Is3DLoaded() && skeletonRoot && handAnchor;

            if (anchorReady && !manager._boardPinState.IsPinned()) {
                for (const auto& panel : manager._panelRegistry.GetPanels()) {
                    if (panel && panel->isActive() && panel->isShown()) {
                        (void)panel->updateParkedTracking(trackingAnchor);
                    }
                }
            }

            if (anchorReady &&
                skeletonRoot == skeletonSwap.stableSkeletonRoot &&
                handAnchor == skeletonSwap.stableHandAnchor) {
                ++skeletonSwap.stableFrames;
            } else {
                skeletonSwap.stableSkeletonRoot = skeletonRoot;
                skeletonSwap.stableHandAnchor = handAnchor;
                skeletonSwap.stableFrames = anchorReady ? 1u : 0u;
            }

            const bool stableLongEnough =
                skeletonSwap.elapsed >= kMinimumWorldHoldSeconds &&
                skeletonSwap.equipmentQuietTime >= kEquipmentCascadeQuietSeconds &&
                skeletonSwap.stableFrames >= kRequiredStableFrames;
            const bool timedOut = skeletonSwap.elapsed >= kMaximumWorldHoldSeconds;

            if (stableLongEnough || (timedOut && anchorReady)) {
                const auto parkedPanels = skeletonSwap.parkedPanels;
                const auto elapsed = skeletonSwap.elapsed;
                if (!manager._boardPinState.IsPinned()) {
                    for (const auto& panel : manager._panelRegistry.GetPanels()) {
                        if (panel && panel->isActive() && panel->isShown()) {
                            panel->prepareSmoothHandHandoff();
                        }
                    }
                }
                manager._rebuildOnNextOpen = true;
                skeletonSwap = {};
                manager._refreshCoordinator.RequestTransforms();
                logger::info(
                    "DragonBoardVR: restoring {} panels to the stable player anchor after {:.3f}s.",
                    parkedPanels,
                    elapsed);
            }
        }

        if (manager._equipRefreshScheduler.Update(deltaTime)) {
            dragonboard::ui::refresh::PanelRefresher::UpdateEquippedStates(
                manager._panelRegistry.GetPanels());
        }
    }
}
