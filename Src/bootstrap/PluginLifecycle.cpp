#include "bootstrap/PluginLifecycle.h"

#include "bootstrap/HotkeyResolver.h"
#include "gameplay/CombatSlowTime.h"
#include "higgsinterface001.h"
#include "integrations/higgs/PhysicalBoardController.h"
#include "integrations/spellwheel/SpellWheelIntegration.h"
#include "integrations/vrik/VrikBoardProxyController.h"
#include "integrations/vrik/VrikFingerPose.h"
#include "keyhandler/keyhandler.h"
#include "plugin.h"
#include "papyrus/PapyrusPanelBridge.h"
#include "runtime/vr/ReferencePlacement.h"
#include "ui/rml/RmlPanelHost.h"
#include "vrui/ModActionManager.h"
#include "vrui/ModEventHandler.h"
#include "vrui/VRFrameUpdater.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>

namespace dragonboard::bootstrap
{
    namespace
    {
        constexpr std::string_view kRealmOfLorkhanPlugin =
            "Realm of Lorkhan - Custom Alternate Start - Choose your own adventure.esp";
        constexpr RE::FormID kRealmWelcomeNoteLocalFormID = 0x0252C1;
        constexpr RE::FormID kRealmWelcomeNoteBaseLocalFormID = 0x0252C0;
        constexpr float kRealmBoardOffset = 45.0f;
        constexpr float kRealmBoardHeight = 10.0f;
        constexpr float kHalfPi = 1.57079632679f;
        constexpr std::uint32_t kSerializationUniqueID = 0x52564244;
        constexpr std::uint32_t kRealmSpawnRecordType = 0x50534C52;
        constexpr std::uint32_t kRealmSpawnRecordVersion = 1;
        constexpr std::uint32_t kRealmSpawnRetryFrames = 60;

        void SkipSerializationRecord(
            SKSE::SerializationInterface* serialization,
            std::uint32_t length)
        {
            std::array<std::uint8_t, 256> buffer{};
            while (length > 0) {
                const auto chunk = (std::min)(
                    length,
                    static_cast<std::uint32_t>(buffer.size()));
                const auto bytesRead = serialization->ReadRecordData(
                    buffer.data(),
                    chunk);
                if (bytesRead == 0) {
                    return;
                }
                length -= bytesRead;
            }
        }

        class RealmOfLorkhanCompatibility final
        {
        public:
            static RealmOfLorkhanCompatibility& GetSingleton()
            {
                static RealmOfLorkhanCompatibility instance;
                return instance;
            }

            void Register()
            {
                if (_registered) {
                    return;
                }

                auto* dataHandler = RE::TESDataHandler::GetSingleton();
                if (!dataHandler ||
                    !dataHandler->LookupLoadedModByName(kRealmOfLorkhanPlugin)) {
                    return;
                }

                _registered = true;
                _pending = !_spawned;
                logger::info(
                    "DragonBoardVR: native Realm of Lorkhan compatibility enabled.");
            }

            void ArmIfNeeded()
            {
                if (!_registered || _spawned) {
                    return;
                }

                _pending = true;
                _retryFrames = 0;
            }

            void UpdateGameThread()
            {
                if (!_pending) {
                    return;
                }

                if (_retryFrames > 0) {
                    --_retryFrames;
                    return;
                }

                _retryFrames = kRealmSpawnRetryFrames;
                TrySpawn();
            }

            void Save(SKSE::SerializationInterface* serialization) const
            {
                const std::uint8_t spawned = _spawned ? 1 : 0;
                if (!serialization->WriteRecord(
                        kRealmSpawnRecordType,
                        kRealmSpawnRecordVersion,
                        spawned)) {
                    logger::error(
                        "DragonBoardVR: failed to save Realm of Lorkhan starter state.");
                }
            }

            void Load(SKSE::SerializationInterface* serialization)
            {
                _spawned = false;
                std::uint32_t type = 0;
                std::uint32_t version = 0;
                std::uint32_t length = 0;
                while (serialization->GetNextRecordInfo(type, version, length)) {
                    if (type == kRealmSpawnRecordType &&
                        version == kRealmSpawnRecordVersion &&
                        length == sizeof(std::uint8_t)) {
                        std::uint8_t spawned = 0;
                        if (serialization->ReadRecordData(spawned) == sizeof(spawned)) {
                            _spawned = spawned != 0;
                        }
                    } else {
                        SkipSerializationRecord(serialization, length);
                    }
                }

                _pending = _registered && !_spawned;
                _retryFrames = 0;
                logger::info(
                    "DragonBoardVR: Realm of Lorkhan starter state loaded (spawned={}).",
                    _spawned);
            }

            void Revert()
            {
                _spawned = false;
                _pending = _registered;
                _retryFrames = 0;
            }

        private:
            void TrySpawn()
            {
                if (!_pending) {
                    return;
                }

                auto* dataHandler = RE::TESDataHandler::GetSingleton();
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!dataHandler || !player || !player->GetParentCell()) {
                    return;
                }

