#pragma once

#include "core/DeferredTaskQueue.h"
#include "core/CooldownTimer.h"
#include "ui/refresh/EquipRefreshScheduler.h"
#include "ui/refresh/RefreshCoordinator.h"
#include "ui/panels/PanelRegistry.h"
#include "ui/panels/BoardPinState.h"
#include "ui/input/ActivationHoldTracker.h"
#include "ui/input/PressHoldTracker.h"
#include "ui/input/InteractionFocusState.h"
#include "ui/input/InputButtonState.h"
#include "ui/settings/SettingsSaveScheduler.h"
#include "ui/settings/IniChangeWatcher.h"
#include "ui/menu/MenuSessionState.h"
#include "ui/navigation/PageNavigationState.h"
#include "ui/pointer/PointerVisualState.h"
#include "VRUIPanel.h"
#include "VRUISettings.h"

#include <vector>
#include <memory>

namespace dragonboard::ui::widgets
{
    class FixedWidgetPresenter;
}

namespace dragonboard::ui::menu
{
    class MenuLifecycleController;
    class MenuInitializationController;
}

namespace dragonboard::ui::input
{
    class InteractionInputController;
    class PointerInteractionController;
}

namespace dragonboard::ui::frame
{
    class FrameUpdateController;
}

namespace dragonboard::ui::pointer
{
    class PointerVisualController;
}

namespace dragonboard::ui::refresh
{
    class RefreshPipelineController;
}

namespace dragonboard::ui::runtime
{
    class DeferredActionController;
}

namespace dragonboard::ui::panels
{
    class PanelManagementController;
}

namespace dragonboard::ui::equipment
{
    class EquipInteractionController;
}

namespace vrui
{
    class VRUIContainer;

    /// Central singleton that orchestrates the DragonBoardVR framework.
    /// Manages panels, processes input, performs raycast, and dispatches events.
    class VRMenuManager
    {
    public:
        static VRMenuManager& get();

        /// Initialize the manager (call once after game data is loaded)
        void initialize();

        /// Called every frame to update all managed panels and input
        void onFrameUpdate(float deltaTime);

        /// Register a panel to be managed
        void registerPanel(std::shared_ptr<VRUIPanel> panel);

        /// Unregister and destroy a panel
        void unregisterPanel(const std::shared_ptr<VRUIPanel>& panel);

        /// Switch active panel without closing menu
        void switchToPanel(const std::string& panelName);

        /// Find a registered panel by name
        std::shared_ptr<VRUIPanel> findPanelByName(const std::string& name);

        /// Trigger a full layout refresh on all panels (updates scale, spacing, etc)
        void refreshActivePanels();
        void refreshActiveDynamicContainers();
        void refreshFixedWidgets();
        void setBoardWorldPinned(bool pinned);
        bool isBoardWorldPinned() const;
        RE::NiPoint3 getBoardWorldPosition() const { return _boardPinState.GetPosition(); }
        RE::NiMatrix3 getBoardWorldRotation() const { return _boardPinState.GetRotation(); }
        float getBoardWorldScale() const { return _boardPinState.GetWorldScale(); }
        float getBoardPinnedMenuScaleBase() const { return _boardPinState.GetMenuScaleBase(); }

        /// Toggle menu visibility (called by activation gesture)
        void toggleMenu();
        void closeMenu();  // Only closes — never opens. Safe to call before console commands.
        void performArmorChangeSafely(std::function<void()> change);

        /// Check if any menu is currently visible
        bool isMenuOpen() const { return _menuSession.IsOpen(); }

        // --- Active container navigation (called by persistent nav buttons) ---

        /// Set the container that Prev/Next page buttons control
        void setActivePageableContainer(std::shared_ptr<VRUIContainer> container) { _pageNavigation.SetActive(container); }

        /// Navigate to the next page of the active container
        void navigateNext() { _pageNavigation.Next(); }

        /// Navigate to the previous page of the active container
        void navigatePrev() { _pageNavigation.Previous(); }

        /// Navigate to page 0 of the active container
        void navigateHome() { _pageNavigation.Home(); }

        /// Get the currently hovered widget (if any)
        std::shared_ptr<VRUIWidget> getHoveredWidget() const { return _interactionFocus.GetHovered(); }
        std::shared_ptr<VRUIWidget> getGrabbedWidget() const { return _interactionFocus.GetGrabbed(); }
        bool hasGrabbedWidget() const { return _interactionFocus.HasGrabbed(); }
        void setGrabbedWidget(const std::shared_ptr<VRUIWidget>& widget) { _interactionFocus.SetGrabbed(widget); }
        void clearGrabbedWidget(VRUIWidget* widget);

        // Page management is delegated to VRUIContainer directly

        // --- External Input Callbacks ---
        // Call these from SkyrimVRTools button listener or KeyHandler

        /// Notify that the grip button state changed on the menu hand
        void onGripButtonChanged(bool pressed);

        /// Get current grip state for polling (e.g., grab interaction)
        bool isGripButtonDown() const { return _inputButtons.Grip(); }
        
        /// Notify that the physical right trigger state changed
        void onTriggerButtonChanged(bool pressed);
        bool isDominantTriggerButtonDown() const
        {
            return VRUISettings::get().useLeftHandAsMenu ?
                _inputButtons.Trigger() :
                _inputButtons.OffhandTrigger();
        }

        void onDominantThumbstickChanged(float x, float y)
        {
            _dominantThumbstickX = x;
            _dominantThumbstickY = y;
        }
        void getDominantThumbstick(float& x, float& y) const
        {
            x = _dominantThumbstickX;
            y = _dominantThumbstickY;
        }

        /// Notify that the physical left trigger state changed
        void onOffhandTriggerButtonChanged(bool pressed);

