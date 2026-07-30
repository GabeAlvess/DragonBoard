#include "VRMenuManager.h"
#include "runtime/vr/HapticFeedback.h"
#include "ui/panels/PanelManagementController.h"
#include "ui/menu/MenuLifecycleController.h"
#include "ui/menu/MenuInitializationController.h"
#include "ui/frame/FrameUpdateController.h"
#include "ui/runtime/DeferredActionController.h"
#include "ui/equipment/EquipInteractionController.h"
#include "ui/rml/RmlPanelHost.h"
#include "VRUIHandTracking.h"
#include "VRUIItemEditPanel.h"
#include "VRUIInventoryContainer.h"
#include "VRUIMagicContainer.h"
#include "VRUISettings.h"
#include <RE/N/NiNode.h>

namespace vrui
{
    VRMenuManager& VRMenuManager::get()
    {
        static VRMenuManager instance;
        return instance;
    }

    void VRMenuManager::initialize()
    {
        dragonboard::ui::menu::MenuInitializationController::Initialize(*this);
    }

    void VRMenuManager::onFrameUpdate(float deltaTime)
    {
        dragonboard::ui::frame::FrameUpdateController::Update(*this, deltaTime);
    }

    void VRMenuManager::registerPanel(std::shared_ptr<VRUIPanel> panel)
    {
        dragonboard::ui::panels::PanelManagementController::Register(*this, std::move(panel));
    }

    void VRMenuManager::unregisterPanel(const std::shared_ptr<VRUIPanel>& panel)
    {
        dragonboard::ui::panels::PanelManagementController::Unregister(*this, panel);
    }

    RE::NiPoint3 VRMenuManager::getPanelOffset() const
    {
        return dragonboard::ui::panels::PanelManagementController::GetPanelOffset();
    }

    RE::NiNode* VRMenuManager::resolvePinnedAttachNode(RE::NiNode* skeletonRoot) const
    {
        return dragonboard::ui::panels::PanelManagementController::ResolvePinnedAttachNode(
            *this, skeletonRoot);
    }

    void VRMenuManager::switchToPanel(const std::string& panelName)
    {
        auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
        if (panelName == "InventoryPanel") {
            const auto inventoryPanel = findPanelByName("InventoryPanel");
            const auto previewPanel = findPanelByName("ItemEditPanel");
            auto* inventory = inventoryPanel ?
                dynamic_cast<VRUIInventoryContainer*>(
                    inventoryPanel->findWidgetByName("InventoryPanel_Grid")) : nullptr;
            auto* preview = previewPanel ?
                dynamic_cast<VRUIItemEditPanel*>(
                    previewPanel->findWidgetByName("ItemEditContainer")) : nullptr;
            if (inventory && preview && rmlHost.OpenInventory(inventory, preview)) {
                dragonboard::ui::panels::PanelManagementController::SwitchTo(
                    *this, "ItemEditPanel");
                return;
            }
            logger::error(
                "DragonBoardVR: RmlUi inventory unavailable; classic fallback was removed.");
            rmlHost.Close();
            return;
        }
        if (panelName == "MagicPanel") {
            const auto magicPanel = findPanelByName("MagicPanel");
            const auto previewPanel = findPanelByName("ItemEditPanel");
            auto* magic = magicPanel ?
                dynamic_cast<VRUIMagicContainer*>(
                    magicPanel->findWidgetByName("MagicPanel_Grid")) : nullptr;
            auto* preview = previewPanel ?
                dynamic_cast<VRUIItemEditPanel*>(
                    previewPanel->findWidgetByName("ItemEditContainer")) : nullptr;
            if (magic && preview && rmlHost.OpenMagic(magic, preview)) {
                dragonboard::ui::panels::PanelManagementController::SwitchTo(
                    *this, "ItemEditPanel");
                return;
            }
            logger::error(
                "DragonBoardVR: RmlUi magic unavailable; classic fallback was removed.");
            rmlHost.Close();
            return;
        }
        if (panelName == "ModsPanel") {
            if (rmlHost.OpenMods()) {
                dragonboard::ui::panels::PanelManagementController::SwitchTo(*this, panelName);
            } else {
                logger::error(
                    "DragonBoardVR: RmlUi Mods unavailable; classic fallback was removed.");
                rmlHost.Close();
            }
            return;
        }
        if (panelName == "ItemEditPanel") {
            if (const auto panel = findPanelByName(panelName)) {
                auto* editor = dynamic_cast<VRUIItemEditPanel*>(
                    panel->findWidgetByName("ItemEditContainer"));
                if (editor && rmlHost.OpenItemEdit(editor)) {
                    // Keep only the selected item's 3D preview active behind
                    // the RmlUi surface. The classic editor controls remain
                    // hidden and the source inventory stays detached.
                    editor->setRmlPreviewLayout(
                        VRUIItemEditPanel::RmlPreviewLayout::ItemEditor);
                    editor->setRmlPreviewMode(true);
                    dragonboard::ui::panels::PanelManagementController::SwitchTo(*this, panelName);
                    return;
                }
            }
            logger::error(
                "DragonBoardVR: RmlUi item editor unavailable; classic fallback was removed.");
            rmlHost.Close();
            return;
        }
        rmlHost.Close();
        dragonboard::ui::panels::PanelManagementController::SwitchTo(*this, panelName);
    }

