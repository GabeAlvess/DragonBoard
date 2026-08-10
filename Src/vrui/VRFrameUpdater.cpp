#include "pch.h"
#include "bootstrap/PluginLifecycle.h"
#include "VRFrameUpdater.h"
#include "VRMenuManager.h"
#include "VRUISettings.h"
#include "VRUIButton.h"
#include "ModActionManager.h"
#include <RE/B/BSInputDeviceManager.h>
#include <RE/U/UserEvents.h>
#include <RE/C/ControlMap.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/U/UI.h>
#include <RE/L/LoadingMenu.h>
#include <RE/F/FaderMenu.h>
#include <RE/B/ButtonEvent.h>
#include <RE/T/ThumbstickEvent.h>
#include <SKSE/SKSE.h>
#include <atomic>
#include <windows.h> // for GetModuleHandleA
#include "integrations/higgs/PhysicalBoardController.h"
#include "integrations/vrik/VrikBoardProxyController.h"
#include "ui/menu/MenuComposition.h"
#include "ui/rml/RmlPanelHost.h"

namespace vrui
{
    VRFrameUpdater* VRFrameUpdater::GetSingleton()
    {
        static VRFrameUpdater instance;
        return &instance;
    }

    void VRFrameUpdater::Register()
    {
        static bool registered = false;
        if (registered) return;

        auto* inputMgr = RE::BSInputDeviceManager::GetSingleton();
        if (inputMgr) {
            inputMgr->PrependEventSink(GetSingleton());
            registered = true;
            logger::info("DragonBoardVR: VRFrameUpdater input sink registered.");
        } else {
            logger::error("DragonBoardVR: Failed to get BSInputDeviceManager!");
        }
    }

    VRFrameUpdater::VRFrameUpdater() : _lastTime(std::chrono::high_resolution_clock::now()) {}

    RE::BSEventNotifyControl VRFrameUpdater::ProcessEvent(
        RE::InputEvent* const* a_eventList,
        [[maybe_unused]] RE::BSTEventSource<RE::InputEvent*>* a_eventSource)
    {
        // --- Calculate frame delta ---
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - _lastTime).count();
        _lastTime = now;
        if (deltaTime <= 0.0f || deltaTime > 0.5f) deltaTime = 0.016f;

