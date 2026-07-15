#include "bootstrap/PluginLifecycle.h"

#include "bootstrap/HotkeyResolver.h"
#include "higgsinterface001.h"
#include "keyhandler/keyhandler.h"
#include "plugin.h"
#include "papyrus/PapyrusPanelBridge.h"
#include "ui/rml/RmlPanelHost.h"
#include "vrui/JournalMenuProbe.h"
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

        case SKSE::MessagingInterface::kDataLoaded: {
            logger::trace("DragonBoardVR: ===== kDataLoaded =====");
            ModActionManager::get().initialize();

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
            // kDataLoaded runs after the renderer is normally available at the
            // main menu, but before the player skeleton and DragonBoard 3D
            // items are initialized. Only arm a Present-thread RmlUi warm-up;
            // no scene graph nodes are created here.
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            break;
        }

        case SKSE::MessagingInterface::kPreLoadGame:
            JournalMenuProbe::GetSingleton().Reset();
            dragonboard::papyrus::ResetPapyrusPanels();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            logger::trace("DragonBoardVR: ===== kPostLoadGame =====");
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            VRFrameUpdater::Register();
            break;

        case SKSE::MessagingInterface::kNewGame:
            logger::trace("DragonBoardVR: ===== kNewGame =====");
            JournalMenuProbe::GetSingleton().Reset();
            dragonboard::papyrus::ResetPapyrusPanels();
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            VRFrameUpdater::Register();
            break;
        }
    }
}
