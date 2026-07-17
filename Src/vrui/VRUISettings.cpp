#include "VRUISettings.h"

#include <CLIBUtil/simpleINI.hpp>
#include <cmath>
#include <filesystem>

namespace vrui
{
    VRUISettings& VRUISettings::get()
    {
        static VRUISettings instance;
        return instance;
    }

    VRUISettings::VRUISettings()
    {
        for (int i = 0; i < kMaxSlots; ++i) {
            slotScaleUser[i] = 1.0f;
        }
        categoryButtons["Btn_Cat_All"] = { 8.500000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Weapons"] = { 6.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Armor"] = { 3.500000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Consumables"] = { 1.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Quest"] = { -1.500000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Books"] = { -9.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Misc"] = { -4.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Destruction"] = { 7.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Conjuration"] = { -3.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Restoration"] = { -0.500000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Illusion"] = { 2.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Alteration"] = { 4.500000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Powers"] = { -5.500000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Passive"] = { -8.000000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
        categoryButtons["Btn_Cat_Magic"] = { -6.500000f, -0.250000f, 10.000000f, 10.000000f, 0.000000f, 0.000000f, 0.800000f };
    }

    void VRUISettings::setUseLeftHandAsMenu(bool useLeftHand)
    {
        if (useLeftHandAsMenu == useLeftHand) return;

        useLeftHandAsMenu = useLeftHand;
        menuRotY = -menuRotY;
        menuRotZ = -menuRotZ;
    }

    std::string VRUISettings::getDefaultIniPath()
    {
        return "Data/SKSE/Plugins/DragonBoardVR.ini";
    }

    void VRUISettings::load(const std::string& iniPath)
    {
        CSimpleIniA ini;
        ini.SetUnicode();

        if (ini.LoadFile(iniPath.c_str()) < 0) {
            logger::trace("DragonBoardVR: No INI file found at '{}', using defaults", iniPath);
            save(iniPath);
            return;
        }

        logger::trace("DragonBoardVR: Loading settings from '{}'", iniPath);

        // [General]
        verboseLogging = ini.GetBoolValue("General", "bVerboseLogging", verboseLogging);
        editModeEnabled = ini.GetBoolValue("General", "bEditModeEnabled", editModeEnabled);

        // [Activation]
        activationMode = static_cast<ActivationMode>(ini.GetLongValue("Activation", "iActivationMode", static_cast<int>(activationMode)));
        activationHoldTimeGrip       = (float)ini.GetDoubleValue("Activation", "fHoldTimeGrip",       activationHoldTimeGrip);
        activationHoldTimeTrigger    = (float)ini.GetDoubleValue("Activation", "fHoldTimeTrigger",    activationHoldTimeTrigger);
        activationHoldTimeThumbstick = (float)ini.GetDoubleValue("Activation", "fHoldTimeThumbstick", activationHoldTimeThumbstick);
        // Legacy alias: overrides the new per-mode values only if explicitly set in INI
        activationHoldTime = (float)ini.GetDoubleValue("Activation", "fHoldTime", activationHoldTime);
        useLeftHandAsMenu = ini.GetBoolValue("Activation", "bUseLeftHandAsMenu", useLeftHandAsMenu);
        
        // [Visual]
        menuScale       = (float)ini.GetDoubleValue("Visual", "fMenuScale",   menuScale);
        menuOffsetX     = (float)ini.GetDoubleValue("Visual", "fMenuOffsetX", menuOffsetX);
        menuOffsetY     = (float)ini.GetDoubleValue("Visual", "fMenuOffsetY", menuOffsetY);
        menuOffsetZ     = (float)ini.GetDoubleValue("Visual", "fMenuOffsetZ", menuOffsetZ);
        menuRotX        = (float)ini.GetDoubleValue("Visual", "fMenuRotX",    menuRotX);
        menuRotY        = (float)ini.GetDoubleValue("Visual", "fMenuRotY",    menuRotY);
        menuRotZ        = (float)ini.GetDoubleValue("Visual", "fMenuRotZ",    menuRotZ);
        containerGridOffsetZ = (float)ini.GetDoubleValue("Visual", "fContainerGridOffsetZ", containerGridOffsetZ);
        // Older builds required Z=180 to show the front of the board. The
        // fixed half-turn now belongs to VRUIPanel, so migrate the configurable
        // value once while preserving the exact visible pose: old 180 -> new 0.
        const bool zeroFaceRotation =
            ini.GetBoolValue("Visual", "bMenuFaceRotationZeroBased", false);
        if (!zeroFaceRotation) {
            menuRotZ = std::fmod(menuRotZ - 180.0f, 360.0f);
            if (menuRotZ <= -180.0f) menuRotZ += 360.0f;
            if (menuRotZ > 180.0f) menuRotZ -= 360.0f;
            ini.SetDoubleValue("Visual", "fMenuRotZ", menuRotZ);
            ini.SetBoolValue("Visual", "bMenuFaceRotationZeroBased", true,
                "; true = fMenuRotZ 0 shows the front face of the board");
            if (ini.SaveFile(iniPath.c_str()) < 0) {
                logger::warn("DragonBoardVR: Could not persist zero-based menu rotation migration to '{}'.", iniPath);
            } else {
                logger::info(
                    "DragonBoardVR: Migrated menu rotation to zero-based front-face convention (fMenuRotZ={:.1f}).",
                    menuRotZ);
            }
        }

        // Apply Mirroring for Right Hand
        // The INI file is treated as "Left Hand Base". If we are on the right hand,
        // we flip the Y/Z rotations in memory.
        if (!useLeftHandAsMenu) {
            menuRotY    = -menuRotY;
            menuRotZ    = -menuRotZ;
        }

        bEnableMenuLerp = ini.GetBoolValue("Visual", "bEnableMenuLerp", bEnableMenuLerp);
        fMenuLerpSpeed  = (float)ini.GetDoubleValue("Visual", "fMenuLerpSpeed", fMenuLerpSpeed);

        // [Buttons]
        buttonSpacingX  = (float)ini.GetDoubleValue("Buttons", "fButtonSpacingX",  buttonSpacingX);
        buttonSpacingY  = (float)ini.GetDoubleValue("Buttons", "fButtonSpacingY",  buttonSpacingY);
        buttonMeshScale = (float)ini.GetDoubleValue("Buttons", "fButtonMeshScale", buttonMeshScale);
        itemMeshScale   = (float)ini.GetDoubleValue("Buttons", "fItemMeshScale",   itemMeshScale);
        itemWeaponScale = (float)ini.GetDoubleValue("Buttons", "fItemWeaponScale", itemWeaponScale);
        itemArmorScale  = (float)ini.GetDoubleValue("Buttons", "fItemArmorScale",  itemArmorScale);
        itemPotionScale = (float)ini.GetDoubleValue("Buttons", "fItemPotionScale", itemPotionScale);
        itemFoodScale   = (float)ini.GetDoubleValue("Buttons", "fItemFoodScale",   itemFoodScale);
        itemMiscScale   = (float)ini.GetDoubleValue("Buttons", "fItemMiscScale",   itemMiscScale);
        gridColumns     = (int)ini.GetLongValue    ("Buttons", "iGridColumns",     gridColumns);
        gridPageSize    = (int)ini.GetLongValue    ("Buttons", "iGridPageSize",    gridPageSize);

        // [Background]
        showBackground    = ini.GetBoolValue("Background", "bShowBackground", showBackground);
        backgroundScale   = (float)ini.GetDoubleValue("Background", "fScale",   backgroundScale);
        backgroundOffsetX = (float)ini.GetDoubleValue("Background", "fOffsetX", backgroundOffsetX);
        backgroundOffsetY = (float)ini.GetDoubleValue("Background", "fOffsetY", backgroundOffsetY);
        backgroundOffsetZ = (float)ini.GetDoubleValue("Background", "fOffsetZ", backgroundOffsetZ);
        backgroundRotX    = (float)ini.GetDoubleValue("Background", "fRotX",    backgroundRotX);
        backgroundRotY    = (float)ini.GetDoubleValue("Background", "fRotY",    backgroundRotY);
        backgroundRotZ    = (float)ini.GetDoubleValue("Background", "fRotZ",    backgroundRotZ);

        // [Interaction]
        raycastMaxDistance = (float)ini.GetDoubleValue("Interaction", "fRaycastMaxDistance", raycastMaxDistance);
        hapticOnHover      = ini.GetBoolValue("Interaction", "bHapticOnHover", hapticOnHover);
        hapticOnPress      = ini.GetBoolValue("Interaction", "bHapticOnPress", hapticOnPress);
        bEnableRotationSnapping = ini.GetBoolValue("Interaction", "bEnableRotationSnapping", bEnableRotationSnapping);
        fSnapDistanceThreshold  = (float)ini.GetDoubleValue("Interaction", "fSnapDistanceThreshold", fSnapDistanceThreshold);
        hapticIntensity    = (float)ini.GetDoubleValue("Interaction", "fHapticIntensity", hapticIntensity);
        hapticDuration     = (float)ini.GetDoubleValue("Interaction", "fHapticDuration",  hapticDuration);
        equipCooldown      = (float)ini.GetDoubleValue("Interaction", "fEquipCooldown",   equipCooldown);
        laserNifPath       = ini.GetValue("Interaction", "sLaserNifPath",      laserNifPath.c_str());
        backgroundNifPath  = ini.GetValue("Interaction", "sBackgroundNifPath", backgroundNifPath.c_str());
        mapNifPath         = ini.GetValue("Interaction", "sMapNifPath",         mapNifPath.c_str());
        devNifPath         = ini.GetValue("Interaction", "sDevNifPath",         devNifPath.c_str());
        magicNifPath       = ini.GetValue("Interaction", "sMagicNifPath",       magicNifPath.c_str());
        inventoryNifPath   = ini.GetValue("Interaction", "sInventoryNifPath",   inventoryNifPath.c_str());
        unknownNifPath     = ini.GetValue("Interaction", "sUnknownNifPath",     unknownNifPath.c_str());
        settingsNifPath    = ini.GetValue("Interaction", "sSettingsNifPath",    settingsNifPath.c_str());
        saveNifPath        = ini.GetValue("Interaction", "sSaveNifPath",        saveNifPath.c_str());
        modsNifPath        = ini.GetValue("Interaction", "sModsNifPath",        modsNifPath.c_str());
        favNifPath         = ini.GetValue("Interaction", "sFavNifPath",         favNifPath.c_str());

        statusNifPath      = ini.GetValue("Interaction", "sStatusNifPath",      statusNifPath.c_str());
        nextPageNifPath    = ini.GetValue("Interaction", "sNextPageNifPath",    nextPageNifPath.c_str());
        homeNifPath        = ini.GetValue("Interaction", "sHomeNifPath",        homeNifPath.c_str());
        prevPageNifPath    = ini.GetValue("Interaction", "sPrevPageNifPath",    prevPageNifPath.c_str());
        bEnableButtonEditMode = ini.GetBoolValue("Interaction", "bEnableButtonEditMode", bEnableButtonEditMode);

        // [LaserPointer]
        reticleScaleX  = (float)ini.GetDoubleValue("LaserPointer", "fReticleScaleX",  reticleScaleX);
        reticleScaleY  = (float)ini.GetDoubleValue("LaserPointer", "fReticleScaleY",  reticleScaleY);
        reticleScaleZ  = (float)ini.GetDoubleValue("LaserPointer", "fReticleScaleZ",  reticleScaleZ);
        reticleOffsetX = (float)ini.GetDoubleValue("LaserPointer", "fReticleOffsetX", reticleOffsetX);
        reticleOffsetY = (float)ini.GetDoubleValue("LaserPointer", "fReticleOffsetY", reticleOffsetY);
        reticleOffsetZ = (float)ini.GetDoubleValue("LaserPointer", "fReticleOffsetZ", reticleOffsetZ);
        reticleRotX    = (float)ini.GetDoubleValue("LaserPointer", "fReticleRotX",    reticleRotX);
        reticleRotY    = (float)ini.GetDoubleValue("LaserPointer", "fReticleRotY",    reticleRotY);
        reticleRotZ    = (float)ini.GetDoubleValue("LaserPointer", "fReticleRotZ",    reticleRotZ);
        laserThickness = (float)ini.GetDoubleValue("LaserPointer", "fLaserThickness", laserThickness);
        laserScaleX    = (float)ini.GetDoubleValue("LaserPointer", "fLaserScaleX",    laserScaleX);
        laserScaleY    = (float)ini.GetDoubleValue("LaserPointer", "fLaserScaleY",    laserScaleY);
        laserScaleZ    = (float)ini.GetDoubleValue("LaserPointer", "fLaserScaleZ",    laserScaleZ);
        laserOffsetX   = (float)ini.GetDoubleValue("LaserPointer", "fLaserOffsetX",   laserOffsetX);
        laserOffsetY   = (float)ini.GetDoubleValue("LaserPointer", "fLaserOffsetY",   laserOffsetY);
        laserOffsetZ   = (float)ini.GetDoubleValue("LaserPointer", "fLaserOffsetZ",   laserOffsetZ);
        laserRotX      = (float)ini.GetDoubleValue("LaserPointer", "fLaserRotX",      laserRotX);
        laserRotY      = (float)ini.GetDoubleValue("LaserPointer", "fLaserRotY",      laserRotY);
        laserRotZ      = (float)ini.GetDoubleValue("LaserPointer", "fLaserRotZ",      laserRotZ);

        // [Labels]
        labelScale          = (float)ini.GetDoubleValue("Labels", "fLabelScale",          labelScale);
        labelXOffset        = (float)ini.GetDoubleValue("Labels", "fLabelXOffset",        labelXOffset);
        labelYOffset        = (float)ini.GetDoubleValue("Labels", "fLabelYOffset",        labelYOffset);
        labelZOffset        = (float)ini.GetDoubleValue("Labels", "fLabelZOffset",        labelZOffset);
        labelSpacing        = (float)ini.GetDoubleValue("Labels", "fLabelSpacing",        labelSpacing);
        labelYOffsetDynamic = (float)ini.GetDoubleValue("Labels", "fLabelYOffsetDynamic", labelYOffsetDynamic);

        // [Wiggle]
        enableWorldPinWiggle = ini.GetBoolValue("Wiggle", "bEnableWorldPinWiggle", enableWorldPinWiggle);
        wigglePosAmplitude   = (float)ini.GetDoubleValue("Wiggle", "fWigglePosAmplitude", wigglePosAmplitude);
        wiggleSideAmplitude  = (float)ini.GetDoubleValue("Wiggle", "fWiggleSideAmplitude", wiggleSideAmplitude);
        wiggleRotAmplitude   = (float)ini.GetDoubleValue("Wiggle", "fWiggleRotAmplitude", wiggleRotAmplitude);
        wiggleSpeed          = (float)ini.GetDoubleValue("Wiggle", "fWiggleSpeed", wiggleSpeed);

        // [Debug]
        debugMode = ini.GetBoolValue("Debug", "bDebugMode", debugMode);

        // [FixedButtons]
        auto loadBtn = [&](const char* prefix, float& px, float& py, float& pz,
                                               float& rx, float& ry, float& rz,
                                               float& sc) {
            auto k = [&](const char* suf) { return std::string(prefix) + suf; };
            px = (float)ini.GetDoubleValue("FixedButtons", k("PosX").c_str(), px);
            py = (float)ini.GetDoubleValue("FixedButtons", k("PosY").c_str(), py);
            pz = (float)ini.GetDoubleValue("FixedButtons", k("PosZ").c_str(), pz);
            rx = (float)ini.GetDoubleValue("FixedButtons", k("RotX").c_str(), rx);
            ry = (float)ini.GetDoubleValue("FixedButtons", k("RotY").c_str(), ry);
            rz = (float)ini.GetDoubleValue("FixedButtons", k("RotZ").c_str(), rz);
            sc = (float)ini.GetDoubleValue("FixedButtons", k("Scale").c_str(), sc);
        };
        loadBtn("fStatus", bStatusPosX, bStatusPosY, bStatusPosZ, bStatusRotX, bStatusRotY, bStatusRotZ, bStatusScale);
        loadBtn("fInv",    bInvPosX,    bInvPosY,    bInvPosZ,    bInvRotX,    bInvRotY,    bInvRotZ,    bInvScale);
        loadBtn("fMagic",  bMagicPosX,  bMagicPosY,  bMagicPosZ,  bMagicRotX,  bMagicRotY,  bMagicRotZ,  bMagicScale);
        loadBtn("fSys",    bSysPosX,    bSysPosY,    bSysPosZ,    bSysRotX,    bSysRotY,    bSysRotZ,    bSysScale);
        loadBtn("fSave",   bSavePosX,   bSavePosY,   bSavePosZ,   bSaveRotX,   bSaveRotY,   bSaveRotZ,   bSaveScale);
        loadBtn("fPrev",   bPrevPosX,   bPrevPosY,   bPrevPosZ,   bPrevRotX,   bPrevRotY,   bPrevRotZ,   bPrevScale);
        loadBtn("fHome",   bHomePosX,   bHomePosY,   bHomePosZ,   bHomeRotX,   bHomeRotY,   bHomeRotZ,   bHomeScale);
        loadBtn("fNext",   bNextPosX,   bNextPosY,   bNextPosZ,   bNextRotX,   bNextRotY,   bNextRotZ,   bNextScale);
        loadBtn("fMods",   bModsPosX,   bModsPosY,   bModsPosZ,   bModsRotX,   bModsRotY,   bModsRotZ,   bModsScale);
        loadBtn("fFav",    bFavPosX,    bFavPosY,    bFavPosZ,    bFavRotX,    bFavRotY,    bFavRotZ,    bFavScale);
        loadBtn("fAddFunc",bAddFuncPosX,bAddFuncPosY,bAddFuncPosZ,bAddFuncRotX,bAddFuncRotY,bAddFuncRotZ,bAddFuncScale);
        loadBtn("fGold",   bGoldPosX,   bGoldPosY,   bGoldPosZ,   bGoldRotX,   bGoldRotY,   bGoldRotZ,   bGoldScale);
        loadBtn("fMap",    bMapPosX,    bMapPosY,    bMapPosZ,    bMapRotX,    bMapRotY,    bMapRotZ,    bMapScale);
        loadBtn("fDev",    bDevPosX,    bDevPosY,    bDevPosZ,    bDevRotX,    bDevRotY,    bDevRotZ,    bDevScale);


        // Load fixed button labels and actions (INI has final word over JSON)
        bStatusLabel  = ini.GetValue("FixedButtons", "sStatusLabel",  bStatusLabel.c_str());
        bStatusAction = ini.GetValue("FixedButtons", "sStatusAction", bStatusAction.c_str());
        bInvLabel     = ini.GetValue("FixedButtons", "sInvLabel",     bInvLabel.c_str());
        bInvAction    = ini.GetValue("FixedButtons", "sInvAction",    bInvAction.c_str());
        bMagicLabel   = ini.GetValue("FixedButtons", "sMagicLabel",   bMagicLabel.c_str());
        bMagicAction  = ini.GetValue("FixedButtons", "sMagicAction",  bMagicAction.c_str());
        bSysLabel     = ini.GetValue("FixedButtons", "sSysLabel",     bSysLabel.c_str());
        bSysAction    = ini.GetValue("FixedButtons", "sSysAction",    bSysAction.c_str());
        bSaveLabel    = ini.GetValue("FixedButtons", "sSaveLabel",    bSaveLabel.c_str());
        bSaveAction   = ini.GetValue("FixedButtons", "sSaveAction",   bSaveAction.c_str());
        bModsLabel    = ini.GetValue("FixedButtons", "sModsLabel",    bModsLabel.c_str());
        bModsAction   = ini.GetValue("FixedButtons", "sModsAction",   bModsAction.c_str());
        bFavLabel     = ini.GetValue("FixedButtons", "sFavLabel",     bFavLabel.c_str());
        bFavAction    = ini.GetValue("FixedButtons", "sFavAction",    bFavAction.c_str());
        bMapLabel     = ini.GetValue("FixedButtons", "sMapLabel",     bMapLabel.c_str());
        bMapAction    = ini.GetValue("FixedButtons", "sMapAction",    bMapAction.c_str());
        bDevLabel     = ini.GetValue("FixedButtons", "sDevLabel",     bDevLabel.c_str());
        bDevAction    = ini.GetValue("FixedButtons", "sDevAction",    bDevAction.c_str());


        // [FixedWidgets]
        int fixedCount = (int)ini.GetLongValue("FixedWidgets", "iCount", 0);
        fixedWidgets.clear();
        for (int i = 0; i < fixedCount; ++i) {
            std::string p = "Widget" + std::to_string(i) + "_";
            FixedWidgetItem item;
            item.name = ini.GetValue("FixedWidgets", (p + "Name").c_str(), "");
            item.nifPath = ini.GetValue("FixedWidgets", (p + "Nif").c_str(), "");
            item.category = ini.GetValue("FixedWidgets", (p + "Category").c_str(), "Misc");
            item.formID = (uint32_t)ini.GetLongValue("FixedWidgets", (p + "FormID").c_str(), 0);
            item.posX = (float)ini.GetDoubleValue("FixedWidgets", (p + "PosX").c_str(), 0.0);
            item.posY = (float)ini.GetDoubleValue("FixedWidgets", (p + "PosY").c_str(), 0.0);
            item.posZ = (float)ini.GetDoubleValue("FixedWidgets", (p + "PosZ").c_str(), 0.0);
            item.rotX = (float)ini.GetDoubleValue("FixedWidgets", (p + "RotX").c_str(), 0.0);
            item.rotY = (float)ini.GetDoubleValue("FixedWidgets", (p + "RotY").c_str(), 0.0);
            item.rotZ = (float)ini.GetDoubleValue("FixedWidgets", (p + "RotZ").c_str(), 0.0);
            item.scale = (float)ini.GetDoubleValue("FixedWidgets", (p + "Scale").c_str(), 1.0);
            if (!item.nifPath.empty()) fixedWidgets.push_back(item);
        }
        showDevButton = ini.GetBoolValue("FixedButtons", "bShowDevButton", showDevButton);
        defaultPanelAction = ini.GetValue("FixedButtons", "sDefaultPanelAction", defaultPanelAction.c_str());

        // [Slots]
        for (int i = 0; i < kMaxSlots; ++i) {
            auto idx = std::to_string(i + 1);
            slotActions[i]   = ini.GetValue("Slots", ("sSlot" + idx).c_str(),           slotActions[i].c_str());
            slotTextures[i]  = ini.GetValue("Slots", ("sSlot" + idx + "Image").c_str(), slotTextures[i].c_str());
            slotNifs[i]      = ini.GetValue("Slots", ("sSlot" + idx + "Nif").c_str(),   slotNifs[i].c_str());
            slotLabels[i]    = ini.GetValue("Slots", ("sSlot" + idx + "Label").c_str(), slotLabels[i].c_str());
            slotSublabels[i] = ini.GetValue("Slots", ("sSlot" + idx + "Sublabel").c_str(), slotSublabels[i].c_str());
            slotPosX[i]      = (float)ini.GetDoubleValue("Slots", ("fSlot" + idx + "PosX").c_str(),      slotPosX[i]);
            slotPosY[i]      = (float)ini.GetDoubleValue("Slots", ("fSlot" + idx + "PosY").c_str(),      slotPosY[i]);
            slotPosZ[i]      = (float)ini.GetDoubleValue("Slots", ("fSlot" + idx + "PosZ").c_str(),      slotPosZ[i]);
            slotRotX[i]      = (float)ini.GetDoubleValue("Slots", ("fSlot" + idx + "RotX").c_str(),      slotRotX[i]);
            slotRotY[i]      = (float)ini.GetDoubleValue("Slots", ("fSlot" + idx + "RotY").c_str(),      slotRotY[i]);
            slotRotZ[i]      = (float)ini.GetDoubleValue("Slots", ("fSlot" + idx + "RotZ").c_str(),      slotRotZ[i]);
            slotScaleUser[i] = (float)ini.GetDoubleValue("Slots", ("fSlot" + idx + "ScaleUser").c_str(), slotScaleUser[i]);
            slotFloating[i]  = ini.GetBoolValue("Slots", ("bSlot" + idx + "Floating").c_str(),  slotFloating[i]);
        }



        // [MapMarker]
        bEnableMapMarker = ini.GetBoolValue("MapMarker", "bEnableMapMarker", bEnableMapMarker);
        mapWorldMinX     = (float)ini.GetDoubleValue("MapMarker", "fWorldMinX",    mapWorldMinX);
        mapWorldMaxX     = (float)ini.GetDoubleValue("MapMarker", "fWorldMaxX",    mapWorldMaxX);
        mapWorldMinY     = (float)ini.GetDoubleValue("MapMarker", "fWorldMinY",    mapWorldMinY);
        mapWorldMaxY     = (float)ini.GetDoubleValue("MapMarker", "fWorldMaxY",    mapWorldMaxY);
        mapMarkerScale   = (float)ini.GetDoubleValue("MapMarker", "fMarkerScale",  mapMarkerScale);
        mapWidth         = (float)ini.GetDoubleValue("MapMarker", "fMapWidth",     mapWidth);
        mapHeight        = (float)ini.GetDoubleValue("MapMarker", "fMapHeight",    mapHeight);
        bMapMarkerDynamicRotation = ini.GetBoolValue("MapMarker", "bDynamicRotation", bMapMarkerDynamicRotation);
        mapMarkerRotX             = (float)ini.GetDoubleValue("MapMarker", "fRotX",        mapMarkerRotX);
        mapMarkerRotY             = (float)ini.GetDoubleValue("MapMarker", "fRotY",        mapMarkerRotY);
        mapMarkerRotZ             = (float)ini.GetDoubleValue("MapMarker", "fRotZ",        mapMarkerRotZ);
        mapMarkerRotOffset        = (float)ini.GetDoubleValue("MapMarker", "fRotOffset",   mapMarkerRotOffset);
        mapMarkerOffsetX          = (float)ini.GetDoubleValue("MapMarker", "fOffsetX",     mapMarkerOffsetX);
        mapMarkerOffsetY          = (float)ini.GetDoubleValue("MapMarker", "fOffsetY",     mapMarkerOffsetY);
        mapMarkerOffsetZ          = (float)ini.GetDoubleValue("MapMarker", "fOffsetZ",     mapMarkerOffsetZ);
        mapMarkerNifPath = ini.GetValue("MapMarker", "sMarkerNifPath", mapMarkerNifPath.c_str());

        // [CategoryOverrides]
        // [CategoryOverrides] & [CategoryButtons]
        auto loadCategoryOffsets = [&](const char* section, std::map<std::string, ItemOffsetData, CaseInsensitiveComparator>& targetMap) {
            CSimpleIniA::TNamesDepend keys;
            if (ini.GetAllKeys(section, keys)) {
                for (const auto& key : keys) {
                    std::string val = ini.GetValue(section, key.pItem, "");
                    ItemOffsetData data;
                    if (sscanf_s(val.c_str(), "%f,%f,%f,%f,%f,%f,%f", &data.posX, &data.posY, &data.posZ, &data.rotX, &data.rotY, &data.rotZ, &data.scale) == 7) {
                        targetMap[key.pItem] = data;
                    }
                }
            }
        };
        loadCategoryOffsets("CategoryOverrides", categoryOverrides);
        loadCategoryOffsets("CategoryButtons",   categoryButtons);

        // [ItemOverrides] — keyed by FormID (hex string, e.g. "0x0001A2B3")
        {
            CSimpleIniA::TNamesDepend keys;
            if (ini.GetAllKeys("ItemOverrides", keys)) {
                for (const auto& key : keys) {
                    std::string val = ini.GetValue("ItemOverrides", key.pItem, "");
                    ItemOffsetData data;
                    if (sscanf_s(val.c_str(), "%f,%f,%f,%f,%f,%f,%f", &data.posX, &data.posY, &data.posZ, &data.rotX, &data.rotY, &data.rotZ, &data.scale) == 7) {
                        // Parse hex FormID (support both '0x' prefix and plain hex)
                        uint32_t fid = 0;
                        sscanf_s(key.pItem, "%i", &fid); // %i auto-detects 0x prefix
                        if (fid != 0) itemOverrides[fid] = data;
                    }
                }
            }
        }
    }

    void VRUISettings::save(const std::string& iniPath) const
    {
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(iniPath.c_str()); 



        // [MapMarker]
        ini.SetBoolValue("MapMarker",   "bEnableMapMarker", bEnableMapMarker);
        ini.SetDoubleValue("MapMarker", "fWorldMinX",       mapWorldMinX);
        ini.SetDoubleValue("MapMarker", "fWorldMaxX",       mapWorldMaxX);
        ini.SetDoubleValue("MapMarker", "fWorldMinY",       mapWorldMinY);
        ini.SetDoubleValue("MapMarker", "fWorldMaxY",       mapWorldMaxY);
        ini.SetDoubleValue("MapMarker", "fMarkerScale",     mapMarkerScale);
        ini.SetDoubleValue("MapMarker", "fMapWidth",        mapWidth);
        ini.SetDoubleValue("MapMarker", "fMapHeight",       mapHeight);
        ini.SetBoolValue("MapMarker",   "bDynamicRotation", bMapMarkerDynamicRotation);
        ini.SetDoubleValue("MapMarker", "fRotX",            mapMarkerRotX);
        ini.SetDoubleValue("MapMarker", "fRotY",            mapMarkerRotY);
        ini.SetDoubleValue("MapMarker", "fRotZ",            mapMarkerRotZ);
        ini.SetDoubleValue("MapMarker", "fRotOffset",       mapMarkerRotOffset);
        ini.SetDoubleValue("MapMarker", "fOffsetX",         mapMarkerOffsetX);
        ini.SetDoubleValue("MapMarker", "fOffsetY",         mapMarkerOffsetY);
        ini.SetDoubleValue("MapMarker", "fOffsetZ",         mapMarkerOffsetZ);
        ini.SetValue("MapMarker", "sMarkerNifPath", mapMarkerNifPath.c_str());

        // [General]
        ini.SetBoolValue("General", "bVerboseLogging", verboseLogging, "; Enable trace-level logging (default: false, very spammy)");
        ini.SetBoolValue("General", "bEditModeEnabled", editModeEnabled, "; Enable Pin to Dashboard and widget editing features");

        // [Activation]
        ini.SetLongValue  ("Activation", "iActivationMode",      static_cast<int>(activationMode), "; 0=Grip, 1=Trigger, 2=Thumbstick, 3=GripPlusThumbstick, 4=GripPlusY, 5=GripPlusB, 6=Hotkey8");
        ini.SetDoubleValue("Activation", "fHoldTimeGrip",        activationHoldTimeGrip,       "; Hold time (seconds) for Grip mode");
        ini.SetDoubleValue("Activation", "fHoldTimeTrigger",     activationHoldTimeTrigger,    "; Hold time for Trigger mode");
        ini.SetDoubleValue("Activation", "fHoldTimeThumbstick",  activationHoldTimeThumbstick, "; Hold time for Thumbstick mode");
        ini.SetDoubleValue("Activation", "fHoldTime",            activationHoldTime,           "; Legacy alias (kept for compatibility)");
        ini.SetBoolValue  ("Activation", "bUseLeftHandAsMenu", useLeftHandAsMenu,
            "; true = menu on left hand, false = right hand");
        ini.SetBoolValue  ("Activation", "bLastSavedLeftHand", useLeftHandAsMenu);

        // [Visual]
        ini.SetDoubleValue("Visual", "fMenuScale", (double)menuScale, "; Overall scale of ALL panels");
        
        // When saving, we must un-mirror the values if we are on the Right Hand
        // so that the INI file always stores "Left Hand Base" values.
        float outRotY    = menuRotY;
        float outRotZ    = menuRotZ;
        if (!useLeftHandAsMenu) {
            outRotY    = -outRotY;
            outRotZ    = -outRotZ;
        }

        ini.SetDoubleValue("Visual", "fMenuOffsetX", (double)menuOffsetX, "; X offset relative to hand (for ALL panels)");
        ini.SetDoubleValue("Visual", "fMenuOffsetY", (double)menuOffsetY, "; Y offset");
        ini.SetDoubleValue("Visual", "fMenuOffsetZ", (double)menuOffsetZ, "; Z offset");
        ini.SetDoubleValue("Visual", "fMenuRotX",    (double)menuRotX,    "; Panel rotation X (all panels)");
        ini.SetDoubleValue("Visual", "fMenuRotY",    (double)outRotY,    "; Panel rotation Y");
        ini.SetDoubleValue("Visual", "fMenuRotZ",    (double)outRotZ,    "; Panel rotation Z");
        ini.SetBoolValue("Visual", "bMenuFaceRotationZeroBased", true,
            "; true = fMenuRotZ 0 shows the front face of the board");
        ini.SetDoubleValue("Visual", "fContainerGridOffsetZ", containerGridOffsetZ, "; Dynamic grids Z offset");
        ini.SetBoolValue  ("Visual", "bEnableMenuLerp",bEnableMenuLerp,"; Smooth hand-follow");
        ini.SetDoubleValue("Visual", "fMenuLerpSpeed", fMenuLerpSpeed, "; Smoothing speed (higher = snappier)");

        // [Buttons]
        ini.SetDoubleValue("Buttons", "fButtonSpacingX",  buttonSpacingX,  "; Horizontal spacing between buttons");
        ini.SetDoubleValue("Buttons", "fButtonSpacingY",  buttonSpacingY,  "; Vertical spacing between buttons (rows)");
        ini.SetDoubleValue("Buttons", "fButtonMeshScale", buttonMeshScale, "; Scale of the button NIF mesh");
        ini.SetDoubleValue("Buttons", "fItemMeshScale",   itemMeshScale,   "; Base scale of in-button 3D item models");
        ini.SetDoubleValue("Buttons", "fItemWeaponScale", itemWeaponScale, "; Multiplier specifically for weapons/shields");
        ini.SetDoubleValue("Buttons", "fItemArmorScale",  itemArmorScale,  "; Multiplier specifically for armor/clothes");
        ini.SetDoubleValue("Buttons", "fItemPotionScale", itemPotionScale, "; Multiplier specifically for potions");
        ini.SetDoubleValue("Buttons", "fItemFoodScale",   itemFoodScale,   "; Multiplier specifically for food/ingredients");
        ini.SetDoubleValue("Buttons", "fItemMiscScale",   itemMiscScale,   "; Multiplier specifically for misc/books/clutter");
        ini.SetLongValue  ("Buttons", "iGridColumns",     gridColumns,     "; Number of grid columns for all containers");
        ini.SetLongValue  ("Buttons", "iGridPageSize",    gridPageSize,    "; Number of items per page in grid containers");

        // [Background]
        ini.SetBoolValue  ("Background", "bShowBackground", showBackground,    "; Show background tablet");
        ini.SetDoubleValue("Background", "fScale",          backgroundScale,   "; Tablet scale");
        ini.SetDoubleValue("Background", "fOffsetX",        backgroundOffsetX, "; Tablet X offset");
        ini.SetDoubleValue("Background", "fOffsetY",        backgroundOffsetY, "; Tablet Y offset");
        ini.SetDoubleValue("Background", "fOffsetZ",        backgroundOffsetZ, "; Tablet Z offset");
        ini.SetDoubleValue("Background", "fRotX",           backgroundRotX,    "; Tablet rotation X");
        ini.SetDoubleValue("Background", "fRotY",           backgroundRotY,    "; Tablet rotation Y");
        ini.SetDoubleValue("Background", "fRotZ",           backgroundRotZ,    "; Tablet rotation Z");

        // [LaserPointer]
        ini.SetDoubleValue("LaserPointer", "fReticleScaleX",  reticleScaleX,  "; Reticle scale X");
        ini.SetDoubleValue("LaserPointer", "fReticleScaleY",  reticleScaleY,  "; Reticle scale Y");
        ini.SetDoubleValue("LaserPointer", "fReticleScaleZ",  reticleScaleZ,  "; Reticle scale Z");
        ini.SetDoubleValue("LaserPointer", "fReticleOffsetX", reticleOffsetX, "; Reticle position X offset on panel");
        ini.SetDoubleValue("LaserPointer", "fReticleOffsetY", reticleOffsetY, "; Reticle Y offset (0=on surface)");
        ini.SetDoubleValue("LaserPointer", "fReticleOffsetZ", reticleOffsetZ, "; Reticle Z offset on panel");
        ini.SetDoubleValue("LaserPointer", "fReticleRotX",    reticleRotX,    "; Reticle rotation X");
        ini.SetDoubleValue("LaserPointer", "fReticleRotY",    reticleRotY,    "; Reticle rotation Y");
        ini.SetDoubleValue("LaserPointer", "fReticleRotZ",    reticleRotZ,    "; Reticle rotation Z");
        ini.SetDoubleValue("LaserPointer", "fLaserThickness", laserThickness, "; Laser beam base X/Y scale (thinness)");
        ini.SetDoubleValue("LaserPointer", "fLaserScaleX",    laserScaleX,    "; Laser additional X scale multiplier");
        ini.SetDoubleValue("LaserPointer", "fLaserScaleY",    laserScaleY,    "; Laser additional Y scale multiplier");
        ini.SetDoubleValue("LaserPointer", "fLaserScaleZ",    laserScaleZ,    "; Laser Z scale multiplier (affects length)");
        ini.SetDoubleValue("LaserPointer", "fLaserOffsetX",   laserOffsetX,   "; Laser X offset relative to hand");
        ini.SetDoubleValue("LaserPointer", "fLaserOffsetY",   laserOffsetY,   "; Laser Y offset relative to hand");
        ini.SetDoubleValue("LaserPointer", "fLaserOffsetZ",   laserOffsetZ,   "; Laser Z offset (shifts beam start)");
        ini.SetDoubleValue("LaserPointer", "fLaserRotX",      laserRotX,      "; Laser rotation X");
        ini.SetDoubleValue("LaserPointer", "fLaserRotY",      laserRotY,      "; Laser rotation Y");
        ini.SetDoubleValue("LaserPointer", "fLaserRotZ",      laserRotZ,      "; Laser rotation Z");

        // [Interaction]
        ini.SetDoubleValue("Interaction", "fRaycastMaxDistance", raycastMaxDistance, "; Laser max range");
        ini.SetBoolValue  ("Interaction", "bHapticOnHover",   hapticOnHover,   "; Haptic on hover");
        ini.SetBoolValue  ("Interaction", "bHapticOnPress",   hapticOnPress,   "; Haptic on press");
        ini.SetBoolValue  ("Interaction", "bEnableRotationSnapping", bEnableRotationSnapping, "; Snap rotation to nearby slots in edit mode");
        ini.SetDoubleValue("Interaction", "fSnapDistanceThreshold",  fSnapDistanceThreshold,  "; Distance threshold for snapping");
        ini.SetDoubleValue("Interaction", "fHapticIntensity", hapticIntensity, "; Haptic strength (0-1)");
        ini.SetDoubleValue("Interaction", "fHapticDuration",  hapticDuration,  "; Haptic duration in seconds");
        ini.SetDoubleValue("Interaction", "fEquipCooldown",   equipCooldown,   "; Seconds between gear swaps");
        ini.SetValue      ("Interaction", "sLaserNifPath",      laserNifPath.c_str(),      "; Custom laser NIF path");
        ini.SetValue      ("Interaction", "sBackgroundNifPath", backgroundNifPath.c_str(), "; Custom tablet NIF path");
        ini.SetValue      ("Interaction", "sMapNifPath",         mapNifPath.c_str(),         "; NIF used for Map button");
        ini.SetValue      ("Interaction", "sDevNifPath",         devNifPath.c_str(),         "; NIF used for Dev button");
        ini.SetValue      ("Interaction", "sMagicNifPath",       magicNifPath.c_str(),       "; NIF used for Magic items/categories");
        ini.SetValue      ("Interaction", "sInventoryNifPath",   inventoryNifPath.c_str(),   "; NIF used for Inventory button/items");
        ini.SetValue      ("Interaction", "sUnknownNifPath",     unknownNifPath.c_str(),     "; NIF used for Unknown/Fallback items");
        ini.SetValue      ("Interaction", "sSettingsNifPath",    settingsNifPath.c_str(),    "; NIF used for Settings button");
        ini.SetValue      ("Interaction", "sSaveNifPath",        saveNifPath.c_str(),        "; NIF used for Save button");
        ini.SetValue      ("Interaction", "sModsNifPath",        modsNifPath.c_str(),        "; NIF used for Mods button");
        ini.SetValue      ("Interaction", "sFavNifPath",         favNifPath.c_str(),         "; NIF used for Favorites button");

        ini.SetValue      ("Interaction", "sStatusNifPath",      statusNifPath.c_str(),      "; NIF used for Status button");
        ini.SetValue      ("Interaction", "sNextPageNifPath",    nextPageNifPath.c_str(),    "; NIF used for Next Page button");
        ini.SetValue      ("Interaction", "sHomeNifPath",        homeNifPath.c_str(),        "; NIF used for Home button");
        ini.SetValue      ("Interaction", "sPrevPageNifPath",    prevPageNifPath.c_str(),    "; NIF used for Previous Page button");
        ini.SetBoolValue  ("Interaction", "bEnableButtonEditMode", bEnableButtonEditMode,   "; Allow grab-and-move of buttons");

        // [Labels]
        ini.SetDoubleValue("Labels", "fLabelScale",          labelScale,          "; Label character scale");
        ini.SetDoubleValue("Labels", "fLabelXOffset",         labelXOffset,        "; Label X offset");
        ini.SetDoubleValue("Labels", "fLabelYOffset",         labelYOffset,        "; Label Y offset");
        ini.SetDoubleValue("Labels", "fLabelZOffset",         labelZOffset,        "; Label elevation above button");
        ini.SetDoubleValue("Labels", "fLabelSpacing",         labelSpacing,        "; Character spacing");
        ini.SetDoubleValue("Labels", "fLabelYOffsetDynamic",  labelYOffsetDynamic, "; Y offset for labels in dynamic containers (pushes text forward of 3D model)");

        // [Wiggle]
        ini.SetBoolValue  ("Wiggle", "bEnableWorldPinWiggle", enableWorldPinWiggle, "; Enable ambient wiggle for Pin to world magic");
        ini.SetDoubleValue("Wiggle", "fWigglePosAmplitude",   wigglePosAmplitude,   "; Vertical bob amplitude");
        ini.SetDoubleValue("Wiggle", "fWiggleSideAmplitude",  wiggleSideAmplitude,  "; Side drift amplitude");
        ini.SetDoubleValue("Wiggle", "fWiggleRotAmplitude",   wiggleRotAmplitude,   "; Rotation amplitude in degrees");
        ini.SetDoubleValue("Wiggle", "fWiggleSpeed",          wiggleSpeed,          "; Base wiggle speed");
        ini.Delete("Wiggle", "fHmdPinDeadzone");

        // [Debug]
        ini.SetBoolValue("Debug", "bDebugMode", debugMode, "; Show debug visuals (AABB boxes)");

        // [FixedButtons]
        auto saveBtn = [&](const char* prefix, float px, float py, float pz,
                                               float rx, float ry, float rz,
                                               float sc) {
            auto k = [&](const char* suf) { return std::string(prefix) + suf; };
            ini.SetDoubleValue("FixedButtons", k("PosX").c_str(),  px);
            ini.SetDoubleValue("FixedButtons", k("PosY").c_str(),  py);
            ini.SetDoubleValue("FixedButtons", k("PosZ").c_str(),  pz);
            ini.SetDoubleValue("FixedButtons", k("RotX").c_str(),  rx);
            ini.SetDoubleValue("FixedButtons", k("RotY").c_str(),  ry);
            ini.SetDoubleValue("FixedButtons", k("RotZ").c_str(),  rz);
            ini.SetDoubleValue("FixedButtons", k("Scale").c_str(), sc, "; Button scale (1.0 = default)");
        };
        saveBtn("fStatus", bStatusPosX, bStatusPosY, bStatusPosZ, bStatusRotX, bStatusRotY, bStatusRotZ, bStatusScale);
        saveBtn("fInv",    bInvPosX,    bInvPosY,    bInvPosZ,    bInvRotX,    bInvRotY,    bInvRotZ,    bInvScale);
        saveBtn("fMagic",  bMagicPosX,  bMagicPosY,  bMagicPosZ,  bMagicRotX,  bMagicRotY,  bMagicRotZ,  bMagicScale);
        saveBtn("fSys",    bSysPosX,    bSysPosY,    bSysPosZ,    bSysRotX,    bSysRotY,    bSysRotZ,    bSysScale);
        saveBtn("fSave",   bSavePosX,   bSavePosY,   bSavePosZ,   bSaveRotX,   bSaveRotY,   bSaveRotZ,   bSaveScale);
        saveBtn("fPrev",   bPrevPosX,   bPrevPosY,   bPrevPosZ,   bPrevRotX,   bPrevRotY,   bPrevRotZ,   bPrevScale);
        saveBtn("fHome",   bHomePosX,   bHomePosY,   bHomePosZ,   bHomeRotX,   bHomeRotY,   bHomeRotZ,   bHomeScale);
        saveBtn("fNext",   bNextPosX,   bNextPosY,   bNextPosZ,   bNextRotX,   bNextRotY,   bNextRotZ,   bNextScale);
        saveBtn("fMods",   bModsPosX,   bModsPosY,   bModsPosZ,   bModsRotX,   bModsRotY,   bModsRotZ,   bModsScale);
        saveBtn("fFav",    bFavPosX,    bFavPosY,    bFavPosZ,    bFavRotX,    bFavRotY,    bFavRotZ,    bFavScale);
        saveBtn("fAddFunc",bAddFuncPosX,bAddFuncPosY,bAddFuncPosZ,bAddFuncRotX,bAddFuncRotY,bAddFuncRotZ,bAddFuncScale);
        saveBtn("fGold",   bGoldPosX,   bGoldPosY,   bGoldPosZ,   bGoldRotX,   bGoldRotY,   bGoldRotZ,   bGoldScale);
        saveBtn("fMap",    bMapPosX,    bMapPosY,    bMapPosZ,    bMapRotX,    bMapRotY,    bMapRotZ,    bMapScale);
        saveBtn("fDev",    bDevPosX,    bDevPosY,    bDevPosZ,    bDevRotX,    bDevRotY,    bDevRotZ,    bDevScale);


        // [FixedWidgets]
        ini.SetLongValue("FixedWidgets", "iCount", (long)fixedWidgets.size());
        for (int i = 0; i < (int)fixedWidgets.size(); ++i) {
            std::string p = "Widget" + std::to_string(i) + "_";
            const auto& item = fixedWidgets[i];
            ini.SetValue("FixedWidgets", (p + "Name").c_str(), item.name.c_str());
            ini.SetValue("FixedWidgets", (p + "Nif").c_str(), item.nifPath.c_str());
            ini.SetValue("FixedWidgets", (p + "Category").c_str(), item.category.c_str());
            ini.SetLongValue("FixedWidgets", (p + "FormID").c_str(), (long)item.formID);
            ini.SetDoubleValue("FixedWidgets", (p + "PosX").c_str(), item.posX);
            ini.SetDoubleValue("FixedWidgets", (p + "PosY").c_str(), item.posY);
            ini.SetDoubleValue("FixedWidgets", (p + "PosZ").c_str(), item.posZ);
            ini.SetDoubleValue("FixedWidgets", (p + "RotX").c_str(), item.rotX);
            ini.SetDoubleValue("FixedWidgets", (p + "RotY").c_str(), item.rotY);
            ini.SetDoubleValue("FixedWidgets", (p + "RotZ").c_str(), item.rotZ);
            ini.SetDoubleValue("FixedWidgets", (p + "Scale").c_str(), item.scale);
        }

        // Fixed button labels and actions
        ini.SetValue("FixedButtons", "sStatusLabel",  bStatusLabel.c_str(),  "; Displayed name of the Perks/Status button");
        ini.SetValue("FixedButtons", "sStatusAction", bStatusAction.c_str(), "; Action: StatsMenu or any panel name");
        ini.SetValue("FixedButtons", "sInvLabel",     bInvLabel.c_str(),     "; Displayed name of the Inventory button");
        ini.SetValue("FixedButtons", "sInvAction",    bInvAction.c_str(),    "; Action: InventoryPanel or menu name");
        ini.SetValue("FixedButtons", "sMagicLabel",   bMagicLabel.c_str(),   "; Displayed name of the Magic button");
        ini.SetValue("FixedButtons", "sMagicAction",  bMagicAction.c_str(),  "; Action: MagicPanel or menu name");
        ini.SetValue("FixedButtons", "sSysLabel",     bSysLabel.c_str(),     "; Displayed name of the System button");
        ini.SetValue("FixedButtons", "sSysAction",    bSysAction.c_str(),    "; Action: MCM_Panel or menu name");
        ini.SetValue("FixedButtons", "sSaveLabel",    bSaveLabel.c_str(),    "; Displayed name of the Save button");
        ini.SetValue("FixedButtons", "sSaveAction",   bSaveAction.c_str(),   "; Action: QuickSave");
        ini.SetValue("FixedButtons", "sModsLabel",    bModsLabel.c_str(),    "; Displayed name of the Mods button");
        ini.SetValue("FixedButtons", "sModsAction",   bModsAction.c_str(),   "; Action: ModsPanel");
        ini.SetValue("FixedButtons", "sFavLabel",     bFavLabel.c_str(),     "; Displayed name of the Favorites button");
        ini.SetValue("FixedButtons", "sFavAction",    bFavAction.c_str(),    "; Action: FavoritesPanel");
        ini.SetValue("FixedButtons", "sMapLabel",     bMapLabel.c_str(),     "; Displayed name of the Map button");
        ini.SetValue("FixedButtons", "sMapAction",    bMapAction.c_str(),    "; Action: MapMenu");
        ini.SetValue("FixedButtons", "sDevLabel",     bDevLabel.c_str(),     "; Displayed name of the Dev button");
        ini.SetValue("FixedButtons", "sDevAction",    bDevAction.c_str(),    "; Action: DevPanel");
        ini.SetBoolValue("FixedButtons", "bShowDevButton", showDevButton,    "; Toggle: Show or hide the Dev button");

        ini.SetValue("FixedButtons", "sDefaultPanelAction", defaultPanelAction.c_str(), "; The action/panel to open when the menu opens");

        // [Slots]
        for (int i = 0; i < kMaxSlots; ++i) {
            auto idx = std::to_string(i + 1);
            ini.SetValue("Slots", ("sSlot" + idx).c_str(), slotActions[i].c_str());
            ini.SetValue("Slots", ("sSlot" + idx + "Image").c_str(),    slotTextures[i].c_str());
            ini.SetValue("Slots", ("sSlot" + idx + "Nif").c_str(),      slotNifs[i].c_str());
            ini.SetValue("Slots", ("sSlot" + idx + "Label").c_str(),    slotLabels[i].c_str());
            ini.SetValue("Slots", ("sSlot" + idx + "Sublabel").c_str(), slotSublabels[i].c_str());
            ini.SetDoubleValue("Slots", ("fSlot" + idx + "PosX").c_str(),      slotPosX[i]);
            ini.SetDoubleValue("Slots", ("fSlot" + idx + "PosY").c_str(),      slotPosY[i]);
            ini.SetDoubleValue("Slots", ("fSlot" + idx + "PosZ").c_str(),      slotPosZ[i]);
            ini.SetDoubleValue("Slots", ("fSlot" + idx + "RotX").c_str(),      slotRotX[i]);
            ini.SetDoubleValue("Slots", ("fSlot" + idx + "RotY").c_str(),      slotRotY[i]);
            ini.SetDoubleValue("Slots", ("fSlot" + idx + "RotZ").c_str(),      slotRotZ[i]);
            ini.SetDoubleValue("Slots", ("fSlot" + idx + "ScaleUser").c_str(), slotScaleUser[i]);
            ini.SetBoolValue  ("Slots", ("bSlot" + idx + "Floating").c_str(),  slotFloating[i]);
        }

        auto parentPath = std::filesystem::path(iniPath).parent_path();
        if (!parentPath.empty())
            std::filesystem::create_directories(parentPath);

        // [CategoryOverrides] & [CategoryButtons]
        ini.Delete("CategoryOverrides", nullptr);
        ini.Delete("CategoryButtons", nullptr);
        ini.Delete("ItemOverrides", nullptr);

        auto saveCategoryOffsets = [&](const char* section, const std::map<std::string, ItemOffsetData, CaseInsensitiveComparator>& sourceMap) {
            for (const auto& kv : sourceMap) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                         kv.second.posX, kv.second.posY, kv.second.posZ,
                         kv.second.rotX, kv.second.rotY, kv.second.rotZ, kv.second.scale);
                ini.SetValue(section, kv.first.c_str(), buf);
            }
        };
        saveCategoryOffsets("CategoryOverrides", categoryOverrides);

        // Save ItemOverrides (uint32_t FormID keys stored as hex)
        for (const auto& kv : itemOverrides) {
            char keyBuf[16];
            snprintf(keyBuf, sizeof(keyBuf), "0x%08X", kv.first);
            char buf[256];
            snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                     kv.second.posX, kv.second.posY, kv.second.posZ,
                     kv.second.rotX, kv.second.rotY, kv.second.rotZ, kv.second.scale);
            ini.SetValue("ItemOverrides", keyBuf, buf);
        }

        ini.SaveFile(iniPath.c_str());
        logger::info("DragonBoardVR: Settings saved to '{}'", iniPath);
    }
}
