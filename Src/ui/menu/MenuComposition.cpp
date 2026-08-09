#include <RE/I/IFormFactory.h>
#include <RE/S/Script.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/U/UI.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESBoundObject.h>
#include <atomic>

#include "game/actions/ActionExecutor.h"
#include "ui/rml/RmlPanelHost.h"
#include "ui/menu/MenuActionRouter.h"
#include "ui/menu/MenuComposition.h"
#include "ui/gallery/GalleryCatalog.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUIItemUtils.h"

#include "vrui/VRUIPanel.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRUIToggleButton.h"
#include "vrui/VRUIContainer.h"
#include "vrui/VRUISlider.h"
#include "vrui/VRUIItemEditPanel.h"
#include "vrui/VRUISettings.h"
#include "vrui/VRUIInventoryContainer.h"
#include "vrui/VRUIMagicContainer.h"
#include "vrui/VRUIMapMarker.h"

using namespace vrui;

namespace
{
    bool g_menuCreated = false;
    std::weak_ptr<VRUIButton> g_developerButton;
    std::weak_ptr<VRUIContainer> g_galleryMarkerContainer;
}

// =========================================================================
// Demo Menu
// =========================================================================

#include "vrui/VRUILayoutManager.h"

static void applyJSONTransform(std::shared_ptr<vrui::VRUIWidget> widget, const std::string& containerId, const std::string& elementId) {
    if (!widget) return;
    widget->setLayoutId(elementId);

    // 1. JSON is the source of truth for button transforms
    auto layout = vrui::VRUILayoutManager::findElementAnywhere(elementId);
    if (layout) {
        vrui::VRUILayoutManager::applyLayoutToWidget(widget.get(), elementId);
        return;
    }

    // 2. Legacy INI seed for category buttons only when JSON has no entry yet
    auto& settings = VRUISettings::get();
    auto it = settings.categoryButtons.find(elementId);
    if (it != settings.categoryButtons.end()) {
        const auto& data = it->second;
        widget->setLocalPosition(RE::NiPoint3(data.posX, data.posY, data.posZ));

        RE::NiMatrix3 rot;
        vrui::VRUILayoutManager::setMatrixEuler(rot,
            data.rotX * kDegToRad,
            data.rotY * kDegToRad,
            data.rotZ * kDegToRad);
        widget->setLocalRotation(rot);

        widget->setLocalScale(data.scale);
    }

    auto pos = widget->getLocalPosition();
    RE::NiMatrix3 rot;
    if (auto* node = widget->getNode()) {
        rot = node->local.rotate;
    }
    float scale = widget->getLocalScale();
    vrui::VRUILayoutManager::registerDefaultLayout(containerId, elementId, pos, rot, scale);
}

static void setWidgetEulerDegrees(const std::shared_ptr<vrui::VRUIWidget>& widget, float rotX, float rotY, float rotZ)
{
    if (!widget) {
        return;
    }

    RE::NiMatrix3 rot;
    vrui::VRUILayoutManager::setMatrixEuler(rot, rotX * kDegToRad, rotY * kDegToRad, rotZ * kDegToRad);
    widget->setLocalRotation(rot);
}

static void attachRmlButtonLabel(
    const std::shared_ptr<VRUIButton>& button,
    std::string id,
    std::string text)
{
    if (!button) return;

    auto& settings = VRUISettings::get();
    button->setLabel(text);

    dragonboard::ui::rml::RmlWidgetLabelPlacement placement;
    placement.localPosition = {
        settings.labelXOffset,
        settings.labelYOffset,
        settings.labelZOffset };
    placement.localRotation.SetEulerAnglesXYZ(
        90.0f * kDegToRad,
        0.0f,
        180.0f * kDegToRad);
    placement.physicalWidth = 2.5f;
    placement.physicalHeight = settings.labelScale;

    auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
    if (!rmlHost.AttachWidgetLabel(
            id,
            button->getVisualAttachmentNode(),
            button->getLabel(),
            placement)) {
        logger::error(
            "DragonBoardVR: failed to attach RmlUi label '{}' to button '{}'.",
            id,
            button->getLabel());
        return;
    }

    button->setLabelChangedCallback(
        [labelId = std::move(id)](std::string_view updatedText) {
            dragonboard::ui::rml::RmlPanelHost::GetSingleton().SetWidgetLabelText(
                labelId, std::string(updatedText));
        });
}

