#include "ui/imgui/DragonBoardSettingsMenu.h"
#include "ui/imgui/StandaloneImGuiStyle.h"

#include "vrui/VRUISettings.h"

#include <algorithm>
#include <iterator>

#include <imgui.h>

namespace dragonboard::ui::imgui
{
    void DragonBoardSettingsMenu::DrawSettings()
    {
        bool open = true;
        if (!standalone::BeginPanel("DragonBoardSettings", "DragonBoard Settings", &open)) {
            ImGui::End();
            return;
        }

        constexpr const char* pages[]{
            "General",
            "Position",
            "Visuals",
            "Item Scales",
            "Labels"
        };
        _settingsPage = std::clamp(_settingsPage, 0, static_cast<int>(std::size(pages)) - 1);

        const float actionHeight = std::max(86.0f, ImGui::GetFrameHeight() + 18.0f);
        const float footerHeight = actionHeight + 22.0f;
        const float bodyHeight = std::max(300.0f, ImGui::GetContentRegionAvail().y - footerHeight);
        const float sidebarWidth = 480.0f;

        ImGui::BeginChild("SettingsNavigation", ImVec2(sidebarWidth, bodyHeight), true);
        ImGui::TextDisabled("CATEGORIES");
        ImGui::Separator();
        for (int i = 0; i < static_cast<int>(std::size(pages)); ++i) {
            if (standalone::SidebarItem(pages[i], _settingsPage == i)) {
                _settingsPage = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("SettingsContent", ImVec2(0.0f, bodyHeight), true);
        standalone::SectionHeading(pages[_settingsPage]);
        ImGui::Spacing();

        bool changed = false;
        {
            std::scoped_lock lock(_draftMutex);
            ImGui::PushItemWidth(std::min(760.0f, ImGui::GetContentRegionAvail().x * 0.62f));
            switch (_settingsPage) {
            case 0:
                changed |= ImGui::Checkbox("Edit mode", &_draft.editModeEnabled);
                changed |= ImGui::Checkbox("Show developer panel button", &_draft.showDevButton);
                changed |= ImGui::SliderFloat("Menu scale", &_draft.menuScale, 0.25f, 3.0f, "%.2f");
                changed |= ImGui::SliderFloat("Button spacing X", &_draft.buttonSpacingX, 0.2f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Button spacing Y", &_draft.buttonSpacingY, 0.2f, 8.0f, "%.2f");
                break;
            case 1:
                ImGui::TextDisabled("POSITION");
                changed |= ImGui::SliderFloat("Position X", &_draft.menuOffsetX, -40.0f, 40.0f, "%.2f");
                changed |= ImGui::SliderFloat("Position Y", &_draft.menuOffsetY, -40.0f, 40.0f, "%.2f");
                changed |= ImGui::SliderFloat("Position Z", &_draft.menuOffsetZ, -40.0f, 40.0f, "%.2f");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextDisabled("ROTATION");
                changed |= ImGui::SliderFloat("Rotation X", &_draft.menuRotX, -180.0f, 180.0f, "%.1f deg");
                changed |= ImGui::SliderFloat("Rotation Y", &_draft.menuRotY, -180.0f, 180.0f, "%.1f deg");
                changed |= ImGui::SliderFloat("Rotation Z", &_draft.menuRotZ, -180.0f, 180.0f, "%.1f deg");
                break;
            case 2:
                changed |= ImGui::SliderFloat("Button scale", &_draft.buttonMeshScale, 0.1f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Item scale", &_draft.itemMeshScale, 0.1f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Grid Z offset", &_draft.containerGridOffsetZ, -20.0f, 20.0f, "%.2f");
                changed |= ImGui::SliderFloat("Reticle scale", &_draft.reticleScale, 0.1f, 10.0f, "%.2f");
                break;
            case 3:
                changed |= ImGui::SliderFloat("Weapons", &_draft.itemWeaponScale, 0.1f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Armor and clothes", &_draft.itemArmorScale, 0.1f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Potions", &_draft.itemPotionScale, 0.1f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Food and ingredients", &_draft.itemFoodScale, 0.1f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Misc and books", &_draft.itemMiscScale, 0.1f, 5.0f, "%.2f");
                break;
            case 4:
                changed |= ImGui::SliderFloat("Label scale", &_draft.labelScale, 0.1f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Label spacing", &_draft.labelSpacing, -5.0f, 5.0f, "%.2f");
                changed |= ImGui::SliderFloat("Label X", &_draft.labelXOffset, -10.0f, 10.0f, "%.2f");
                changed |= ImGui::SliderFloat("Label Y", &_draft.labelYOffset, -10.0f, 10.0f, "%.2f");
                changed |= ImGui::SliderFloat("Label Z", &_draft.labelZOffset, -10.0f, 10.0f, "%.2f");
                break;
            default:
                break;
            }
            ImGui::PopItemWidth();
        }
        if (changed) MarkChanged();
        ImGui::EndChild();

        ImGui::Separator();
        const float actionWidth = 280.0f;
        const float buttonsWidth = actionWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() - buttonsWidth - ImGui::GetStyle().WindowPadding.x));
        if (ImGui::Button("Save INI", ImVec2(actionWidth, actionHeight))) {
            _applyPending.store(true);
            _savePending.store(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(actionWidth, actionHeight))) {
            open = false;
        }

        ImGui::End();
        if (!open) Close();
    }

    void DragonBoardSettingsMenu::CaptureSettingsGameThread()
    {
        const auto& settings = vrui::VRUISettings::get();
        _menuOnLeftHand.store(settings.useLeftHandAsMenu);
        std::scoped_lock lock(_draftMutex);
        _draft.editModeEnabled = settings.editModeEnabled;
        _draft.showDevButton = settings.showDevButton;
        _draft.menuScale = settings.menuScale;
        _draft.buttonSpacingX = settings.buttonSpacingX;
        _draft.buttonSpacingY = settings.buttonSpacingY;
        _draft.menuOffsetX = settings.menuOffsetX;
        _draft.menuOffsetY = settings.menuOffsetY;
        _draft.menuOffsetZ = settings.menuOffsetZ;
        _draft.menuRotX = settings.menuRotX;
        _draft.menuRotY = settings.menuRotY;
        _draft.menuRotZ = settings.menuRotZ;
        _draft.buttonMeshScale = settings.buttonMeshScale;
        _draft.itemMeshScale = settings.itemMeshScale;
        _draft.containerGridOffsetZ = settings.containerGridOffsetZ;
        _draft.reticleScale = settings.reticleScaleX;
        _draft.itemWeaponScale = settings.itemWeaponScale;
        _draft.itemArmorScale = settings.itemArmorScale;
        _draft.itemPotionScale = settings.itemPotionScale;
        _draft.itemFoodScale = settings.itemFoodScale;
        _draft.itemMiscScale = settings.itemMiscScale;
        _draft.labelScale = settings.labelScale;
        _draft.labelSpacing = settings.labelSpacing;
        _draft.labelXOffset = settings.labelXOffset;
        _draft.labelYOffset = settings.labelYOffset;
        _draft.labelZOffset = settings.labelZOffset;
    }

    void DragonBoardSettingsMenu::ApplyDraftGameThread()
    {
        auto& settings = vrui::VRUISettings::get();
        std::scoped_lock lock(_draftMutex);
        settings.editModeEnabled = _draft.editModeEnabled;
        settings.showDevButton = _draft.showDevButton;
        settings.menuScale = _draft.menuScale;
        settings.buttonSpacingX = _draft.buttonSpacingX;
        settings.buttonSpacingY = _draft.buttonSpacingY;
        settings.menuOffsetX = _draft.menuOffsetX;
        settings.menuOffsetY = _draft.menuOffsetY;
        settings.menuOffsetZ = _draft.menuOffsetZ;
        settings.menuRotX = _draft.menuRotX;
        settings.menuRotY = _draft.menuRotY;
        settings.menuRotZ = _draft.menuRotZ;
        settings.buttonMeshScale = _draft.buttonMeshScale;
        settings.itemMeshScale = _draft.itemMeshScale;
        settings.containerGridOffsetZ = _draft.containerGridOffsetZ;
        settings.reticleScaleX = settings.reticleScaleY = settings.reticleScaleZ = _draft.reticleScale;
        settings.itemWeaponScale = _draft.itemWeaponScale;
        settings.itemArmorScale = _draft.itemArmorScale;
        settings.itemPotionScale = _draft.itemPotionScale;
        settings.itemFoodScale = _draft.itemFoodScale;
        settings.itemMiscScale = _draft.itemMiscScale;
        settings.labelScale = _draft.labelScale;
        settings.labelSpacing = _draft.labelSpacing;
        settings.labelXOffset = _draft.labelXOffset;
        settings.labelYOffset = _draft.labelYOffset;
        settings.labelZOffset = _draft.labelZOffset;
    }

    void DragonBoardSettingsMenu::MarkChanged()
    {
        _applyPending.store(true);
    }
}
