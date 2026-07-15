#include "ui/rml/RmlPanelHost.h"
#include "game/actions/ActionExecutor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace dragonboard::ui::rml
{
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

        const std::filesystem::path iniPath = "Data/SKSE/Plugins/DragonBoardVR_DevCommands.ini";
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
        if (_selectedDevCommand >= static_cast<int>(_devCommands.size())) {
            _selectedDevCommand = _devCommands.empty() ? -1 : 0;
        }
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
        std::scoped_lock lock(_devMutex);
        _devGameInfo = std::move(snapshot);
    }
}