static void configureFavoriteButton(const std::shared_ptr<VRUIContainer>& fixedContainer,
    const std::shared_ptr<VRUIButton>& button,
    const std::string& action,
    const std::string& originalLabel)
{
    if (!button) {
        return;
    }

    if (action == VRUISettings::get().defaultPanelAction) {
        button->setLabel("* " + originalLabel);
    }

    button->setOnLongPressHandler([action, originalLabel, fixedContainer](VRUIButton* pressedButton, EquipHand) {
        if (!pressedButton || action.empty() || action == "None") {
            return;
        }

        auto& settings = VRUISettings::get();
        settings.defaultPanelAction = action;
        VRMenuManager::get().requestSettingsSave();

        if (fixedContainer) {
            for (auto& child : fixedContainer->getChildren()) {
                if (auto buttonChild = std::dynamic_pointer_cast<VRUIButton>(child)) {
                    std::string label = buttonChild->getLabel();
                    if (label.rfind("* ", 0) == 0) {
                        buttonChild->setLabel(label.substr(2));
                    }
                }
            }
        }

        pressedButton->setLabel("* " + originalLabel);
        logger::trace("DragonBoardVR: Default panel set to '{}'", action);
    });
}


static void ensureRmlBackendPanel(const std::string& panelName, const std::string& type)
{
    auto& manager = VRMenuManager::get();
    if (manager.findPanelByName(panelName)) {
        return;
    }

    auto panel = std::make_shared<VRUIPanel>(panelName);
    if (type == "Inventory") {
        auto container = std::make_shared<VRUIInventoryContainer>(panelName + "_Grid");
        container->setRmlBackendOnly(true);
        panel->addElement(container);
    } else if (type == "Magic") {
        auto container = std::make_shared<VRUIMagicContainer>(panelName + "_Grid");
        container->setRmlBackendOnly(true);
        panel->addElement(container);
    } else {
        return;
    }

    manager.registerPanel(panel);
}

static void ensureRmlHostPanel(const std::string& panelName)
{
    auto& manager = VRMenuManager::get();
    if (!manager.findPanelByName(panelName)) {
        manager.registerPanel(std::make_shared<VRUIPanel>(panelName));
    }
}

// Fixed buttons and startup use the same action authority.
static VRUIButton::PressCallback resolveFixedButtonAction(const std::string& action)
{
    return [action](VRUIButton*, EquipHand) {
        (void)dragonboard::ui::menu::MenuActionRouter::Execute(
            action,
            dragonboard::ui::menu::MenuActionMode::Toggle);
    };
}

bool dragonboard::ui::menu::IsCreated()
{
    return g_menuCreated;
}

void dragonboard::ui::menu::Recreate()
{
    g_menuCreated = false;
    Create();
}


void dragonboard::ui::menu::RefreshGalleryMarkers()
{
    auto container = g_galleryMarkerContainer.lock();
    if (!container) return;
    container->clearElements();
    const auto& settings = VRUISettings::get();
    std::size_t count = 0;
    for (const auto& photo : dragonboard::ui::gallery::GalleryCatalog::GetSingleton().Snapshot()) {
        if (!photo.mapPinned || photo.worldspaceFormId == 0 ||
            count >= static_cast<std::size_t>(std::max(settings.galleryMaximumVisibleMarkers, 0))) continue;
        auto marker = std::make_shared<vrui::VRUIMapMarker>(
            "DragonBoardVR\\DBCameraMarker.nif",
            vrui::MapMarkerSource::GalleryPhoto,
            0,
            "textures\\DBCameraMarker.dds",
            photo.position,
            photo.id,
            photo.worldspaceFormId);
        container->addElement(std::static_pointer_cast<vrui::VRUIWidget>(marker));
        ++count;
    }
}