        // --- Drive the VR menu system each frame safely ---
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            static std::atomic<bool> taskQueued{false};
            if (!taskQueued.exchange(true)) {
                taskInterface->AddTask([deltaTime]() {
                    taskQueued.store(false);
                    static bool firstSafeFrameRan = false;
                    static int framesToWait = 30; // ~0.5s at 60fps — enough for 3D/skeleton to settle
                    
                    if (!firstSafeFrameRan) {
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        auto* ui = RE::UI::GetSingleton();
                        
                        // Check VRIK presence once for this check
                        static bool vrikOn = GetModuleHandleA("vrik.dll") != nullptr;
                        
                        // Check if player is NOT in loading screen and 3D is ready
                        bool isLoading = ui ? (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::FaderMenu::MENU_NAME)) : true;
                        
                        // We also check if player has a parent cell and is not in a 'middle' state
                        if (player && player->GetParentCell() && player->Is3DLoaded() && player->Get3D(!vrikOn) && !isLoading) {
                            if (framesToWait > 0) {
                                framesToWait--;
                                return;
                            }
                            
                            logger::trace("DragonBoardVR: Stability confirmed (VRIK={}). Executing delayed initialization.", vrikOn);
                            VRMenuManager::get().initialize();
                            if (!dragonboard::ui::menu::IsCreated()) {
                                dragonboard::ui::menu::Create();
                            }
                            // kPostLoadGame can arrive before MenuInitializationController
                            // loads DragonBoardVR.ini. Re-arm persisted quest markers only
                            // after settings and the three marker widgets are ready.
                            dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                                .RequestQuestMarkerRestore();
                            firstSafeFrameRan = true;
                        } else {
                            // Reset wait counter if we fall back into loading state
                            if (isLoading) framesToWait = 30;
                            return;
                        }
                    }

                    VRMenuManager::get().onFrameUpdate(deltaTime);
                    dragonboard::bootstrap::UpdateGameThread();
                    dragonboard::ui::rml::RmlPanelHost::GetSingleton().UpdateGameThread(deltaTime);
                    ModActionManager::get().update(deltaTime);
                });
            }
        }

        // --- Process VR controller events ---
        bool stopInputPropagation = false;
        if (a_eventList) {
            for (auto* event = *a_eventList; event; event = event->next) {
                if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                    auto* thumbEvent = event->AsThumbstickEvent();
                    if (thumbEvent) {
                        const auto device = thumbEvent->GetDevice();
                        const bool isLeftHand =
                            device == RE::INPUT_DEVICE::kViveSecondary ||
                            device == RE::INPUT_DEVICE::kOculusSecondary ||
                            device == RE::INPUT_DEVICE::kWMRSecondary;
                        const bool isRightHand =
                            device == RE::INPUT_DEVICE::kVivePrimary ||
                            device == RE::INPUT_DEVICE::kOculusPrimary ||
                            device == RE::INPUT_DEVICE::kWMRPrimary;
                        auto& menuManager = VRMenuManager::get();
                        const bool menuOnLeft = menuManager.isMenuHandLeft();
                        const bool isDominant = menuOnLeft ? isRightHand : isLeftHand;
                        if (isDominant) {
                            menuManager.onDominantThumbstickChanged(
                                thumbEvent->xValue, thumbEvent->yValue);
                        }

                        const auto grabbedWidget = menuManager.getGrabbedWidget();
                        const auto* grabbedButton = grabbedWidget ?
                            dynamic_cast<VRUIButton*>(grabbedWidget.get()) : nullptr;
                        const bool grabbedWidgetOwnsVerticalAxis =
                            grabbedButton &&
                            grabbedButton->isGrabbed() &&
                            grabbedButton->isGrabHandLeft() == isLeftHand;
                        const bool grabbedSurfaceOwnsVerticalAxis =
                            dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                                .IsGripThumbScaleInputCaptured(isLeftHand);
                        if (grabbedWidgetOwnsVerticalAxis ||
                            grabbedSurfaceOwnsVerticalAxis) {
                            thumbEvent->yValue = 0.0f;
                        }
                    }
                    continue;
                }
                if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) continue;

                auto* btnEvent = event->AsButtonEvent();
                if (!btnEvent) continue;

                auto device = btnEvent->GetDevice();
                uint32_t keyCode = btnEvent->GetIDCode();

                // Identify VR controllers
                bool isVRController = false;
                bool isLeftHand = false;
                bool isRightHand = false;

#ifdef ENABLE_SKYRIM_VR
                switch (device) {
                case RE::INPUT_DEVICE::kVivePrimary:
                case RE::INPUT_DEVICE::kOculusPrimary:
                case RE::INPUT_DEVICE::kWMRPrimary:
                    isVRController = true;
                    isRightHand = true; // Primary is Right Hand
                    break;
                case RE::INPUT_DEVICE::kViveSecondary:
                case RE::INPUT_DEVICE::kOculusSecondary:
                case RE::INPUT_DEVICE::kWMRSecondary:
                    isVRController = true;
                    isLeftHand = true; // Secondary is Left Hand
                    break;
                default:
                    break;
                }