                auto* welcomeNote = dataHandler->LookupForm<RE::TESObjectREFR>(
                    kRealmWelcomeNoteLocalFormID,
                    kRealmOfLorkhanPlugin);
                auto* welcomeNoteBase = dataHandler->LookupForm<RE::TESObjectBOOK>(
                    kRealmWelcomeNoteBaseLocalFormID,
                    kRealmOfLorkhanPlugin);
                if (!welcomeNote || !welcomeNoteBase ||
                    welcomeNote->GetBaseObject() != welcomeNoteBase) {
                    logger::warn(
                        "DragonBoardVR: Realm of Lorkhan Welcome Note anchor was not found.");
                    _pending = false;
                    return;
                }

                if (player->GetParentCell() != welcomeNote->GetParentCell()) {
                    return;
                }

                const auto& settings = vrui::VRUISettings::get();
                if (!settings.physicalBoardEnabled) {
                    _pending = false;
                    return;
                }

                auto* physicalBoard = dataHandler->LookupForm<RE::TESObjectMISC>(
                    settings.physicalBoardLocalFormID,
                    settings.physicalBoardPlugin);
                if (!physicalBoard) {
                    logger::warn(
                        "DragonBoardVR: physical board form unavailable for Realm of Lorkhan.");
                    _pending = false;
                    return;
                }

                auto reference = player->PlaceObjectAtMe(physicalBoard, false);
                if (!reference) {
                    logger::warn(
                        "DragonBoardVR: failed to create the Realm of Lorkhan starter board.");
                    return;
                }

                const auto notePosition = welcomeNote->GetPosition();
                const auto noteAngle = welcomeNote->GetAngle();
                const RE::NiPoint3 boardPosition{
                    notePosition.x + std::cos(noteAngle.z) * kRealmBoardOffset,
                    notePosition.y + std::sin(noteAngle.z) * kRealmBoardOffset,
                    notePosition.z + kRealmBoardHeight
                };
                const RE::NiPoint3 boardAngle{ kHalfPi, 0.0f, noteAngle.z };
                if (!dragonboard::runtime::vr::SetReferenceTransform(
                        reference.get(),
                        boardPosition,
                        boardAngle)) {
                    logger::warn(
                        "DragonBoardVR: failed to position the Realm of Lorkhan starter board.");
                }
                dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton().
                    PrepareSpawnedReference(reference.get());

                _pending = false;
                _spawned = true;
                logger::info(
                    "DragonBoardVR: placed the Realm of Lorkhan starter board beside the Welcome Note.");
            }

            bool _registered{ false };
            bool _pending{ false };
            bool _spawned{ false };
            std::uint32_t _retryFrames{ 0 };
        };

        void SaveRealmOfLorkhanState(SKSE::SerializationInterface* serialization)
        {
            RealmOfLorkhanCompatibility::GetSingleton().Save(serialization);
        }

        void LoadRealmOfLorkhanState(SKSE::SerializationInterface* serialization)
        {
            RealmOfLorkhanCompatibility::GetSingleton().Load(serialization);
        }

        void RevertRealmOfLorkhanState(SKSE::SerializationInterface*)
        {
            RealmOfLorkhanCompatibility::GetSingleton().Revert();
        }
    }

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

    void LoadInitialSettings()
    {
        auto& settings = vrui::VRUISettings::get();
        const auto iniPath = vrui::VRUISettings::getDefaultIniPath();
        settings.load(iniPath);
        logger::info(
            "DragonBoardVR: Initial settings loaded from '{}' (language='{}').",
            iniPath,
            settings.uiLanguage);
    }

    bool InitializeSerialization()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            logger::error(
                "DragonBoardVR: SKSE serialization interface unavailable.");
            return false;
        }

        serialization->SetUniqueID(kSerializationUniqueID);
        serialization->SetSaveCallback(SaveRealmOfLorkhanState);
        serialization->SetLoadCallback(LoadRealmOfLorkhanState);
        serialization->SetRevertCallback(RevertRealmOfLorkhanState);
        return true;
    }

    void UpdateGameThread()
    {
        RealmOfLorkhanCompatibility::GetSingleton().UpdateGameThread();
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
                dragonboard::integrations::vrik::InitializeHiggsHandCollisionSuppression();
                dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton().Initialize();
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
            dragonboard::integrations::vrik::VrikBoardProxyController::GetSingleton().Initialize();
            dragonboard::integrations::spellwheel::RegisterPlayerEventSink();
            dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton().RefreshConfiguredForm();
            dragonboard::integrations::vrik::VrikBoardProxyController::GetSingleton().RefreshConfiguredForms();
            RealmOfLorkhanCompatibility::GetSingleton().Register();
            RealmOfLorkhanCompatibility::GetSingleton().ArmIfNeeded();

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
            dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton().Reset();
            dragonboard::integrations::vrik::VrikBoardProxyController::GetSingleton().Reset();
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
            RealmOfLorkhanCompatibility::GetSingleton().ArmIfNeeded();
            break;

        case SKSE::MessagingInterface::kNewGame:
            logger::trace("DragonBoardVR: ===== kNewGame =====");
            RealmOfLorkhanCompatibility::GetSingleton().Revert();
            dragonboard::integrations::spellwheel::RegisterPlayerEventSink();
            dragonboard::papyrus::ResetPapyrusPanels();
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestRmlWarmup();
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().RequestQuestMarkerRestore();
            VRFrameUpdater::Register();
            RealmOfLorkhanCompatibility::GetSingleton().ArmIfNeeded();
            break;
        }
    }
}