    void VRMenuManager::togglePanel(const std::string& panelName)
    {
        auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
        bool targetIsOpen = false;

        if (panelName == "InventoryPanel") {
            targetIsOpen = rmlHost.IsInventoryOpen();
        } else if (panelName == "MagicPanel") {
            targetIsOpen = rmlHost.IsMagicOpen();
        } else if (panelName == "ModsPanel") {
            targetIsOpen = rmlHost.IsModsOpen();
        } else if (!rmlHost.IsOpen()) {
            const auto target = findPanelByName(panelName);
            targetIsOpen = target && target->isActive() && target->isShown();
        }

        if (targetIsOpen) {
            rmlHost.Close();
            navigateHome();
            dragonboard::ui::panels::PanelManagementController::SwitchTo(*this, "MainPanel");
            logger::trace(
                "DragonBoardVR: toggled active panel '{}' closed; returned to MainPanel.",
                panelName);
            return;
        }

        switchToPanel(panelName);
    }

    std::shared_ptr<VRUIPanel> VRMenuManager::findPanelByName(const std::string& name)
    {
        return dragonboard::ui::panels::PanelManagementController::FindByName(*this, name);
    }

    void VRMenuManager::refreshActivePanels()
    {
        _refreshCoordinator.RequestAll();
    }

    void VRMenuManager::setBoardWorldPinned(bool pinned)
    {
        dragonboard::ui::panels::PanelManagementController::SetWorldPinned(*this, pinned);
    }

    bool VRMenuManager::isBoardWorldPinned() const
    {
        return _boardPinState.IsPinned();
    }

    void VRMenuManager::refreshActiveDynamicContainers()
    {
        _refreshCoordinator.RequestDynamic();
    }

    void VRMenuManager::refreshFixedWidgets()
    {
        _refreshCoordinator.RequestFixedWidgets();
    }