        /// Notify that the grip button state changed on the dominant hand
        void onDominantGripButtonChanged(bool pressed);

        /// Notify that the thumbstick button state changed on the menu hand
        /// (keyCode 0x0b = left thumbstick, 0x0c = right thumbstick from controlmapvr.txt)
        void onThumbstickButtonChanged(bool pressed);

        /// Notify that the secondary button (Y/B) state changed (keyCode 1)
        void onSecondaryButtonChanged(bool pressed);

        /// Notify that Y/B changed specifically on the dominant laser hand.
        void onDominantSecondaryButtonChanged(bool pressed);

        /// Notify that Hotkey8 (Keyboard 8 or VR mapped button) state changed
        void onHotkey8ButtonChanged(bool pressed);

        /// Get current thumbstick state for polling
        bool isThumbstickButtonDown() const { return _inputButtons.Thumbstick(); }

        /// Get current dominant grip state for polling (e.g., grab interaction)
        bool isDominantGripButtonDown() const { return _inputButtons.DominantGrip(); }

        /// Notify that the grip button state changed on the off-hand
        void onOffhandGripButtonChanged(bool pressed);

        /// Get current off-hand grip state for polling (e.g., two-hand scale)
        bool isOffhandGripButtonDown() const { return _inputButtons.OffhandGrip(); }

        /// Get HMD/Camera node for billboarding
        RE::NiNode* getHeadNode() const;

        // --- Hand node discovery ---
        RE::NiNode* getMenuHandNode() const;
        RE::NiNode* getDominantHandNode() const;
        RE::NiNode* getNonDominantHandNode() const;
        RE::NiNode* getPlayerSkeletonRoot() const;
        RE::NiNode* getPinnedAttachNode() const;

        // --- Laser Access ---
        RE::NiPoint3 getLaserOrigin() const;
        RE::NiPoint3 getLaserDirection() const;

        // --- Cooldown Tracking ---
        bool canEquip() const;
        void notifyEquip();

        /// Schedule a lightweight equipped-state update on all active dynamic containers.
        /// Call this after any equip/unequip instead of scheduleRefresh().
        void scheduleEquipRefresh(float delay = 0.15f);
        void requestSettingsSave(float delay = 0.15f);
        void saveSettingsNow();

        /// Run a console command through the central scheduler/executor.
        /// Dangerous commands are delayed on the main-thread scheduler after a fade-out.
        void executeConsoleCommand(const std::string& command, bool isDangerous, const char* logLabel = nullptr);


        // --- Haptic feedback ---
        void triggerHaptic(bool isDominantHand, float intensity, float duration);

        // Clears current hovered widget to avoid dangling pointer when children get deleted
        void clearHover();

        /// Get if VRIK is installed
        bool isVRIKInstalled() const { return _isVRIKInstalled; }

    private:
        friend class dragonboard::ui::widgets::FixedWidgetPresenter;
        friend class dragonboard::ui::menu::MenuLifecycleController;
        friend class dragonboard::ui::menu::MenuInitializationController;
        friend class dragonboard::ui::input::InteractionInputController;
        friend class dragonboard::ui::input::PointerInteractionController;
        friend class dragonboard::ui::frame::FrameUpdateController;
        friend class dragonboard::ui::pointer::PointerVisualController;
        friend class dragonboard::ui::refresh::RefreshPipelineController;
        friend class dragonboard::ui::runtime::DeferredActionController;
        friend class dragonboard::ui::panels::PanelManagementController;
        friend class dragonboard::ui::equipment::EquipInteractionController;

        VRMenuManager() = default;

        // --- Internal Panel Data ---
        RE::NiPoint3 getPanelOffset() const;

        // --- Input processing ---
        RE::NiNode* resolvePinnedAttachNode(RE::NiNode* skeletonRoot) const;

        // --- State ---
        // VRIK compatibility flag
        bool _isVRIKInstalled = false;

        bool _initialized = false;
        dragonboard::ui::menu::MenuSessionState _menuSession;

        // --- Input State ---
        dragonboard::ui::input::InputButtonState _inputButtons;
        float _dominantThumbstickX = 0.0f;
        float _dominantThumbstickY = 0.0f;
        
        // --- Current interaction ---
        static constexpr float kHoverLockTime = 0.16f; // 160ms minimum hover duration
        dragonboard::ui::panels::BoardPinState _boardPinState;

        dragonboard::ui::pointer::PointerVisualState _pointerVisual;

        dragonboard::core::CooldownTimer _menuToggleCooldown;
        dragonboard::core::CooldownTimer _equipCooldown;
        dragonboard::ui::refresh::EquipRefreshScheduler _equipRefreshScheduler;
        dragonboard::ui::refresh::RefreshCoordinator _refreshCoordinator;
        dragonboard::ui::settings::SettingsSaveScheduler _settingsSaveScheduler;
        dragonboard::ui::settings::IniChangeWatcher _iniChangeWatcher;
        dragonboard::ui::input::ActivationHoldTracker _activationHoldTracker;
        dragonboard::ui::input::PressHoldTracker _dominantTriggerTracker;
        dragonboard::ui::input::PressHoldTracker _offhandTriggerTracker;
        dragonboard::ui::input::PressHoldTracker _secondaryButtonTracker;
        dragonboard::ui::input::PressHoldTracker _dominantSecondaryButtonTracker;
        dragonboard::ui::input::InteractionFocusState _interactionFocus;

        dragonboard::core::DeferredTaskQueue _deferredTasks;

        // --- Components ---
        dragonboard::ui::panels::PanelRegistry _panelRegistry;

        dragonboard::ui::navigation::PageNavigationState _pageNavigation;


    };
}
