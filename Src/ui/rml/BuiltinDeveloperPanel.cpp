#include "ui/rml/RmlPanelHost.h"
#include "ui/rml/DragonBoardRmlUi.h"
#include "game/actions/ActionExecutor.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr const char* kDevCommandsIniPath =
            "Data/SKSE/Plugins/DragonBoardVR_DevCommands.ini";

        std::string TrimDevCommandText(std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        std::string MakeDevCommandLabel(std::string_view command)
        {
            std::string label(command);
            for (auto& character : label) {
                if (character == '=' || character == '|' ||
                    character == '\r' || character == '\n') {
                    character = ' ';
                }
            }
            label = TrimDevCommandText(std::move(label));
            return label.empty() ? "Custom command" : label;
        }

        bool AppendDevCommandToIni(
            std::string_view label,
            std::string_view command)
        {
            const std::filesystem::path iniPath = kDevCommandsIniPath;
            std::error_code error;
            if (const auto parent = iniPath.parent_path(); !parent.empty()) {
                std::filesystem::create_directories(parent, error);
                if (error) {
                    logger::error(
                        "DragonBoardVR: could not create the Dev commands directory '{}': {}.",
                        parent.string(),
                        error.message());
                    return false;
                }
            }

            const bool writeHeader =
                !std::filesystem::exists(iniPath, error) ||
                (!error && std::filesystem::file_size(iniPath, error) == 0);
            std::ofstream file(iniPath, std::ios::app);
            if (!file.is_open()) {
                logger::error(
                    "DragonBoardVR: could not open '{}' to save a Dev command.",
                    iniPath.string());
                return false;
            }

            if (writeHeader) {
                file << "[DevCommands]\n";
                file << "; Format: Label|Description = ConsoleCommand\n";
            } else {
                file << '\n';
            }
            file << label
                 << "|Custom command added from the DragonBoard developer panel. = "
                 << command << '\n';
            file.flush();
            return file.good();
        }
    }

    void RmlPanelHost::QueueDevCommand(const DevCommandEntry& entry)
    {
        {
            std::scoped_lock lock(_devMutex);
            _pendingDevCommand = entry.command;
            _pendingDevCommandLabel = entry.label;
            _pendingDevCommandDangerous = entry.dangerous;
        }
        _devCommandPending.store(true);
    }

    void RmlPanelHost::LoadDevCommandsGameThread()
    {
        std::vector<DevCommandEntry> commands{
            { "TGM", "tgm", "Toggle god mode for the player.", false },
            { "Immortal", "tim", "Toggle immortal mode. Health may reach zero, but the player does not die.", false },
            { "All map", "tmm 1", "Reveal all map markers.", true },
            { "Kill", "kill", "Kill the actor currently targeted by the console.", true },
            { "Gold", "player.additem f 1000", "Add 1000 gold to the player inventory.", false },
            { "Toggle Menu", "tm", "Toggle Skyrim user-interface visibility.", false },
            { "Unlock", "unlock", "Unlock the object currently targeted by the console.", true },
            { "RaceMenu", "showracemenu", "Open the character creation and race menu.", true }
        };

        const std::filesystem::path iniPath = kDevCommandsIniPath;
        std::ifstream file(iniPath);
        if (file.is_open()) {
            std::vector<DevCommandEntry> configured;
            std::string line;
            const auto trim = [](std::string value) {
                const auto first = value.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return std::string{};
                const auto last = value.find_last_not_of(" \t\r\n");
                return value.substr(first, last - first + 1);
            };
            while (std::getline(file, line)) {
                const auto stripped = trim(line);
                if (stripped.empty() || stripped.front() == ';' || stripped.front() == '[') continue;
                const auto separator = stripped.find('=');
                if (separator == std::string::npos) continue;
                auto label = trim(stripped.substr(0, separator));
                auto command = trim(stripped.substr(separator + 1));
                if (label.empty() || command.empty()) continue;

                std::string description = "Custom command loaded from DragonBoardVR_DevCommands.ini.";
                if (const auto known = std::find_if(commands.begin(), commands.end(), [&](const auto& entry) {
                        return entry.command == command;
                    }); known != commands.end()) {
                    description = known->description;
                }
                if (const auto descriptionSeparator = label.find('|'); descriptionSeparator != std::string::npos) {
                    description = trim(label.substr(descriptionSeparator + 1));
                    label = trim(label.substr(0, descriptionSeparator));
                }
                const bool dangerous = dragonboard::game::actions::IsDangerousConsoleCommand(command);
                configured.push_back({
                    std::move(label), std::move(command), std::move(description), dangerous });
            }
            if (!configured.empty()) commands = std::move(configured);
        }

        std::scoped_lock lock(_devMutex);
        _devCommands = std::move(commands);
        _selectedDevCommand = _devCommands.empty() ? -1 :
            std::clamp(_selectedDevCommand, 0, static_cast<int>(_devCommands.size() - 1));
    }

    void RmlPanelHost::AddDevCommandGameThread(std::string command)
    {
        command = TrimDevCommandText(std::move(command));
        if (command.empty()) return;
        for (auto& character : command) {
            if (character == '\r' || character == '\n') character = ' ';
        }

        DevCommandEntry entry{
            MakeDevCommandLabel(command),
            command,
            "Custom command added from the DragonBoard developer panel.",
            dragonboard::game::actions::IsDangerousConsoleCommand(command)
        };

        {
            std::scoped_lock lock(_devMutex);
            const auto existing = std::find_if(
                _devCommands.begin(),
                _devCommands.end(),
                [&](const auto& candidate) { return candidate.command == command; });
            if (existing != _devCommands.end()) {
                _selectedDevCommand = static_cast<int>(
                    std::distance(_devCommands.begin(), existing));
                _rmlDeveloperSyncPending.store(true, std::memory_order_release);
                logger::info(
                    "DragonBoardVR: Dev command '{}' is already in the command list.",
                    command);
                return;
            }
        }

        if (!AppendDevCommandToIni(entry.label, entry.command)) {
            logger::error(
                "DragonBoardVR: Dev command '{}' was not added because it could not be saved.",
                command);
            return;
        }

        {
            std::scoped_lock lock(_devMutex);
            _devCommands.push_back(std::move(entry));
            _selectedDevCommand = static_cast<int>(_devCommands.size() - 1);
        }
        _rmlDeveloperSyncPending.store(true, std::memory_order_release);
        logger::info("DragonBoardVR: saved a new Dev command: '{}'.", command);
    }

    void RmlPanelHost::CaptureDevGameInfoGameThread()
    {
        DevGameInfoSnapshot snapshot;
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            const auto position = player->GetPosition();
            snapshot.playerX = position.x;
            snapshot.playerY = position.y;
            snapshot.playerZ = position.z;
            if (auto* cell = player->GetParentCell()) {
                snapshot.cellFormId = cell->GetFormID();
                if (const char* name = cell->GetName(); name && name[0] != '\0') {
                    snapshot.cellName = name;
                }
                if (auto* worldspace = cell->GetRuntimeData().worldSpace) {
                    snapshot.worldspaceFormId = worldspace->GetFormID();
                    if (const char* name = worldspace->GetName(); name && name[0] != '\0') {
                        snapshot.worldspaceName = name;
                    }
                }
            }
        }
        snapshot.mapCalibrationPoints = vrui::VRUISettings::get().mapCalibrationPoints;
        std::scoped_lock lock(_devMutex);
        _devGameInfo = std::move(snapshot);
    }

    void RmlPanelHost::CaptureMapCalibrationGameThread(
        std::size_t cityIndex, float, float)
    {
        static constexpr std::array<const char*, vrui::kMapCalibrationPointCount> cityNames{
            "Whiterun", "Riften", "Solitude", "Falkreath", "Windhelm"
        };
        if (cityIndex >= cityNames.size()) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        if (!player || !cell || cell->cellFlags.any(RE::TESObjectCELL::Flag::kIsInteriorCell) ||
            !cell->GetRuntimeData().worldSpace) {
            logger::warn(
                "DragonBoardVR: map calibration point '{}' rejected because the player is not in an exterior worldspace.",
                cityNames[cityIndex]);
            _inputBridge.SetHaptic(static_cast<std::uint8_t>(
                DragonBoardRmlUi::HapticCue::kError));
            return;
        }

        const auto position = player->GetPosition();
        auto& settings = vrui::VRUISettings::get();
        float mapU = 0.0f;
        float mapV = 0.0f;
        if (!vrui::GetMapCalibrationLandmarkUv(cityIndex, mapU, mapV)) {
            logger::warn("DragonBoardVR: map calibration could not resolve the fixed '{}' landmark.", cityNames[cityIndex]);
            return;
        }
        settings.mapCalibrationPoints[cityIndex] = vrui::MapCalibrationPoint{
            true, position.x, position.y, mapU, mapV
        };
        vrui::VRMenuManager::get().saveSettingsNow();
        CaptureDevGameInfoGameThread();
        _rmlDeveloperInfoSyncPending.store(true, std::memory_order_release);
        _inputBridge.SetHaptic(static_cast<std::uint8_t>(
            DragonBoardRmlUi::HapticCue::kStrong));

        vrui::MapCalibrationTransform calibration;
        const bool ready = vrui::FitMapCalibration(settings.mapCalibrationPoints, calibration);
        logger::info(
            "DragonBoardVR: map calibration '{}' saved against fixed texture UV: world=({:.2f}, {:.2f}) mapUV=({:.4f}, {:.4f}) ready={} rms={:.5f}.",
            cityNames[cityIndex], position.x, position.y, mapU, mapV,
            ready, ready ? calibration.rmsError : 0.0f);
    }
}
