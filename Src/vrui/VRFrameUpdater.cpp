#include "pch.h"
#include "VRFrameUpdater.h"
#include "VRMenuManager.h"
#include "VRUISettings.h"
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
            inputMgr->AddEventSink(GetSingleton());
            registered = true;
            logger::trace("DragonBoardVR: VRFrameUpdater input sink registered!");
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
                            firstSafeFrameRan = true;
                        } else {
                            // Reset wait counter if we fall back into loading state
                            if (isLoading) framesToWait = 30;
                            return;
                        }
                    }

                    VRMenuManager::get().onFrameUpdate(deltaTime);
                    dragonboard::ui::rml::RmlPanelHost::GetSingleton().UpdateGameThread(deltaTime);
                    ModActionManager::get().update(deltaTime);
                });
            }
        }

        // --- Process VR controller events ---
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
                        const bool menuOnLeft = VRUISettings::get().useLeftHandAsMenu;
                        const bool isDominant = menuOnLeft ? isRightHand : isLeftHand;
                        if (isDominant) {
                            VRMenuManager::get().onDominantThumbstickChanged(
                                thumbEvent->xValue, thumbEvent->yValue);
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
                    auto& settings = VRUISettings::get();

                    bool isMenuHand = settings.useLeftHandAsMenu ? isLeftHand : isRightHand;
                    bool isDominantHand = settings.useLeftHandAsMenu ? isRightHand : isLeftHand;

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
                    if (isGripButton) {
                        if (isMenuHand) VRMenuManager::get().onGripButtonChanged(btnEvent->IsPressed());
                        if (isDominantHand) VRMenuManager::get().onDominantGripButtonChanged(btnEvent->IsPressed());
                        if (!isDominantHand) VRMenuManager::get().onOffhandGripButtonChanged(btnEvent->IsPressed());
                    }

                    if (isDominantHand && (isTriggerButton || isGripButton)) {
                        dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                            .OnDominantVrButtonEvent(
                                isTriggerButton,
                                isGripButton,
                                btnEvent->IsPressed());
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

        return RE::BSEventNotifyControl::kContinue;
    }
}
