#include "ui/imgui/DragonBoardSettingsMenu.h"

#include "ui/imgui/StandaloneImGuiStyle.h"
#include "game/actions/ActionExecutor.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <d3d11.h>
#include <imgui.h>

namespace dragonboard::ui::imgui
{
    void DragonBoardSettingsMenu::DrawDeveloperPanel()
    {
        bool open = true;
        if (!standalone::BeginPanel("DragonBoardDeveloper", "DragonBoard Developer Panel", &open)) {
            ImGui::End();
            return;
        }

        constexpr const char* pages[]{ "Console Commands", "Game Information" };
        _developerPage = std::clamp(_developerPage, 0, 1);

        const float actionHeight = std::max(86.0f, ImGui::GetFrameHeight() + 18.0f);
        const float footerHeight = actionHeight + 22.0f;
        const float bodyHeight = std::max(300.0f, ImGui::GetContentRegionAvail().y - footerHeight);
        const float sidebarWidth = 480.0f;

        ImGui::BeginChild("DeveloperNavigation", ImVec2(sidebarWidth, bodyHeight), true);
        ImGui::TextDisabled("DEVELOPER TOOLS");
        ImGui::Separator();
        if (standalone::SidebarItem("Console Commands", _developerPage == 0)) _developerPage = 0;
        if (standalone::SidebarItem("Game Information", _developerPage == 1)) _developerPage = 1;
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("DeveloperContent", ImVec2(0.0f, bodyHeight), true);
        standalone::SectionHeading(pages[_developerPage]);
        ImGui::Spacing();

        if (_developerPage == 0) {
            std::vector<DevCommandEntry> commands;
            {
                std::scoped_lock lock(_devMutex);
                commands = _devCommands;
            }
            if (_selectedDevCommand < 0 || _selectedDevCommand >= static_cast<int>(commands.size())) {
                _selectedDevCommand = commands.empty() ? -1 : 0;
            }

            const float executeHeight = actionHeight + 22.0f;
            const float contentHeight = std::max(220.0f, ImGui::GetContentRegionAvail().y - executeHeight);
            const float listWidth = ImGui::GetContentRegionAvail().x * 0.36f;
            ImGui::BeginChild("DevCommandList", ImVec2(listWidth, contentHeight), true);
            ImGui::TextDisabled("AVAILABLE COMMANDS");
            ImGui::Separator();
            for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
                const float rowHeight = std::max(76.0f, ImGui::GetTextLineHeightWithSpacing() + 18.0f);
                if (ImGui::Selectable(commands[i].label.c_str(), _selectedDevCommand == i, 0, ImVec2(0.0f, rowHeight))) {
                    _selectedDevCommand = i;
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("DevCommandDetails", ImVec2(0.0f, contentHeight), true);
            ImGui::TextDisabled("COMMAND DETAILS");
            ImGui::Separator();
            if (_selectedDevCommand >= 0 && _selectedDevCommand < static_cast<int>(commands.size())) {
                const auto& selected = commands[_selectedDevCommand];
                ImGui::TextWrapped("%s", selected.label.c_str());
                ImGui::Spacing();
                ImGui::TextWrapped("%s", selected.description.c_str());
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("CONSOLE INPUT");
                ImGui::TextWrapped("%s", selected.command.c_str());
                if (selected.dangerous) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.30f, 1.0f),
                        "This command closes DragonBoard and runs after a safe delay.");
                }
            } else {
                ImGui::TextDisabled("No console commands configured.");
            }
            ImGui::EndChild();

            if (_selectedDevCommand >= 0 && _selectedDevCommand < static_cast<int>(commands.size())) {
                const float buttonWidth = 440.0f;
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                    ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x));
                if (ImGui::Button("Execute Command", ImVec2(buttonWidth, actionHeight))) {
                    QueueDevCommand(commands[_selectedDevCommand]);
                }
            }
        } else {
            DevGameInfoSnapshot info;
            {
                std::scoped_lock lock(_devMutex);
                info = _devGameInfo;
            }

            const float performanceHeight = std::max(360.0f, ImGui::GetTextLineHeightWithSpacing() * 5.5f);
            ImGui::BeginChild("PerformanceInfo", ImVec2(0.0f, performanceHeight), true);
            ImGui::TextDisabled("PERFORMANCE");
            ImGui::Separator();
            ImGui::Text("FPS (Present): %.1f", _presentFps);
            ImGui::Text("Frame time: %.2f ms", _presentFrameMs);
            ImGui::Text("Panel draw calls: %d", _panelDrawCalls);
            ImGui::TextDisabled("Panel-only count; it does not instrument every Skyrim D3D11 draw.");
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::BeginChild("RuntimeInfo", ImVec2(0.0f, 0.0f), true);
            ImGui::TextDisabled("RUNTIME AND LOCATION");
            ImGui::Separator();
            ImGui::Text("Panel texture: %ux%u", standalone::kPanelWidth, standalone::kPanelHeight);
            ImGui::Text("ImGui VR Helper: %s", _connected.load() ? "connected" : "not loaded (standalone mode)");
            ImGui::Text("DragonBoardVR: %s", Plugin::VERSION.string().c_str());
            if (_device) {
                ImGui::Text("D3D feature level: 0x%X", static_cast<unsigned>(_device->GetFeatureLevel()));
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Player position: X %.1f  Y %.1f  Z %.1f", info.playerX, info.playerY, info.playerZ);
            ImGui::Text("Cell: %s", info.cellName.empty() ? "<none>" : info.cellName.c_str());
            ImGui::TextDisabled("Cell FormID: %08X", info.cellFormId);
            ImGui::Text("Worldspace: %s", info.worldspaceName.empty() ? "<interior or none>" : info.worldspaceName.c_str());
            if (info.worldspaceFormId != 0) {
                ImGui::TextDisabled("Worldspace FormID: %08X", info.worldspaceFormId);
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::Separator();
        const float closeWidth = 280.0f;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() - closeWidth - ImGui::GetStyle().WindowPadding.x));
        if (ImGui::Button("Close", ImVec2(closeWidth, actionHeight))) open = false;

        ImGui::End();
        if (!open) Close();
    }

    void DragonBoardSettingsMenu::QueueDevCommand(const DevCommandEntry& entry)
    {
        {
            std::scoped_lock lock(_devMutex);
            _pendingDevCommand = entry.command;
            _pendingDevCommandLabel = entry.label;
            _pendingDevCommandDangerous = entry.dangerous;
        }
        _devCommandPending.store(true);
    }

    void DragonBoardSettingsMenu::LoadDevCommandsGameThread()
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

    void DragonBoardSettingsMenu::CaptureDevGameInfoGameThread()
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