    void VRMenuManager::closeMenu()
    {
        auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
        rmlHost.Close();
        if (!_menuSession.IsOpen()) return; // Already closed, nothing to do
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([]() {
                dragonboard::ui::menu::MenuLifecycleController::ApplySafeClose(VRMenuManager::get());
            });
        }
    }

    void VRMenuManager::toggleMenu(bool suppressToggleHaptic)
    {
        // Debounce: prevent multiple triggers from same gesture/frame
        if (!_menuToggleCooldown.TryConsume(0.5f)) return;

        auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
        rmlHost.Close();

        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([suppressToggleHaptic]() {
                dragonboard::ui::menu::MenuLifecycleController::ApplyToggle(
                    VRMenuManager::get(), suppressToggleHaptic);
            });
        }
    }

    void VRMenuManager::clearHover()
    {
        if (auto hovered = _interactionFocus.GetHovered()) {
            hovered->onRayExit();
        }
        _interactionFocus.ClearHover();
    }

    void VRMenuManager::restartDragonBoard()
    {
        dragonboard::ui::menu::MenuLifecycleController::Restart(*this);
    }

    void VRMenuManager::performSkeletonChangeSafely(std::function<void()> change)
    {
        dragonboard::ui::equipment::EquipInteractionController::PerformSkeletonSafeChange(
            *this, std::move(change));
    }

    void VRMenuManager::clearGrabbedWidget(VRUIWidget* widget)
    {
        _interactionFocus.ClearGrabbed(widget);
    }

    // =====================================================================
    // External Input Callbacks (called by SkyrimVRTools or KeyHandler)
    // =====================================================================

    void VRMenuManager::onGripButtonChanged(bool pressed)
    {
        _inputButtons.SetGrip(pressed);
    }

    void VRMenuManager::onDominantGripButtonChanged(bool pressed)
    {
        _inputButtons.SetDominantGrip(pressed);
    }

    void VRMenuManager::onOffhandGripButtonChanged(bool pressed)
    {
        _inputButtons.SetOffhandGrip(pressed);
    }

    void VRMenuManager::onTriggerButtonChanged(bool pressed)
    {
        _inputButtons.SetTrigger(pressed);
    }

    void VRMenuManager::onOffhandTriggerButtonChanged(bool pressed)
    {
        _inputButtons.SetOffhandTrigger(pressed);
    }

    void VRMenuManager::onThumbstickButtonChanged(bool pressed)
    {
        _inputButtons.SetThumbstick(pressed);
    }

    void VRMenuManager::onSecondaryButtonChanged(bool pressed)
    {
        _inputButtons.SetSecondary(pressed);
    }

    void VRMenuManager::onDominantSecondaryButtonChanged(bool pressed)
    {
        _inputButtons.SetDominantSecondary(pressed);
    }

    void VRMenuManager::onHotkey8ButtonChanged(bool pressed)
    {
        _inputButtons.SetHotkey8(pressed);
        if (pressed) {
            toggleMenu();
        }
    }

    // =====================================================================
    // Hand Node Discovery
    // =====================================================================

    RE::NiNode* VRMenuManager::getMenuHandNode() const
    {
        return VRUIHandTracking::getMenuHandNode(_isVRIKInstalled);
    }

    RE::NiNode* VRMenuManager::getMenuControllerNode() const
    {
        return VRUIHandTracking::getMenuControllerNode(_isVRIKInstalled);
    }

    RE::NiNode* VRMenuManager::getDominantHandNode() const
    {
        return VRUIHandTracking::getDominantHandNode(_isVRIKInstalled);
    }

    RE::NiNode* VRMenuManager::getNonDominantHandNode() const
    {
        return VRUIHandTracking::getNonDominantHandNode(_isVRIKInstalled);
    }

    RE::NiNode* VRMenuManager::getLeftHandNode() const
    {
        return VRUIHandTracking::getLeftHandNode(_isVRIKInstalled);
    }

    RE::NiNode* VRMenuManager::getRightHandNode() const
    {
        return VRUIHandTracking::getRightHandNode(_isVRIKInstalled);
    }

    RE::NiNode* VRMenuManager::getPlayerSkeletonRoot() const
    {
        return VRUIHandTracking::getPlayerSkeletonRoot(_isVRIKInstalled);
    }

    RE::NiNode* VRMenuManager::getPinnedAttachNode() const
    {
        return resolvePinnedAttachNode(getPlayerSkeletonRoot());
    }

    RE::NiNode* VRMenuManager::getHeadNode() const
    {
        return VRUIHandTracking::getHeadNode(_isVRIKInstalled);
    }

    // =====================================================================
    // Haptic Feedback
    // =====================================================================

    void VRMenuManager::triggerHaptic(
        bool isDominantHand,
        float intensity,
        float duration)
    {
        const auto& settings = VRUISettings::get();
        dragonboard::runtime::vr::TriggerHaptic(
            isDominantHand,
            settings.useLeftHandAsMenu,
            settings.isNativeLeftHandedMode(),
            intensity,
            duration);
    }

    RE::NiPoint3 VRMenuManager::getLaserOrigin() const
    {
        auto* dominantHand = getDominantHandNode();
        return dominantHand ? dominantHand->world.translate : RE::NiPoint3();
    }

    RE::NiPoint3 VRMenuManager::getLaserDirection() const
    {
        auto* dominantHand = getDominantHandNode();
        if (!dominantHand) return RE::NiPoint3(0, 0, 1);

        RE::NiMatrix3& rot = dominantHand->world.rotate;
        return RE::NiPoint3(rot.entry[0][2], rot.entry[1][2], rot.entry[2][2]);
    }

    bool VRMenuManager::canEquip() const
    {
        return dragonboard::ui::equipment::EquipInteractionController::CanExecute(*this);
    }

    void VRMenuManager::notifyEquip()
    {
        dragonboard::ui::equipment::EquipInteractionController::NotifyExecuted(*this);
    }

    void VRMenuManager::scheduleEquipRefresh(float delay)
    {
        dragonboard::ui::equipment::EquipInteractionController::RequestRefresh(*this, delay);
    }

    void VRMenuManager::requestSettingsSave(float delay)
    {
        dragonboard::ui::runtime::DeferredActionController::RequestSettingsSave(*this, delay);
    }

    void VRMenuManager::saveSettingsNow()
    {
        dragonboard::ui::runtime::DeferredActionController::SaveSettingsNow(*this);
    }

    void VRMenuManager::executeConsoleCommand(const std::string& command, bool isDangerous, const char* logLabel)
    {
        dragonboard::ui::runtime::DeferredActionController::ExecuteConsoleCommand(
            *this, command, isDangerous, logLabel);
    }
}