void dragonboard::ui::menu::SetDeveloperButtonVisible(bool visible)
{
    if (auto button = g_developerButton.lock()) {
        button->setVisible(visible);
    }
}

void dragonboard::ui::menu::Create()
{
    if (g_menuCreated) {
        logger::warn("DragonBoardVR: Demo menu already created, skipping.");
        return;
    }

    logger::trace("DragonBoardVR: Creating demo menu...");
    auto& manager = VRMenuManager::get();
    auto& settings = VRUISettings::get();

    // Load JSON Layout override before generation
    vrui::VRUILayoutManager::loadLayout();

    // Log the real INI save path so the user can inspect the correct file
    auto iniPath = VRUISettings::getDefaultIniPath();
    logger::trace("DragonBoardVR: INI save path = '{}' (relative to game working directory)", iniPath);

    // --- Panels ---
    auto panel = std::make_shared<VRUIPanel>("MainPanel");
    auto bgPanel = std::make_shared<VRUIPanel>("Background_Panel", 1.0f, true);

    auto galleryMarkers = std::make_shared<VRUIContainer>("GalleryMarkers", ContainerLayout::Free);
    bgPanel->addElement(galleryMarkers);
    g_galleryMarkerContainer = galleryMarkers;

    // Player Map Marker integration
    auto mapMarker = std::make_shared<vrui::VRUIMapMarker>(settings.mapMarkerNifPath);
    bgPanel->addElement(std::static_pointer_cast<vrui::VRUIWidget>(mapMarker));
    constexpr std::array<const char*, vrui::VRUIMapMarker::kQuestMarkerSlotCount>
        questMarkerNifPaths{
            "DragonBoardVR\\DBMarkerMain.nif",
            "DragonBoardVR\\DBMarkerSide.nif",
            "DragonBoardVR\\DBMarkerMisc.nif"
        };
    constexpr std::array<const char*, vrui::VRUIMapMarker::kQuestMarkerSlotCount>
        questMarkerTextures{
            "textures\\DBMarkerMain.dds",
            "textures\\DBMarkerSide.dds",
            "textures\\DBMarkerMisc.dds"
        };
    for (std::size_t slot = 0; slot < questMarkerTextures.size(); ++slot) {
        auto questMarker = std::make_shared<vrui::VRUIMapMarker>(
            questMarkerNifPaths[slot],
            vrui::MapMarkerSource::QuestObjective,
            slot,
            questMarkerTextures[slot]);
        bgPanel->addElement(std::static_pointer_cast<vrui::VRUIWidget>(questMarker));
    }
    auto persistentPanel = std::make_shared<VRUIPanel>("Persistent_Panel", 1.0f, false);
    persistentPanel->setPointerSurface(true);
    auto alwaysVisiblePanel = std::make_shared<VRUIPanel>("AlwaysVisiblePanel", 1.0f, false);
    alwaysVisiblePanel->setHandFollowBasis(VRUIPanel::HandFollowBasis::kLeft);
    alwaysVisiblePanel->setActive(false);
    auto alwaysVisibleRightHandPanel = std::make_shared<VRUIPanel>("AlwaysVisibleRightHandPanel", 1.0f, false);
    alwaysVisibleRightHandPanel->setHandFollowBasis(VRUIPanel::HandFollowBasis::kRight);
    alwaysVisibleRightHandPanel->setActive(false);

    // =========================================================================
    // PERSISTENT PANEL — sidebar + nav buttons, always visible during panel switches
    // All fixed buttons live here in a single Free container so snapping works
    // uniformly across ALL fixed buttons (sidebar + nav share the same parent).
    // =========================================================================
    auto fixedContainer = std::make_shared<VRUIContainer>("FixedButtons", ContainerLayout::Free);
    auto fixedWidgetsContainer = std::make_shared<VRUIContainer>(
        "FixedWidgetsContainer",
        ContainerLayout::Free);
    persistentPanel->addElement(fixedWidgetsContainer);

    // --- Sidebar Fixed Buttons ---
    constexpr const char* kSkillsIconNif = "DragonBoardVR\\skillsicon.nif";
    constexpr const char* kInventoryIconNif = "DragonBoardVR\\inventoryicon.nif";
    constexpr const char* kMagicIconNif = "DragonBoardVR\\magicicon.nif";
    constexpr const char* kSaveIconNif = "DragonBoardVR\\saveicon.nif";
    constexpr const char* kFavoritesIconNif = "DragonBoardVR\\favoritesicon.nif";
    constexpr const char* kMapIconNif = "DragonBoardVR\\mapicon.nif";
    const float fixedButtonIconXOffset = settings.labelXOffset;
    const float fixedButtonIconYOffset = settings.labelYOffset - 0.1f;
    const float fixedButtonIconZOffset = settings.labelZOffset - 0.05f;
    const float fixedButtonIconScale = 0.5625f;

    auto sbStatus = std::make_shared<VRUIButton>("", settings.statusNifPath, "textures\\test.dds", 2.0f, 2.0f, true);
    sbStatus->setButtonId("Status");
    sbStatus->setOverlayNif(kSkillsIconNif, fixedButtonIconXOffset, fixedButtonIconYOffset, fixedButtonIconZOffset, fixedButtonIconScale);
    sbStatus->setOnPressHandler(resolveFixedButtonAction(settings.bStatusAction));
    sbStatus->setLocalPosition({settings.bStatusPosX, settings.bStatusPosY, settings.bStatusPosZ});
    setWidgetEulerDegrees(sbStatus, settings.bStatusRotX, settings.bStatusRotY, settings.bStatusRotZ);
    sbStatus->setLocalScale(settings.bStatusScale);
    applyJSONTransform(sbStatus, "TopTabs", "Btn_Status");
    configureFavoriteButton(fixedContainer, sbStatus, settings.bStatusAction, settings.bStatusLabel);

    auto sbInv = std::make_shared<VRUIButton>("", settings.inventoryNifPath, "textures\\test.dds", 2.0f, 2.0f, true);
    sbInv->setButtonId("Inventory");
    sbInv->setOverlayNif(kInventoryIconNif, fixedButtonIconXOffset, fixedButtonIconYOffset, fixedButtonIconZOffset, fixedButtonIconScale);
    sbInv->setOnPressHandler(resolveFixedButtonAction(settings.bInvAction));
    sbInv->setLocalPosition({settings.bInvPosX, settings.bInvPosY, settings.bInvPosZ});
    setWidgetEulerDegrees(sbInv, settings.bInvRotX, settings.bInvRotY, settings.bInvRotZ);
    sbInv->setLocalScale(settings.bInvScale);
    applyJSONTransform(sbInv, "TopTabs", "Btn_Inventory");
    configureFavoriteButton(fixedContainer, sbInv, settings.bInvAction, settings.bInvLabel);

    auto sbMagic = std::make_shared<VRUIButton>("", settings.magicNifPath, "textures\\test.dds", 2.0f, 2.0f, true);
    sbMagic->setButtonId("Magic");
    sbMagic->setOverlayNif(kMagicIconNif, fixedButtonIconXOffset, fixedButtonIconYOffset, fixedButtonIconZOffset, fixedButtonIconScale);
    sbMagic->setOnPressHandler(resolveFixedButtonAction(settings.bMagicAction));
    sbMagic->setLocalPosition({settings.bMagicPosX, settings.bMagicPosY, settings.bMagicPosZ});
    setWidgetEulerDegrees(sbMagic, settings.bMagicRotX, settings.bMagicRotY, settings.bMagicRotZ);
    sbMagic->setLocalScale(settings.bMagicScale);
    applyJSONTransform(sbMagic, "TopTabs", "Btn_Magic");
    configureFavoriteButton(fixedContainer, sbMagic, settings.bMagicAction, settings.bMagicLabel);

    auto sbSys = std::make_shared<VRUIButton>("", settings.settingsNifPath, "", 2.0f, 2.0f, true);
    attachRmlButtonLabel(sbSys, "fixed.settings", settings.bSysLabel);
    sbSys->setOnPressHandler(resolveFixedButtonAction(settings.bSysAction));
    sbSys->setLocalPosition({settings.bSysPosX, settings.bSysPosY, settings.bSysPosZ});
    setWidgetEulerDegrees(sbSys, settings.bSysRotX, settings.bSysRotY, settings.bSysRotZ);
    sbSys->setLocalScale(settings.bSysScale);
    applyJSONTransform(sbSys, "TopTabs", "Btn_System");
    configureFavoriteButton(fixedContainer, sbSys, settings.bSysAction, settings.bSysLabel);

    auto sbSave = std::make_shared<VRUIButton>("", settings.saveNifPath, "textures\\test.dds", 2.0f, 2.0f, true);
    sbSave->setButtonId("Save");
    sbSave->setOverlayNif(kSaveIconNif, fixedButtonIconXOffset, fixedButtonIconYOffset, fixedButtonIconZOffset, fixedButtonIconScale);
    sbSave->setOnPressHandler(resolveFixedButtonAction(settings.bSaveAction));
    sbSave->setLocalPosition({settings.bSavePosX, settings.bSavePosY, settings.bSavePosZ});
    setWidgetEulerDegrees(sbSave, settings.bSaveRotX, settings.bSaveRotY, settings.bSaveRotZ);
    sbSave->setLocalScale(settings.bSaveScale);
    applyJSONTransform(sbSave, "TopTabs", "Btn_Save");
    configureFavoriteButton(fixedContainer, sbSave, settings.bSaveAction, settings.bSaveLabel);

    auto sbMods = std::make_shared<VRUIButton>("", settings.modsNifPath, "textures\\test.dds", 2.0f, 2.0f);
    attachRmlButtonLabel(sbMods, "fixed.mods", settings.bModsLabel);
    sbMods->setOnPressHandler(resolveFixedButtonAction(settings.bModsAction));
    sbMods->setLocalPosition({settings.bModsPosX, settings.bModsPosY, settings.bModsPosZ});
    setWidgetEulerDegrees(sbMods, settings.bModsRotX, settings.bModsRotY, settings.bModsRotZ);
    sbMods->setLocalScale(settings.bModsScale);
    applyJSONTransform(sbMods, "TopTabs", "Btn_Mods");
    configureFavoriteButton(fixedContainer, sbMods, settings.bModsAction, settings.bModsLabel);

    auto sbFav = std::make_shared<VRUIButton>("", settings.favNifPath, "textures\\test.dds", 2.0f, 2.0f, true);
    sbFav->setButtonId("Journal");
    sbFav->setOverlayNif(kFavoritesIconNif, fixedButtonIconXOffset, fixedButtonIconYOffset, fixedButtonIconZOffset, fixedButtonIconScale);
    sbFav->setOnPressHandler(resolveFixedButtonAction(settings.bFavAction));
    sbFav->setLocalPosition({settings.bFavPosX, settings.bFavPosY, settings.bFavPosZ});
    setWidgetEulerDegrees(sbFav, settings.bFavRotX, settings.bFavRotY, settings.bFavRotZ);
    sbFav->setLocalScale(settings.bFavScale);
    applyJSONTransform(sbFav, "TopTabs", "Btn_Favorites");
    configureFavoriteButton(fixedContainer, sbFav, settings.bFavAction, settings.bFavLabel);



    auto sbMap = std::make_shared<VRUIButton>("", settings.mapNifPath, "", 2.0f, 2.0f, true);
    sbMap->setButtonId("Map");
    sbMap->setOverlayNif(kMapIconNif, fixedButtonIconXOffset, fixedButtonIconYOffset, fixedButtonIconZOffset, fixedButtonIconScale);
    sbMap->setOnPressHandler(resolveFixedButtonAction(settings.bMapAction));
    sbMap->setLocalPosition({settings.bMapPosX, settings.bMapPosY, settings.bMapPosZ});
    setWidgetEulerDegrees(sbMap, settings.bMapRotX, settings.bMapRotY, settings.bMapRotZ);
    sbMap->setLocalScale(settings.bMapScale);
    applyJSONTransform(sbMap, "TopTabs", "Btn_Map");
    configureFavoriteButton(fixedContainer, sbMap, settings.bMapAction, settings.bMapLabel);

    auto sbGallery = std::make_shared<VRUIButton>("", settings.devNifPath, "", 2.0f, 2.0f, true);
    attachRmlButtonLabel(sbGallery, "fixed.gallery", settings.bGalleryLabel);
    sbGallery->setOnPressHandler(resolveFixedButtonAction(settings.bGalleryAction));
    sbGallery->setLocalPosition({ settings.bGalleryPosX, settings.bGalleryPosY, settings.bGalleryPosZ });
    setWidgetEulerDegrees(sbGallery, settings.bGalleryRotX, settings.bGalleryRotY, settings.bGalleryRotZ);
    sbGallery->setLocalScale(settings.bGalleryScale);
    applyJSONTransform(sbGallery, "TopTabs", "Btn_Gallery");
    configureFavoriteButton(fixedContainer, sbGallery, settings.bGalleryAction, settings.bGalleryLabel);

    auto sbDev = std::make_shared<VRUIButton>("", settings.devNifPath, "", 2.0f, 2.0f, true);
    attachRmlButtonLabel(sbDev, "fixed.dev", settings.bDevLabel);
    sbDev->setOnPressHandler(resolveFixedButtonAction(settings.bDevAction));
    sbDev->setLocalPosition({ settings.bDevPosX, settings.bDevPosY, settings.bDevPosZ });
    setWidgetEulerDegrees(sbDev, settings.bDevRotX, settings.bDevRotY, settings.bDevRotZ);
    sbDev->setLocalScale(settings.bDevScale);
    applyJSONTransform(sbDev, "TopTabs", "Btn_Dev");
    configureFavoriteButton(fixedContainer, sbDev, settings.bDevAction, settings.bDevLabel);
    sbDev->setVisible(settings.showDevButton);
    g_developerButton = sbDev;
    fixedContainer->addElement(sbDev);

    // --- Persistent Home Button ---
    auto homeBtn = std::make_shared<VRUIButton>("", settings.homeNifPath, "textures\\test.dds", 2.0f, 2.0f);
    attachRmlButtonLabel(homeBtn, "fixed.home", "Home");
    homeBtn->setOnPressHandler([](VRUIButton*, EquipHand) {
        VRMenuManager::get().navigateHome();
        VRMenuManager::get().switchToPanel("MainPanel");
    });
    homeBtn->setLocalPosition({settings.bHomePosX, settings.bHomePosY, settings.bHomePosZ});
    setWidgetEulerDegrees(homeBtn, settings.bHomeRotX, settings.bHomeRotY, settings.bHomeRotZ);
    homeBtn->setLocalScale(settings.bHomeScale);
    applyJSONTransform(homeBtn, "TopTabs", "Btn_Home");
    configureFavoriteButton(fixedContainer, homeBtn, "MainPanel", "Home");

    // Populate fixedContainer: sidebar + nav + gold all together for uniform snapping
    fixedContainer->addElement(sbStatus);
    fixedContainer->addElement(sbInv);
    fixedContainer->addElement(sbMagic);
    fixedContainer->addElement(sbSys);
    fixedContainer->addElement(sbSave);
    fixedContainer->addElement(sbMods);
    fixedContainer->addElement(sbFav);

    fixedContainer->addElement(sbMap);
    fixedContainer->addElement(sbGallery);
    // sbDev is always present internally; Settings controls its visibility.
    fixedContainer->addElement(homeBtn);
    persistentPanel->addElement(fixedContainer);

    // =========================================================================
    // MAIN PANEL — only the 36-slot grid, no sidebar/nav
    // =========================================================================
    // 5 cols x N rows per settings
    auto grid = std::make_shared<VRUIContainer>("Grid5x4", ContainerLayout::Grid,
        settings.buttonSpacingX, settings.buttonSpacingY, 0.0f);
    grid->setPageSize(settings.gridPageSize);
    grid->setGridColumns(settings.gridColumns);

    // Set as the initial active pageable container
    manager.setActivePageableContainer(grid);

    // Read 8 slots from INI
    auto independentSlots = std::make_shared<VRUIContainer>("IndependentSlots", ContainerLayout::Free);
    panel->addElement(independentSlots);

    for (int i = 0; i < VRUISettings::kMaxSlots; ++i) {
        std::string action = settings.slotActions[i];
        std::string nifPath = settings.slotNifs[i];
        if (nifPath.empty()) nifPath = std::format("DragonBoardVR/slot{:02d}.nif", i + 1);
        std::string texturePath = settings.slotTextures[i];
        if (texturePath.empty()) texturePath = "textures\\test.dds";
        std::string lowerAction = action;
        std::transform(lowerAction.begin(), lowerAction.end(), lowerAction.begin(),
            [](unsigned char c){ return std::tolower(c); });

        std::string slotId = std::format("Slot{:02d}", i + 1);
        auto jsonElement = vrui::VRUILayoutManager::findElementAnywhere(slotId);
        bool forcedByJSON = false;

        if (!jsonElement && settings.slotFloating[i]) {
            RE::NiMatrix3 legacyRot;
            vrui::VRUILayoutManager::setMatrixEuler(
                legacyRot,
                settings.slotRotX[i] * kDegToRad,
                settings.slotRotY[i] * kDegToRad,
                settings.slotRotZ[i] * kDegToRad);
            vrui::VRUILayoutManager::updateElementTransformAnywhere(
                slotId,
                { settings.slotPosX[i], settings.slotPosY[i], settings.slotPosZ[i] },
                legacyRot,
                settings.slotScaleUser[i],
                nifPath,
                "",
                0,
                action);
            jsonElement = vrui::VRUILayoutManager::findElementAnywhere(slotId);
        }

        if (!jsonElement && lowerAction != "none") {
            RE::NiMatrix3 seedRot;
            RE::NiPoint3 seedPos(0.0f, 0.0f, 0.0f);
            float seedScale = settings.slotScaleUser[i] > 0.0f ? settings.slotScaleUser[i] : 1.0f;

            if (settings.slotFloating[i]) {
                seedPos = { settings.slotPosX[i], settings.slotPosY[i], settings.slotPosZ[i] };
                vrui::VRUILayoutManager::setMatrixEuler(
                    seedRot,
                    settings.slotRotX[i] * kDegToRad,
                    settings.slotRotY[i] * kDegToRad,
                    settings.slotRotZ[i] * kDegToRad);
            } else {
                vrui::VRUILayoutManager::setMatrixEuler(seedRot, 0.0f, 0.0f, 0.0f);
            }

            vrui::VRUILayoutManager::updateElementTransformAnywhere(
                slotId,
                seedPos,
                seedRot,
                seedScale,
                nifPath,
                "",
                0,
                action,
                settings.slotLabels[i]);

            if (settings.slotFloating[i]) {
                jsonElement = vrui::VRUILayoutManager::findElementAnywhere(slotId);
            }
        }

        if (jsonElement) {
            // Apply overrides from JSON editor
            if (!jsonElement->actionFunc.empty() && jsonElement->actionFunc != "None") action = jsonElement->actionFunc;
            if (!jsonElement->label.empty() && jsonElement->label != "None") settings.slotLabels[i] = jsonElement->label;
            forcedByJSON = true;
        }

        auto btn = std::make_shared<VRUIButton>(action, nifPath, texturePath, 2.0f, 2.0f);
        btn->setSlotIndex(i);
        btn->setLabel(settings.slotLabels[i]);
        btn->setSublabel(settings.slotSublabels[i]);

        if (jsonElement) {
            vrui::VRUILayoutManager::applyLayoutToWidget(btn.get(), slotId);

            // Sync the floating coordinates in settings so VRUIButton::update doesn't overwrite
            // the JSON position with INI zeros every frame.
            settings.slotPosX[i] = jsonElement->transform.px;
            settings.slotPosY[i] = jsonElement->transform.py;
            settings.slotPosZ[i] = jsonElement->transform.pz;

            // If the element was placed independently via JSON, mark it as floating so the grid logic doesn't squash it
            settings.slotFloatingCache[i] = true;
            settings.slotFloating[i] = true;
        }

        if (lowerAction.starts_with("console:") || lowerAction.starts_with("cmd:") || lowerAction.starts_with("command:")) {
            size_t colonPos = action.find(':');
            // Trim leading whitespace from command
            std::string cmd = action.substr(colonPos + 1);
            cmd.erase(0, cmd.find_first_not_of(" \t"));

            // Commands that load cells or teleport are dangerous if run while the UI is still open.
            // They must be deferred to a later frame (double-deferred: close menu → next game frame → execute).
            const bool isDangerous = dragonboard::game::actions::IsDangerousConsoleCommand(cmd);

            btn->setOnPressHandler([cmd, isDangerous](VRUIButton*, EquipHand) {
                // Step 1: Close the menu immediately
                VRMenuManager::get().closeMenu();
                VRMenuManager::get().executeConsoleCommand(
                    cmd,
                    isDangerous,
                    isDangerous ? "DragonBoardVR: [DEFERRED] Executed dangerous command:" : "DragonBoardVR: Executed console command:");
            });
        } else {
            btn->setOnPressHandler([action](VRUIButton*, EquipHand) {
                (void)dragonboard::ui::menu::MenuActionRouter::Execute(
                    action,
                    dragonboard::ui::menu::MenuActionMode::Toggle);
            });
        }

        if (jsonElement) {
            independentSlots->addElement(btn);
        } else {
            grid->addElement(btn);
        }

        // Hide only if NOT forced visible and action is none/empty
        if ((lowerAction == "none" || action.empty()) && !forcedByJSON) {
            btn->setVisible(false);
        } else if (forcedByJSON) {
            btn->setVisible(true); // make sure it's active
        }
    }

    // Main panel = just the grid
    panel->addElement(grid);



    // Register all panels
    auto itemEditPanel = std::make_shared<VRUIPanel>("ItemEditPanel");
    auto itemEditContainer = std::make_shared<VRUIItemEditPanel>("ItemEditContainer");
    itemEditPanel->addElement(itemEditContainer);

    manager.registerPanel(bgPanel);
    dragonboard::ui::menu::RefreshGalleryMarkers();
    manager.registerPanel(persistentPanel);
    manager.registerPanel(alwaysVisiblePanel);
    manager.registerPanel(alwaysVisibleRightHandPanel);
    manager.registerPanel(panel);
    manager.registerPanel(itemEditPanel);

    // Default to active
    bgPanel->setActive(true);
    panel->setActive(true);

    g_menuCreated = true;
    logger::trace("DragonBoardVR: Menu created with {} slots.", VRUISettings::kMaxSlots);
    // RE::DebugNotification("DragonBoardVR: Menu Ready! Press F8 or hold LEFT grip.");

    // Ensure dynamic panels exist from the start so background pre-loading works properly
    ensureRmlBackendPanel("InventoryPanel", "Inventory");
    ensureRmlBackendPanel("MagicPanel", "Magic");
    ensureRmlHostPanel("ModsPanel");

    // Do not auto-open menu here, because the player might still be in a loading screen
    // and hand node transforms are not initialized yet!
    // manager.toggleMenu();
}