#endif

                if (isVRController) {
                    auto* controlMap = RE::ControlMap::GetSingleton();
                    auto* userEvents = RE::UserEvents::GetSingleton();
                    auto& manager = VRMenuManager::get();
                    const bool menuOnLeft = manager.isMenuHandLeft();

                    bool isMenuHand = menuOnLeft ? isLeftHand : isRightHand;
                    bool isDominantHand = menuOnLeft ? isRightHand : isLeftHand;

                    // 1. Triggers are always fixed: Left Trigger -> Left Equip, Right Trigger -> Right Equip
                    uint32_t triggerKey = 33;
                    if (controlMap && userEvents) {
                        const RE::BSFixedString& attackEvent = isLeftHand ? userEvents->leftAttack : userEvents->rightAttack;
                        uint32_t mappedTrigger = controlMap->GetMappedKey(attackEvent, device);
                        if (mappedTrigger != RE::ControlMap::kInvalid) triggerKey = mappedTrigger;
                    }
                    const bool isTriggerButton = keyCode == triggerKey || keyCode == 33;
                    if (isTriggerButton) {
                        if (isLeftHand) {
                            VRMenuManager::get().onOffhandTriggerButtonChanged(btnEvent->IsPressed()); // Left Hand
                        } else {
                            VRMenuManager::get().onTriggerButtonChanged(btnEvent->IsPressed()); // Right Hand
                        }
                    }

                    // Thumbstick press: keyCode 0x0b (left) or 0x0c (right)
                    // Used for ActivationMode::Thumbstick and ActivationMode::GripPlusThumbstick
                    bool isThumbstick = (keyCode == 0x0b || keyCode == 0x0c);
                    if (isThumbstick) {
                        if (isMenuHand) VRMenuManager::get().onThumbstickButtonChanged(btnEvent->IsPressed());
                    }

                    // Y/B Button: keyCode 1 (Oculus)
                    if (keyCode == 1) {
                        VRMenuManager::get().onSecondaryButtonChanged(btnEvent->IsPressed());
                        if (isDominantHand) {
                            VRMenuManager::get().onDominantSecondaryButtonChanged(
                                btnEvent->IsPressed());
                        }
                    }

                    // Grips
                    uint32_t gripKey = 2; // Default for non-VR or fallback
#ifdef ENABLE_SKYRIM_VR
                    gripKey = (device == RE::INPUT_DEVICE::kViveSecondary || device == RE::INPUT_DEVICE::kOculusSecondary || device == RE::INPUT_DEVICE::kWMRSecondary) ? 7 : 2;
#endif
                    if (controlMap && userEvents) {
                        uint32_t mappedGrip = controlMap->GetMappedKey(userEvents->shout, device);
                        if (mappedGrip != RE::ControlMap::kInvalid) gripKey = mappedGrip;
                        
                        // Dynamic Hotkey 8 support for VR devices (always active)
                        uint32_t hotkey8Mapped = controlMap->GetMappedKey("Hotkey8", device);
                        if (keyCode == hotkey8Mapped) {
                            VRMenuManager::get().onHotkey8ButtonChanged(btnEvent->IsPressed());
                        }
                    }
                    const bool isGripButton = keyCode == gripKey || keyCode == 2 || keyCode == 7;
                    auto& rmlHost =
                        dragonboard::ui::rml::RmlPanelHost::GetSingleton();
                    const bool rmlOwnsGrip =
                        isDominantHand &&
                        isGripButton &&
                        manager.isPhysicalBoardActive() &&
                        rmlHost.ShouldCaptureGripInput();
                    if (isGripButton) {
                        dragonboard::integrations::vrik::VrikBoardProxyController::
                            GetSingleton().NotifyGripInput(
                                isLeftHand, btnEvent->IsPressed());
                        if (isMenuHand) VRMenuManager::get().onGripButtonChanged(btnEvent->IsPressed());
                        if (isDominantHand) VRMenuManager::get().onDominantGripButtonChanged(btnEvent->IsPressed());
                        if (!isDominantHand) VRMenuManager::get().onOffhandGripButtonChanged(btnEvent->IsPressed());
                    }

                    if (isTriggerButton || (isDominantHand && isGripButton)) {
                        rmlHost.OnVrButtonEvent(
                            isLeftHand,
                            isTriggerButton,
                            isGripButton,
                            btnEvent->IsPressed());
                    }
                    if (rmlOwnsGrip) {
                        stopInputPropagation = true;
                        if (btnEvent->IsPressed()) {
                            logger::trace(
                                "DragonBoardVR: RML panel captured physical-board grip before VRIK holster processing.");
                        }
                    }
                }
            }
        }

        // Log first frame
        static bool firstFrame = true;
        if (firstFrame) {
            firstFrame = false;
            logger::trace("DragonBoardVR: First frame - update loop ACTIVE!");
        }

        return stopInputPropagation ?
            RE::BSEventNotifyControl::kStop :
            RE::BSEventNotifyControl::kContinue;
    }
}
