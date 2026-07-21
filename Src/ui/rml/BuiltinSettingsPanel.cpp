#include "ui/rml/RmlPanelHost.h"

#include "ui/menu/MenuComposition.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

namespace dragonboard::ui::rml
{
    void RmlPanelHost::CaptureSettingsGameThread()
    {
        const auto& settings = vrui::VRUISettings::get();
        std::scoped_lock lock(_draftMutex);
        _draft.editModeEnabled = settings.editModeEnabled;
        _draft.showDevButton = settings.showDevButton;
        _draft.worldPinned = vrui::VRMenuManager::get().isBoardWorldPinned();
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

    void RmlPanelHost::ApplyDraftGameThread()
    {
        auto& settings = vrui::VRUISettings::get();
        std::scoped_lock lock(_draftMutex);
        settings.editModeEnabled = _draft.editModeEnabled;
        settings.showDevButton = _draft.showDevButton;
        dragonboard::ui::menu::SetDeveloperButtonVisible(settings.showDevButton);
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

}
