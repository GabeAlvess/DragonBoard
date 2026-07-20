#include "bootstrap/PluginLifecycle.h"

#include "bootstrap/HotkeyResolver.h"
#include "gameplay/CombatSlowTime.h"
#include "higgsinterface001.h"
#include "integrations/spellwheel/SpellWheelIntegration.h"
#include "integrations/vrik/VrikFingerPose.h"
#include "keyhandler/keyhandler.h"
#include "plugin.h"
#include "papyrus/PapyrusPanelBridge.h"
#include "ui/rml/RmlPanelHost.h"
#include "vrui/ModActionManager.h"
#include "vrui/ModEventHandler.h"
#include "vrui/VRFrameUpdater.h"
#include "vrui/VRMenuManager.h"

#include <filesystem>
#include <memory>

namespace dragonboard::bootstrap
{
    void InitializeLogging()
    {
        const auto logsFolder = SKSE::log::log_directory();
        if (!logsFolder) {
            SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
            return;
        }

        auto logPath = *logsFolder / std::filesystem::path(Plugin::NAME) += ".log";
        auto fileLogger = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto pluginLogger = std::make_shared<spdlog::logger>("log", std::move(fileLogger));

        spdlog::set_default_logger(std::move(pluginLogger));
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }

    void HandleSKSEMessage(SKSE::MessagingInterface::Message* message)
    {
        using namespace vrui;

        switch (message->type) {
        case SKSE::MessagingInterface::kPostPostLoad:
            logger::trace("DragonBoardVR: ===== kPostPostLoad =====");
            dragonboard::integrations::spellwheel::Initialize();
            dragonboard::integrations::vrik::Initialize();
            g_higgsInterface = HiggsPluginAPI::GetHiggsInterface001(
                SKSE::GetPluginHandle(), SKSE::GetMessagingInterface());
            if (g_higgsInterface) {
                logger::info(
                    "DragonBoardVR: HIGGS interface obtained successfully. Build: {}",
                    g_higgsInterface->GetBuildNumber());
            } else {
                logger::warn("DragonBoardVR: HIGGS interface not found. Direct grabbing will be disabled.");
            }
            break;

        case SKSE::MessagingInterface::kInputLoaded:
            // Skyrim VR does not reliably dispatch kNewGame before controller
            // input is needed in a fresh game. Register as soon as the input
            // manager is ready; Register() is idempotent, so the load/new-game
            // handlers below remain safe fallbacks.
            logger::info("DragonBoardVR: SKSE input loaded; registering VR input sink.");
            VRFrameUpdater::Register();
            // Start the staged RmlUi warm-up while Skyrim is still loading.
            // RequestRmlWarmup is idempotent and kDataLoaded remains a retry
            // point if the swap chain is not available yet.
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            break;

        case SKSE::MessagingInterface::kDataLoaded: {
            logger::trace("DragonBoardVR: ===== kDataLoaded =====");
            ModActionManager::get().initialize();
            dragonboard::integrations::spellwheel::RegisterPlayerEventSink();

            if (auto* modEventSource = SKSE::GetModCallbackEventSource()) {
                modEventSource->AddEventSink(ModCallbackEventHandler::GetSingleton());
                logger::trace("DragonBoardVR: Registered ModCallbackEventHandler.");
            }

            KeyHandler::RegisterSink();
            auto* keyHandler = KeyHandler::GetSingleton();

            (void)keyHandler->Register(0x22, KeyEventType::KEY_DOWN, []() {
                VRMenuManager::get().onGripButtonChanged(true);
            });
            (void)keyHandler->Register(0x22, KeyEventType::KEY_UP, []() {
                VRMenuManager::get().onGripButtonChanged(false);
            });

            const auto hotkey8Keys = ResolveHotkey8KeyboardKeys();
            if (!hotkey8Keys.empty()) {
                for (const auto key : hotkey8Keys) {
                    (void)keyHandler->Register(key, KeyEventType::KEY_DOWN, []() {
                        VRMenuManager::get().onHotkey8ButtonChanged(true);
                    });
                    (void)keyHandler->Register(key, KeyEventType::KEY_UP, []() {
                        VRMenuManager::get().onHotkey8ButtonChanged(false);
                    });
                    logger::trace("DragonBoardVR: Hotkey8 keyboard mapping registered (0x{:X})", key);
                }
            } else {
                logger::warn("DragonBoardVR: Could not resolve Hotkey8 from ControlMap.");
            }

            logger::trace("DragonBoardVR: Keys registered (G=grip, ControlMap=Hotkey8)");
            // Retry the staged Present-thread warm-up once the renderer is
            // normally available. The first request is made at kInputLoaded so
            // document work can be spread across frames during loading.
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            break;
        }

        case SKSE::MessagingInterface::kPreLoadGame:
            dragonboard::integrations::vrik::RestoreTouchHandPose();
            dragonboard::gameplay::CombatSlowTime::GetSingleton().Close();
            dragonboard::papyrus::ResetPapyrusPanels();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            logger::trace("DragonBoardVR: ===== kPostLoadGame =====");
            dragonboard::integrations::spellwheel::RegisterPlayerEventSink();
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestQuestMarkerRestore();
            VRFrameUpdater::Register();
            break;

        case SKSE::MessagingInterface::kNewGame:
            logger::trace("DragonBoardVR: ===== kNewGame =====");
            dragonboard::integrations::spellwheel::RegisterPlayerEventSink();
            dragonboard::papyrus::ResetPapyrusPanels();
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestQuestMarkerRestore();
            VRFrameUpdater::Register();
            break;
        }
    }
}
