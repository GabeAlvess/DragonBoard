#include "VRUISettings.h"

#include <CLIBUtil/simpleINI.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <initializer_list>

namespace vrui
{
    namespace
    {
        std::string GetSiblingIniPath(const std::string& mainIniPath, const char* fileName)
        {
            const auto parent = std::filesystem::path(mainIniPath).parent_path();
            return (parent / fileName).string();
        }

        void CopyIniKey(
            const CSimpleIniA& source,
            CSimpleIniA& destination,
            const char* section,
            const char* key)
        {
            const auto* value = source.GetValue(section, key, nullptr);
            if (value) destination.SetValue(section, key, value);
        }

        void CopyIniKeyAs(
            const CSimpleIniA& source,
            CSimpleIniA& destination,
            const char* sourceSection,
            const char* destinationSection,
            const char* key)
        {
            const auto* value = source.GetValue(sourceSection, key, nullptr);
            if (value) destination.SetValue(destinationSection, key, value);
        }

        void CopyIniSection(
            const CSimpleIniA& source,
            CSimpleIniA& destination,
            const char* section)
        {
            CSimpleIniA::TNamesDepend keys;
            if (!source.GetAllKeys(section, keys)) return;
            for (const auto& key : keys) {
                const auto* value = source.GetValue(section, key.pItem, nullptr);
                if (value) destination.SetValue(section, key.pItem, value, key.pComment);
            }
        }

        std::string NormalizeEntranceStyle(std::string_view value)
        {
            std::string compact;
            compact.reserve(value.size());
            for (const unsigned char character : value) {
                if (std::isalnum(character)) {
                    compact.push_back(static_cast<char>(std::tolower(character)));
                }
            }

            if (compact == "reverseradial") return "reverse radial";
            if (compact == "fade" || compact == "instant") return "Fade";
            if (compact == "lefttoright") return "leftToRight";
            if (compact == "righttoleft") return "rightToLeft";
            return "radial";
        }

        void ReflectMenuPoseAcrossHands(
            float& offsetX,
            float& rotationY,
            float& rotationZ)
        {
            // Reflection across the local YZ plane. For an XYZ Euler rotation
            // this preserves X while reversing the Y and Z components.
            offsetX = -offsetX;
            rotationY = -rotationY;
            rotationZ = -rotationZ;
        }
    }

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
        const std::array<const char*, kMaxSlots> defaultActions{
            "none", "Wait", "TweenMenu", "None", "None", "None", "Journal", "None"
        };
        const std::array<const char*, kMaxSlots> defaultLabels{
            "Save", "Wait", "TweenMenu", "Inventory", "", "", "Journal", ""
        };
        const std::array<const char*, kMaxSlots> defaultSublabels{
            "", "", "Menu", "", "", "", "", ""
        };
        const std::array<float, kMaxSlots> defaultPosX{
            5.0f, 12.0f, 9.5f, 6.0f, -6.278928f, -9.0f, 7.25f, 0.0f
        };
        const std::array<float, kMaxSlots> defaultPosY{
            -1.0f, -1.0f, -1.0f, -0.5f, -0.4158f, -0.5f, -1.0f, 0.0f
        };
        const std::array<float, kMaxSlots> defaultPosZ{
            -10.25f, -10.25f, -10.25f, -10.0f, -10.885161f, -10.0f, -10.25f, 0.0f
        };
        const std::array<bool, kMaxSlots> defaultFloating{
            true, true, true, false, false, false, true, false
        };
        for (int i = 0; i < kMaxSlots; ++i) {
            slotActions[i] = defaultActions[i];
            slotLabels[i] = defaultLabels[i];
            slotSublabels[i] = defaultSublabels[i];
            slotPosX[i] = defaultPosX[i];
            slotPosY[i] = defaultPosY[i];
            slotPosZ[i] = defaultPosZ[i];
            slotFloating[i] = defaultFloating[i];
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

    bool VRUISettings::isPoseMirroredForHand(bool leftHand) const
    {
        return leftHand == _nativeLeftHandedMode;
    }

    bool VRUISettings::isMenuPoseMirrored() const
    {
        return isPoseMirroredForHand(useLeftHandAsMenu);
    }

    bool VRUISettings::isTutorialComplete(TutorialId tutorial) const
    {
        const auto bit = 1u << static_cast<std::uint8_t>(tutorial);
        return (tutorialCompletionMask & bit) != 0;
    }

    void VRUISettings::setTutorialComplete(TutorialId tutorial, bool complete)
    {
        const auto bit = 1u << static_cast<std::uint8_t>(tutorial);
        if (complete) {
            tutorialCompletionMask |= bit;
        } else {
            tutorialCompletionMask &= ~bit;
        }
    }

    void VRUISettings::resetTutorialProgress()
    {
        tutorialCompletionMask = 0;
    }

    void VRUISettings::setUseLeftHandAsMenu(
        bool useLeftHand,
        bool nativeLeftHandedMode)
    {
        const bool wasMirrored = isMenuPoseMirrored();
        const bool willBeMirrored = useLeftHand == nativeLeftHandedMode;

        useLeftHandAsMenu = useLeftHand;
        _nativeLeftHandedMode = nativeLeftHandedMode;
        if (wasMirrored != willBeMirrored) {
            ReflectMenuPoseAcrossHands(menuOffsetX, menuRotY, menuRotZ);
        }
    }

    std::string VRUISettings::getDefaultIniPath()
    {
        return "Data/SKSE/Plugins/DragonBoardVR.ini";
    }

    std::string VRUISettings::getDefaultLayoutIniPath()
    {
        return "Data/SKSE/Plugins/DragonBoardVR_Layout.ini";
    }

    std::string VRUISettings::getDefaultStateIniPath()
    {
        return "Data/SKSE/Plugins/DragonBoardVR_State.ini";
    }

    void VRUISettings::load(const std::string& iniPath)
    {
        const bool showTutorialsBeforeLoad = showTutorials;
        CSimpleIniA ini;
        ini.SetUnicode();

        const auto layoutPath = GetSiblingIniPath(iniPath, "DragonBoardVR_Layout.ini");
        const auto statePath = GetSiblingIniPath(iniPath, "DragonBoardVR_State.ini");
        CSimpleIniA layoutIni;
        CSimpleIniA stateIni;
        layoutIni.SetUnicode();
        stateIni.SetUnicode();
        const bool layoutExists = layoutIni.LoadFile(layoutPath.c_str()) >= 0;
        const bool stateExists = stateIni.LoadFile(statePath.c_str()) >= 0;

        if (ini.LoadFile(iniPath.c_str()) < 0) {
            logger::trace("DragonBoardVR: No INI file found at '{}', using defaults", iniPath);
            save(iniPath);
            return;
        }

        logger::trace("DragonBoardVR: Loading settings from '{}'", iniPath);

        // These sections can be edited or cleared while the plugin is running.
        // Rebuild them from the file so a hot reload cannot retain deleted keys.
        categoryOverrides.clear();
        stableItemOverrides.clear();
        itemOverrides.clear();

        // [General]
        verboseLogging = ini.GetBoolValue("General", "bVerboseLogging", verboseLogging);
        editModeEnabled = ini.GetBoolValue("General", "bEditModeEnabled", editModeEnabled);
        lockPins = ini.GetBoolValue("General", "bLockPins", lockPins);
        uiLanguage = ini.GetValue("Interface", "sLanguage", uiLanguage.c_str());
        showTutorials = ini.GetBoolValue(
            "General", "bShowTutorials", showTutorials);

        // [Combat]
        const bool combatEnabledMissing = !ini.KeyExists("Combat", "bSlowTimeOnOpen");
        const bool combatMultiplierMissing = !ini.KeyExists("Combat", "fSlowTimeMultiplier");
        slowTimeOnOpen = ini.GetBoolValue("Combat", "bSlowTimeOnOpen", slowTimeOnOpen);
        slowTimeMultiplier = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Combat", "fSlowTimeMultiplier", slowTimeMultiplier)),
            0.05f,
            1.0f);
        if (combatEnabledMissing) {
            ini.SetBoolValue(
                "Combat", "bSlowTimeOnOpen", slowTimeOnOpen,
                "; Slow time only when DragonBoard is opened while the player is in combat");
        }
        if (combatMultiplierMissing) {
            ini.SetDoubleValue(
                "Combat", "fSlowTimeMultiplier", slowTimeMultiplier,
                "; Game-time multiplier while DragonBoard is open in combat (0.05 to 1.0)");
        }
        if ((combatEnabledMissing || combatMultiplierMissing) &&
            ini.SaveFile(iniPath.c_str()) < 0) {
            logger::warn("DragonBoardVR: Failed to add [Combat] defaults to '{}'.", iniPath);
        }

        // [RmlUi]
        rmlRenderOnDirty = ini.GetBoolValue("RmlUi", "bRenderOnDirty", rmlRenderOnDirty);
        rmlMaxActiveFPS = std::clamp(
            static_cast<int>(ini.GetLongValue("RmlUi", "iMaxActiveFPS", rmlMaxActiveFPS)),
            15,
            240);
        rmlEntranceAnimation = ini.GetBoolValue(
            "RmlUi", "bEntranceAnimation", rmlEntranceAnimation);
        rmlEntranceStyle = NormalizeEntranceStyle(
            ini.GetValue("RmlUi", "animationstyle", rmlEntranceStyle.c_str()));
        rmlEntranceDuration = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "RmlUi", "fEntranceDuration", rmlEntranceDuration)),
            0.05f,
            2.0f);
        rmlEntranceFeather = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "RmlUi", "fEntranceFeather", rmlEntranceFeather)),
            0.0f,
            0.5f);
        const int requestedRmlWidth = static_cast<int>(
            ini.GetLongValue("RmlUi", "iRenderWidth", rmlRenderWidth));
        const int requestedRmlHeight = static_cast<int>(
            ini.GetLongValue("RmlUi", "iRenderHeight", rmlRenderHeight));
        if (requestedRmlWidth >= 320 && requestedRmlWidth <= 4096 &&
            requestedRmlHeight >= 180 && requestedRmlHeight <= 2304 &&
            requestedRmlWidth * 9 == requestedRmlHeight * 16) {
            rmlRenderWidth = requestedRmlWidth;
            rmlRenderHeight = requestedRmlHeight;
        } else {
            rmlRenderWidth = 1920;
            rmlRenderHeight = 1080;
            logger::warn(
                "DragonBoardVR: invalid RmlUi render size {}x{}; using 1920x1080 (16:9 required).",
                requestedRmlWidth,
                requestedRmlHeight);
        }

        // [Activation]
        activationMode = static_cast<ActivationMode>(ini.GetLongValue("Activation", "iActivationMode", static_cast<int>(activationMode)));
        activationHoldTimeGrip       = (float)ini.GetDoubleValue("Activation", "fHoldTimeGrip",       activationHoldTimeGrip);
        activationHoldTimeTrigger    = (float)ini.GetDoubleValue("Activation", "fHoldTimeTrigger",    activationHoldTimeTrigger);
        activationHoldTimeThumbstick = (float)ini.GetDoubleValue("Activation", "fHoldTimeThumbstick", activationHoldTimeThumbstick);
        // Legacy alias: overrides the new per-mode values only if explicitly set in INI
        activationHoldTime = (float)ini.GetDoubleValue("Activation", "fHoldTime", activationHoldTime);
        useLeftHandAsMenu = ini.GetBoolValue("Activation", "bUseLeftHandAsMenu", useLeftHandAsMenu);
        
        // [Visual]
        if (layoutExists) {
            // Split configs store the complete hand-relative board transform in
            // DragonBoardVR_Layout.ini. Start from the left-hand defaults so
            // older split layouts that omit a field remain compatible.
            menuOffsetX = 1.0f;
            menuOffsetY = -17.0f;
            menuOffsetZ = -3.5f;
            menuRotX = -10.0f;
            menuRotY = 36.0f;
            menuRotZ = 85.0f;
            containerGridOffsetZ = 0.42f;
        }
        menuScale       = (float)ini.GetDoubleValue("Visual", "fMenuScale",   menuScale);
        // Other pose keys removed from the split INIs stay supported by the backend,
        // but only a genuinely old monolithic INI may override their defaults.
        // In particular, never run the legacy 180-degree migration against a
        // missing key: doing so rotates the new internal default to its back face.
        if (!layoutExists) {
            menuOffsetX     = (float)ini.GetDoubleValue("Visual", "fMenuOffsetX", menuOffsetX);
            menuOffsetY     = (float)ini.GetDoubleValue("Visual", "fMenuOffsetY", menuOffsetY);
            menuOffsetZ     = (float)ini.GetDoubleValue("Visual", "fMenuOffsetZ", menuOffsetZ);
            menuRotX        = (float)ini.GetDoubleValue("Visual", "fMenuRotX",    menuRotX);
            menuRotY        = (float)ini.GetDoubleValue("Visual", "fMenuRotY",    menuRotY);
            menuRotZ        = (float)ini.GetDoubleValue("Visual", "fMenuRotZ",    menuRotZ);
            containerGridOffsetZ = (float)ini.GetDoubleValue("Visual", "fContainerGridOffsetZ", containerGridOffsetZ);

            const bool hasLegacyMenuRotation = ini.KeyExists("Visual", "fMenuRotZ");
            if (hasLegacyMenuRotation) {
                menuRotZ = std::fmod(menuRotZ - 180.0f, 360.0f);
                if (menuRotZ <= -180.0f) menuRotZ += 360.0f;
                if (menuRotZ > 180.0f) menuRotZ -= 360.0f;
                logger::info(
                    "DragonBoardVR: Migrated legacy menu rotation to the current front-face convention (fMenuRotZ={:.1f}).",
                    menuRotZ);
            }
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
        normalizeItemVisuals = ini.GetBoolValue("Buttons", "bNormalizeItemVisuals", normalizeItemVisuals);
        useNifInventoryMarkerRotation = ini.GetBoolValue(
            "Buttons", "bUseNifInventoryMarkerRotation", useNifInventoryMarkerRotation);
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
        enableFingerTouch = ini.GetBoolValue(
            "Interaction", "bEnableFingerTouch", enableFingerTouch);
        fingerTouchTipExtension = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchTipExtension", fingerTouchTipExtension)),
            -10.0f,
            10.0f);
        fingerTouchOffsetX = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchOffsetX", fingerTouchOffsetX)),
            -10.0f,
            10.0f);
        fingerTouchOffsetY = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchOffsetY", fingerTouchOffsetY)),
            -10.0f,
            10.0f);
        fingerTouchOffsetZ = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchOffsetZ", fingerTouchOffsetZ)),
            -10.0f,
            10.0f);
        fingerTouchEnterDistance = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchEnterDistance", fingerTouchEnterDistance)),
            2.0f,
            40.0f);
        fingerTouchExitDistance = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchExitDistance", fingerTouchExitDistance)),
            fingerTouchEnterDistance + 0.5f,
            60.0f);
        fingerTouchHoverDistance = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchHoverDistance", fingerTouchHoverDistance)),
            0.5f,
            fingerTouchEnterDistance);
        fingerTouchPressDistance = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchPressDistance", fingerTouchPressDistance)),
            0.05f,
            fingerTouchHoverDistance);
        fingerTouchReleaseDistance = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchReleaseDistance", fingerTouchReleaseDistance)),
            fingerTouchPressDistance,
            fingerTouchHoverDistance);
        fingerTouchScrollDeadzone = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Interaction", "fFingerTouchScrollDeadzone", fingerTouchScrollDeadzone)),
            10.0f,
            200.0f);
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
        homeNifPath        = ini.GetValue("Interaction", "sHomeNifPath",        homeNifPath.c_str());
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
        fingerTrackingProbe = ini.GetBoolValue(
            "Debug", "bFingerTrackingProbe", fingerTrackingProbe);
        fingerTrackingProbeMarkers = ini.GetBoolValue(
            "Debug", "bFingerTrackingProbeMarkers", fingerTrackingProbeMarkers);
        fingerTrackingMarkerScale = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Debug", "fFingerTrackingMarkerScale", fingerTrackingMarkerScale)),
            0.05f,
            5.0f);
        fingerTrackingTipExtension = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Debug", "fFingerTrackingTipExtension", fingerTrackingTipExtension)),
            -10.0f,
            10.0f);
        fingerTrackingProbeInterval = std::clamp(
            static_cast<float>(ini.GetDoubleValue(
                "Debug", "fFingerTrackingProbeInterval", fingerTrackingProbeInterval)),
            0.10f,
            5.0f);

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
        loadBtn("fHome",   bHomePosX,   bHomePosY,   bHomePosZ,   bHomeRotX,   bHomeRotY,   bHomeRotZ,   bHomeScale);
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
        // Preserve the legacy INI keys (and therefore the existing transform),
        // while migrating the retired Favorites button to the RmlUi Journal.
        bFavLabel = "Journal";
        bFavAction = "Journal";
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
        bEnableQuestMarker = ini.GetBoolValue("QuestMarker", "bEnableQuestMarker", bEnableQuestMarker);
        questMarkerScale = (float)ini.GetDoubleValue("QuestMarker", "fMarkerScale", questMarkerScale);
        questMarkerRotX = (float)ini.GetDoubleValue("QuestMarker", "fRotX", questMarkerRotX);
        questMarkerRotY = (float)ini.GetDoubleValue("QuestMarker", "fRotY", questMarkerRotY);
        questMarkerRotZ = (float)ini.GetDoubleValue("QuestMarker", "fRotZ", questMarkerRotZ);
        for (std::size_t slot = 0; slot < kQuestMarkerSlotCount; ++slot) {
            const auto suffix = slot == 0 ? std::string{} : std::to_string(slot + 1);
            questMarkerLastFormIDs[slot] = static_cast<std::uint32_t>(
                ini.GetLongValue(
                    "QuestMarker",
                    ("iLastFormID" + suffix).c_str(),
                    questMarkerLastFormIDs[slot]));
            questMarkerLastQuestInstanceIDs[slot] = static_cast<std::uint32_t>(
                ini.GetLongValue(
                    "QuestMarker",
                    ("iLastQuestInstanceID" + suffix).c_str(),
                    questMarkerLastQuestInstanceIDs[slot]));
            questMarkerLastObjectiveInstanceIDs[slot] = static_cast<std::uint32_t>(
                ini.GetLongValue(
                    "QuestMarker",
                    ("iLastObjectiveInstanceID" + suffix).c_str(),
                    questMarkerLastObjectiveInstanceIDs[slot]));
            questMarkerLastObjectiveIDs[slot] = static_cast<std::uint16_t>(
                ini.GetLongValue(
                    "QuestMarker",
                    ("iLastObjectiveID" + suffix).c_str(),
                    questMarkerLastObjectiveIDs[slot]));
        }
        for (std::size_t i = 0; i < mapCalibrationPoints.size(); ++i) {
            const auto prefix = "Point" + std::to_string(i + 1);
            auto& point = mapCalibrationPoints[i];
            (void)GetMapCalibrationLandmarkUv(i, point.mapU, point.mapV);
            point.valid = ini.GetBoolValue("MapCalibration", ("b" + prefix + "Valid").c_str(), point.valid);
            point.worldX = (float)ini.GetDoubleValue("MapCalibration", ("f" + prefix + "WorldX").c_str(), point.worldX);
            point.worldY = (float)ini.GetDoubleValue("MapCalibration", ("f" + prefix + "WorldY").c_str(), point.worldY);
            point.mapU = (float)ini.GetDoubleValue("MapCalibration", ("f" + prefix + "MapU").c_str(), point.mapU);
            point.mapV = (float)ini.GetDoubleValue("MapCalibration", ("f" + prefix + "MapV").c_str(), point.mapV);
        }

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

        // [ItemOverrides] supports both stable "Plugin.esp|LOCAL_FORM_ID"
        // keys and legacy full runtime FormIDs.
        {
            CSimpleIniA::TNamesDepend keys;
            if (ini.GetAllKeys("ItemOverrides", keys)) {
                for (const auto& key : keys) {
                    std::string val = ini.GetValue("ItemOverrides", key.pItem, "");
                    ItemOffsetData data;
                    if (sscanf_s(val.c_str(), "%f,%f,%f,%f,%f,%f,%f", &data.posX, &data.posY, &data.posZ, &data.rotX, &data.rotY, &data.rotZ, &data.scale) == 7) {
                        const std::string overrideKey = key.pItem;
                        if (overrideKey.find('|') != std::string::npos) {
                            stableItemOverrides[overrideKey] = data;
                        } else {
                            // Parse hex FormID (support both '0x' prefix and plain hex)
                            uint32_t fid = 0;
                            sscanf_s(key.pItem, "%i", &fid); // %i auto-detects 0x prefix
                            if (fid != 0) itemOverrides[fid] = data;
                        }
                    }
                }
            }
        }

        if (layoutExists) {
            menuScale = (float)layoutIni.GetDoubleValue("Visual", "fMenuScale", menuScale);
            menuOffsetX = (float)layoutIni.GetDoubleValue(
                "Visual", "fMenuOffsetX", menuOffsetX);
            menuOffsetY = (float)layoutIni.GetDoubleValue(
                "Visual", "fMenuOffsetY", menuOffsetY);
            menuOffsetZ = (float)layoutIni.GetDoubleValue(
                "Visual", "fMenuOffsetZ", menuOffsetZ);
            menuRotX = (float)layoutIni.GetDoubleValue(
                "Visual", "fMenuRotX", menuRotX);
            menuRotY = (float)layoutIni.GetDoubleValue(
                "Visual", "fMenuRotY", menuRotY);
            menuRotZ = (float)layoutIni.GetDoubleValue(
                "Visual", "fMenuRotZ", menuRotZ);
            bEnableMenuLerp = layoutIni.GetBoolValue(
                "Visual", "bEnableMenuLerp", bEnableMenuLerp);
            fMenuLerpSpeed = (float)layoutIni.GetDoubleValue(
                "Visual", "fMenuLerpSpeed", fMenuLerpSpeed);

            itemWeaponScale = (float)layoutIni.GetDoubleValue(
                "Buttons", "fItemWeaponScale", itemWeaponScale);
            itemArmorScale = (float)layoutIni.GetDoubleValue(
                "Buttons", "fItemArmorScale", itemArmorScale);
            itemPotionScale = (float)layoutIni.GetDoubleValue(
                "Buttons", "fItemPotionScale", itemPotionScale);
            itemFoodScale = (float)layoutIni.GetDoubleValue(
                "Buttons", "fItemFoodScale", itemFoodScale);
            itemMiscScale = (float)layoutIni.GetDoubleValue(
                "Buttons", "fItemMiscScale", itemMiscScale);
            normalizeItemVisuals = layoutIni.GetBoolValue(
                "Buttons", "bNormalizeItemVisuals", normalizeItemVisuals);

            backgroundScale = (float)layoutIni.GetDoubleValue(
                "Background", "fScale", backgroundScale);
            backgroundOffsetX = (float)layoutIni.GetDoubleValue(
                "Background", "fOffsetX", backgroundOffsetX);
            backgroundOffsetY = (float)layoutIni.GetDoubleValue(
                "Background", "fOffsetY", backgroundOffsetY);
            backgroundOffsetZ = (float)layoutIni.GetDoubleValue(
                "Background", "fOffsetZ", backgroundOffsetZ);
            backgroundRotX = (float)layoutIni.GetDoubleValue(
                "Background", "fRotX", backgroundRotX);
            backgroundRotY = (float)layoutIni.GetDoubleValue(
                "Background", "fRotY", backgroundRotY);
            backgroundRotZ = (float)layoutIni.GetDoubleValue(
                "Background", "fRotZ", backgroundRotZ);

            reticleScaleX = (float)layoutIni.GetDoubleValue(
                "LaserPointer", "fReticleScaleX", reticleScaleX);
            reticleScaleY = (float)layoutIni.GetDoubleValue(
                "LaserPointer", "fReticleScaleY", reticleScaleY);
            reticleScaleZ = (float)layoutIni.GetDoubleValue(
                "LaserPointer", "fReticleScaleZ", reticleScaleZ);
            laserNifPath = layoutIni.GetValue(
                "LaserPointer", "sLaserNifPath", laserNifPath.c_str());
            backgroundNifPath = layoutIni.GetValue(
                "LaserPointer", "sBackgroundNifPath", backgroundNifPath.c_str());
            mapNifPath = layoutIni.GetValue(
                "LaserPointer", "sMapNifPath", mapNifPath.c_str());
            devNifPath = layoutIni.GetValue(
                "LaserPointer", "sDevNifPath", devNifPath.c_str());
            magicNifPath = layoutIni.GetValue(
                "LaserPointer", "sMagicNifPath", magicNifPath.c_str());
            inventoryNifPath = layoutIni.GetValue(
                "LaserPointer", "sInventoryNifPath", inventoryNifPath.c_str());
            unknownNifPath = layoutIni.GetValue(
                "LaserPointer", "sUnknownNifPath", unknownNifPath.c_str());
            settingsNifPath = layoutIni.GetValue(
                "LaserPointer", "sSettingsNifPath", settingsNifPath.c_str());
            saveNifPath = layoutIni.GetValue(
                "LaserPointer", "sSaveNifPath", saveNifPath.c_str());
            modsNifPath = layoutIni.GetValue(
                "LaserPointer", "sModsNifPath", modsNifPath.c_str());
            favNifPath = layoutIni.GetValue(
                "LaserPointer", "sFavNifPath", favNifPath.c_str());
            statusNifPath = layoutIni.GetValue(
                "LaserPointer", "sStatusNifPath", statusNifPath.c_str());
            homeNifPath = layoutIni.GetValue(
                "LaserPointer", "sHomeNifPath", homeNifPath.c_str());
            bEnableButtonEditMode = layoutIni.GetBoolValue(
                "LaserPointer", "bEnableButtonEditMode", bEnableButtonEditMode);

            const auto loadLayoutButton = [&](const char* prefix,
                                                float& px, float& py, float& pz,
                                                float& rx, float& ry, float& rz,
                                                float& scale) {
                const auto key = [prefix](const char* suffix) {
                    return std::string(prefix) + suffix;
                };
                px = (float)layoutIni.GetDoubleValue(
                    "FixedButtons", key("PosX").c_str(), px);
                py = (float)layoutIni.GetDoubleValue(
                    "FixedButtons", key("PosY").c_str(), py);
                pz = (float)layoutIni.GetDoubleValue(
                    "FixedButtons", key("PosZ").c_str(), pz);
                rx = (float)layoutIni.GetDoubleValue(
                    "FixedButtons", key("RotX").c_str(), rx);
                ry = (float)layoutIni.GetDoubleValue(
                    "FixedButtons", key("RotY").c_str(), ry);
                rz = (float)layoutIni.GetDoubleValue(
                    "FixedButtons", key("RotZ").c_str(), rz);
                scale = (float)layoutIni.GetDoubleValue(
                    "FixedButtons", key("Scale").c_str(), scale);
            };
            loadLayoutButton("fStatus", bStatusPosX, bStatusPosY, bStatusPosZ,
                bStatusRotX, bStatusRotY, bStatusRotZ, bStatusScale);
            loadLayoutButton("fInv", bInvPosX, bInvPosY, bInvPosZ,
                bInvRotX, bInvRotY, bInvRotZ, bInvScale);
            loadLayoutButton("fMagic", bMagicPosX, bMagicPosY, bMagicPosZ,
                bMagicRotX, bMagicRotY, bMagicRotZ, bMagicScale);
            loadLayoutButton("fSys", bSysPosX, bSysPosY, bSysPosZ,
                bSysRotX, bSysRotY, bSysRotZ, bSysScale);
            loadLayoutButton("fSave", bSavePosX, bSavePosY, bSavePosZ,
                bSaveRotX, bSaveRotY, bSaveRotZ, bSaveScale);
            loadLayoutButton("fHome", bHomePosX, bHomePosY, bHomePosZ,
                bHomeRotX, bHomeRotY, bHomeRotZ, bHomeScale);
            loadLayoutButton("fMods", bModsPosX, bModsPosY, bModsPosZ,
                bModsRotX, bModsRotY, bModsRotZ, bModsScale);
            loadLayoutButton("fFav", bFavPosX, bFavPosY, bFavPosZ,
                bFavRotX, bFavRotY, bFavRotZ, bFavScale);
            loadLayoutButton("fAddFunc", bAddFuncPosX, bAddFuncPosY, bAddFuncPosZ,
                bAddFuncRotX, bAddFuncRotY, bAddFuncRotZ, bAddFuncScale);
            loadLayoutButton("fGold", bGoldPosX, bGoldPosY, bGoldPosZ,
                bGoldRotX, bGoldRotY, bGoldRotZ, bGoldScale);
            loadLayoutButton("fMap", bMapPosX, bMapPosY, bMapPosZ,
                bMapRotX, bMapRotY, bMapRotZ, bMapScale);
            loadLayoutButton("fDev", bDevPosX, bDevPosY, bDevPosZ,
                bDevRotX, bDevRotY, bDevRotZ, bDevScale);

            bStatusLabel = layoutIni.GetValue(
                "FixedButtons", "sStatusLabel", bStatusLabel.c_str());
            bStatusAction = layoutIni.GetValue(
                "FixedButtons", "sStatusAction", bStatusAction.c_str());
            bInvLabel = layoutIni.GetValue(
                "FixedButtons", "sInvLabel", bInvLabel.c_str());
            bInvAction = layoutIni.GetValue(
                "FixedButtons", "sInvAction", bInvAction.c_str());
            bMagicLabel = layoutIni.GetValue(
                "FixedButtons", "sMagicLabel", bMagicLabel.c_str());
            bMagicAction = layoutIni.GetValue(
                "FixedButtons", "sMagicAction", bMagicAction.c_str());
            bSysLabel = layoutIni.GetValue(
                "FixedButtons", "sSysLabel", bSysLabel.c_str());
            bSysAction = layoutIni.GetValue(
                "FixedButtons", "sSysAction", bSysAction.c_str());
            bSaveLabel = layoutIni.GetValue(
                "FixedButtons", "sSaveLabel", bSaveLabel.c_str());
            bSaveAction = layoutIni.GetValue(
                "FixedButtons", "sSaveAction", bSaveAction.c_str());
            bModsLabel = layoutIni.GetValue(
                "FixedButtons", "sModsLabel", bModsLabel.c_str());
            bModsAction = layoutIni.GetValue(
                "FixedButtons", "sModsAction", bModsAction.c_str());
            bMapLabel = layoutIni.GetValue(
                "FixedButtons", "sMapLabel", bMapLabel.c_str());
            bMapAction = layoutIni.GetValue(
                "FixedButtons", "sMapAction", bMapAction.c_str());
            bDevLabel = layoutIni.GetValue(
                "FixedButtons", "sDevLabel", bDevLabel.c_str());
            bDevAction = layoutIni.GetValue(
                "FixedButtons", "sDevAction", bDevAction.c_str());
            showDevButton = layoutIni.GetBoolValue(
                "FixedButtons", "bShowDevButton", showDevButton);
            defaultPanelAction = layoutIni.GetValue(
                "FixedButtons", "sDefaultPanelAction", defaultPanelAction.c_str());
            bFavLabel = "Journal";
            bFavAction = "Journal";

            for (int i = 0; i < kMaxSlots; ++i) {
                const auto index = std::to_string(i + 1);
                slotActions[i] = layoutIni.GetValue(
                    "Slots", ("sSlot" + index).c_str(), slotActions[i].c_str());
                slotTextures[i] = layoutIni.GetValue(
                    "Slots", ("sSlot" + index + "Image").c_str(), slotTextures[i].c_str());
                slotNifs[i] = layoutIni.GetValue(
                    "Slots", ("sSlot" + index + "Nif").c_str(), slotNifs[i].c_str());
                slotLabels[i] = layoutIni.GetValue(
                    "Slots", ("sSlot" + index + "Label").c_str(), slotLabels[i].c_str());
                slotSublabels[i] = layoutIni.GetValue(
                    "Slots", ("sSlot" + index + "Sublabel").c_str(), slotSublabels[i].c_str());
                slotPosX[i] = (float)layoutIni.GetDoubleValue(
                    "Slots", ("fSlot" + index + "PosX").c_str(), slotPosX[i]);
                slotPosY[i] = (float)layoutIni.GetDoubleValue(
                    "Slots", ("fSlot" + index + "PosY").c_str(), slotPosY[i]);
                slotPosZ[i] = (float)layoutIni.GetDoubleValue(
                    "Slots", ("fSlot" + index + "PosZ").c_str(), slotPosZ[i]);
                slotRotX[i] = (float)layoutIni.GetDoubleValue(
                    "Slots", ("fSlot" + index + "RotX").c_str(), slotRotX[i]);
                slotRotY[i] = (float)layoutIni.GetDoubleValue(
                    "Slots", ("fSlot" + index + "RotY").c_str(), slotRotY[i]);
                slotRotZ[i] = (float)layoutIni.GetDoubleValue(
                    "Slots", ("fSlot" + index + "RotZ").c_str(), slotRotZ[i]);
                slotScaleUser[i] = (float)layoutIni.GetDoubleValue(
                    "Slots", ("fSlot" + index + "ScaleUser").c_str(), slotScaleUser[i]);
                slotFloating[i] = layoutIni.GetBoolValue(
                    "Slots", ("bSlot" + index + "Floating").c_str(), slotFloating[i]);
            }
        }

        // The INI/layout pose is stored as a left-hand base. Reflect the lateral
        // translation and orientation once after all sources have been layered.
        if (isMenuPoseMirrored()) {
            ReflectMenuPoseAcrossHands(menuOffsetX, menuRotY, menuRotZ);
        }

        if (stateExists) {
            for (std::size_t i = 0; i < mapCalibrationPoints.size(); ++i) {
                const auto prefix = "Point" + std::to_string(i + 1);
                auto& point = mapCalibrationPoints[i];
                point.valid = stateIni.GetBoolValue(
                    "MapCalibration", ("b" + prefix + "Valid").c_str(), point.valid);
                point.worldX = (float)stateIni.GetDoubleValue(
                    "MapCalibration", ("f" + prefix + "WorldX").c_str(), point.worldX);
                point.worldY = (float)stateIni.GetDoubleValue(
                    "MapCalibration", ("f" + prefix + "WorldY").c_str(), point.worldY);
                point.mapU = (float)stateIni.GetDoubleValue(
                    "MapCalibration", ("f" + prefix + "MapU").c_str(), point.mapU);
                point.mapV = (float)stateIni.GetDoubleValue(
                    "MapCalibration", ("f" + prefix + "MapV").c_str(), point.mapV);
            }
        }

        bool tutorialStateChanged = false;
        if (stateExists) {
            tutorialsPreviouslyEnabled = stateIni.GetBoolValue(
                "Tutorials",
                "bPreviouslyEnabled",
                tutorialsPreviouslyEnabled);
            const bool legacyWelcomeComplete = stateIni.GetBoolValue(
                "Tutorials",
                "bWelcomeComplete",
                false);
            tutorialCompletionMask = static_cast<std::uint32_t>(
                stateIni.GetLongValue(
                    "Tutorials",
                    "uCompletedMask",
                    legacyWelcomeComplete ? 1L : 0L));
            if (showTutorials &&
                (!showTutorialsBeforeLoad || !tutorialsPreviouslyEnabled)) {
                resetTutorialProgress();
                tutorialPositionResetRequested = true;
                tutorialStateChanged = true;
                logger::info(
                    "DragonBoardVR: tutorials re-enabled; tutorial completion state reset.");
            }
            if (tutorialsPreviouslyEnabled != showTutorials) {
                tutorialsPreviouslyEnabled = showTutorials;
                tutorialStateChanged = true;
            }
        } else {
            tutorialsPreviouslyEnabled = showTutorials;
        }

        if (!layoutExists || !stateExists) {
            logger::info(
                "DragonBoardVR: Migrating split settings files (layoutExists={}, stateExists={}).",
                layoutExists,
                stateExists);
            save(iniPath);
        } else if (tutorialStateChanged) {
            save(iniPath);
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
        ini.SetBoolValue("QuestMarker", "bEnableQuestMarker", bEnableQuestMarker);
        ini.SetDoubleValue("QuestMarker", "fMarkerScale", questMarkerScale);
        ini.SetDoubleValue("QuestMarker", "fRotX", questMarkerRotX);
        ini.SetDoubleValue("QuestMarker", "fRotY", questMarkerRotY);
        ini.SetDoubleValue("QuestMarker", "fRotZ", questMarkerRotZ);
        for (std::size_t slot = 0; slot < kQuestMarkerSlotCount; ++slot) {
            const auto suffix = slot == 0 ? std::string{} : std::to_string(slot + 1);
            ini.SetLongValue(
                "QuestMarker",
                ("iLastFormID" + suffix).c_str(),
                questMarkerLastFormIDs[slot]);
            ini.SetLongValue(
                "QuestMarker",
                ("iLastQuestInstanceID" + suffix).c_str(),
                questMarkerLastQuestInstanceIDs[slot]);
            ini.SetLongValue(
                "QuestMarker",
                ("iLastObjectiveInstanceID" + suffix).c_str(),
                questMarkerLastObjectiveInstanceIDs[slot]);
            ini.SetLongValue(
                "QuestMarker",
                ("iLastObjectiveID" + suffix).c_str(),
                questMarkerLastObjectiveIDs[slot]);
        }
        for (std::size_t i = 0; i < mapCalibrationPoints.size(); ++i) {
            const auto prefix = "Point" + std::to_string(i + 1);
            const auto& point = mapCalibrationPoints[i];
            ini.SetBoolValue("MapCalibration", ("b" + prefix + "Valid").c_str(), point.valid);
            ini.SetDoubleValue("MapCalibration", ("f" + prefix + "WorldX").c_str(), point.worldX);
            ini.SetDoubleValue("MapCalibration", ("f" + prefix + "WorldY").c_str(), point.worldY);
            ini.SetDoubleValue("MapCalibration", ("f" + prefix + "MapU").c_str(), point.mapU);
            ini.SetDoubleValue("MapCalibration", ("f" + prefix + "MapV").c_str(), point.mapV);
        }

        // [General]
        ini.SetBoolValue("General", "bVerboseLogging", verboseLogging, "; Enable trace-level logging (default: false, very spammy)");
        ini.SetBoolValue("General", "bEditModeEnabled", editModeEnabled, "; Enable Pin to Dashboard and widget editing features");
        ini.SetBoolValue("General", "bLockPins", lockPins, "; Prevent pinned items from being edited, grabbed, or removed");

        // [Combat]
        ini.SetBoolValue(
            "Combat", "bSlowTimeOnOpen", slowTimeOnOpen,
            "; Slow time only when DragonBoard is opened while the player is in combat");
        ini.SetDoubleValue(
            "Combat", "fSlowTimeMultiplier", slowTimeMultiplier,
            "; Game-time multiplier while DragonBoard is open in combat (0.05 to 1.0)");

        ini.SetBoolValue(
            "General", "bShowTutorials", showTutorials,
            "; Show DragonBoard tutorials; changing false to true resets all tutorials");
        ini.SetValue(
            "Interface", "sLanguage", uiLanguage.c_str(),
            "; DragonBoard interface language code; JSON catalogs are discovered automatically");

        // [RmlUi]
        ini.SetBoolValue("RmlUi", "bRenderOnDirty", rmlRenderOnDirty,
            "; Reuse the last RmlUi texture while the active document is unchanged");
        ini.SetLongValue("RmlUi", "iMaxActiveFPS", rmlMaxActiveFPS,
            "; Maximum RmlUi texture refresh rate during pointer, scroll, or animation activity");
        ini.SetLongValue("RmlUi", "iRenderWidth", rmlRenderWidth,
            "; RmlUi render-target width; must form an exact 16:9 pair with iRenderHeight");
        ini.SetLongValue("RmlUi", "iRenderHeight", rmlRenderHeight,
            "; RmlUi render-target height; supported examples: 1920x1080, 1280x720, 960x540");
        ini.SetBoolValue("RmlUi", "bEntranceAnimation", rmlEntranceAnimation,
            "; Reveal the RmlUi page from the center whenever the Board opens");
        ini.SetValue("RmlUi", "animationstyle", rmlEntranceStyle.c_str(),
            "; animation preset: radial, reverse radial, Fade, leftToRight, rightToLeft");
        ini.SetDoubleValue("RmlUi", "fEntranceDuration", rmlEntranceDuration,
            "; Entrance reveal duration in seconds (0.05 to 2.0)");
        ini.SetDoubleValue("RmlUi", "fEntranceFeather", rmlEntranceFeather,
            "; Width of the soft reveal edge relative to the panel diagonal (0.0 to 0.5)");

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
        float outOffsetX = menuOffsetX;
        float outRotY    = menuRotY;
        float outRotZ    = menuRotZ;
        if (isMenuPoseMirrored()) {
            ReflectMenuPoseAcrossHands(outOffsetX, outRotY, outRotZ);
        }

        ini.SetDoubleValue("Visual", "fMenuOffsetX", (double)outOffsetX, "; Mirrored X offset relative to hand (for ALL panels)");
        ini.SetDoubleValue("Visual", "fMenuOffsetY", (double)menuOffsetY, "; Y offset");
        ini.SetDoubleValue("Visual", "fMenuOffsetZ", (double)menuOffsetZ, "; Z offset");
        ini.SetDoubleValue("Visual", "fMenuRotX",    (double)menuRotX,    "; Panel rotation X (all panels)");
        ini.SetDoubleValue("Visual", "fMenuRotY",    (double)outRotY,    "; Panel rotation Y");
        ini.SetDoubleValue("Visual", "fMenuRotZ",    (double)outRotZ,    "; Panel rotation Z");
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
        ini.SetBoolValue  ("Buttons", "bNormalizeItemVisuals", normalizeItemVisuals,
                           "; Center visible item geometry and use uniform automatic fitting");
        ini.SetBoolValue  ("Buttons", "bUseNifInventoryMarkerRotation", useNifInventoryMarkerRotation,
                           "; Use NIF inventory rotation only when no player/category override exists");
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
        ini.SetBoolValue  ("Interaction", "bEnableFingerTouch", enableFingerTouch,
                           "; Switch from laser to index-finger touch near the DragonBoard");
        ini.SetDoubleValue("Interaction", "fFingerTouchTipExtension", fingerTouchTipExtension,
                           "; Virtual fingertip distance beyond Finger12");
        ini.SetDoubleValue("Interaction", "fFingerTouchOffsetX", fingerTouchOffsetX,
                           "; Touch-point correction along Finger12 local X axis");
        ini.SetDoubleValue("Interaction", "fFingerTouchOffsetY", fingerTouchOffsetY,
                           "; Touch-point correction along Finger12 local Y axis");
        ini.SetDoubleValue("Interaction", "fFingerTouchOffsetZ", fingerTouchOffsetZ,
                           "; Touch-point correction along Finger12 local Z axis");
        ini.SetDoubleValue("Interaction", "fFingerTouchEnterDistance", fingerTouchEnterDistance,
                           "; Distance from the board that activates touch mode");
        ini.SetDoubleValue("Interaction", "fFingerTouchExitDistance", fingerTouchExitDistance,
                           "; Larger distance required to return to laser mode");
        ini.SetDoubleValue("Interaction", "fFingerTouchHoverDistance", fingerTouchHoverDistance,
                           "; Fingertip distance that starts hover");
        ini.SetDoubleValue("Interaction", "fFingerTouchPressDistance", fingerTouchPressDistance,
                           "; Fingertip distance that starts a touch press");
        ini.SetDoubleValue("Interaction", "fFingerTouchReleaseDistance", fingerTouchReleaseDistance,
                           "; Withdrawal distance required to release a touch");
        ini.SetDoubleValue("Interaction", "fFingerTouchScrollDeadzone", fingerTouchScrollDeadzone,
                           "; Vertical touch movement in RmlUi pixels before a tap becomes scroll");
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
        ini.SetValue      ("Interaction", "sHomeNifPath",        homeNifPath.c_str(),        "; NIF used for Home button");
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
        ini.SetBoolValue(
            "Debug", "bFingerTrackingProbe", fingerTrackingProbe,
            "; Log runtime index-finger nodes and HIGGS curl while DragonBoard is open");
        ini.SetBoolValue(
            "Debug", "bFingerTrackingProbeMarkers", fingerTrackingProbeMarkers,
            "; Show visual markers at the estimated index fingertips");
        ini.SetDoubleValue(
            "Debug", "fFingerTrackingMarkerScale", fingerTrackingMarkerScale,
            "; Visual fingertip marker scale (0.05 to 5.0)");
        ini.SetDoubleValue(
            "Debug", "fFingerTrackingTipExtension", fingerTrackingTipExtension,
            "; Distance beyond Finger12 along the Finger11-to-Finger12 direction");
        ini.SetDoubleValue(
            "Debug", "fFingerTrackingProbeInterval", fingerTrackingProbeInterval,
            "; Finger tracking log interval in seconds (0.10 to 5.0)");

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
        saveBtn("fHome",   bHomePosX,   bHomePosY,   bHomePosZ,   bHomeRotX,   bHomeRotY,   bHomeRotZ,   bHomeScale);
        saveBtn("fMods",   bModsPosX,   bModsPosY,   bModsPosZ,   bModsRotX,   bModsRotY,   bModsRotZ,   bModsScale);
        saveBtn("fFav",    bFavPosX,    bFavPosY,    bFavPosZ,    bFavRotX,    bFavRotY,    bFavRotZ,    bFavScale);
        saveBtn("fAddFunc",bAddFuncPosX,bAddFuncPosY,bAddFuncPosZ,bAddFuncRotX,bAddFuncRotY,bAddFuncRotZ,bAddFuncScale);
        saveBtn("fGold",   bGoldPosX,   bGoldPosY,   bGoldPosZ,   bGoldRotX,   bGoldRotY,   bGoldRotZ,   bGoldScale);
        saveBtn("fMap",    bMapPosX,    bMapPosY,    bMapPosZ,    bMapRotX,    bMapRotY,    bMapRotZ,    bMapScale);
        saveBtn("fDev",    bDevPosX,    bDevPosY,    bDevPosZ,    bDevRotX,    bDevRotY,    bDevRotZ,    bDevScale);

        // Remove obsolete persistent page-button settings from legacy combined
        // INIs before the split files are reconstructed.
        ini.Delete("Interaction", "sPrevPageNifPath");
        ini.Delete("Interaction", "sNextPageNifPath");
        for (const auto* prefix : { "fPrev", "fNext" }) {
            for (const auto* suffix : {
                     "PosX", "PosY", "PosZ",
                     "RotX", "RotY", "RotZ", "Scale" }) {
                const auto key = std::string(prefix) + suffix;
                ini.Delete("FixedButtons", key.c_str());
            }
        }


        // [FixedWidgets]
        ini.Delete("FixedWidgets", nullptr);
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
        ini.SetValue("FixedButtons", "sSysAction",    bSysAction.c_str(),    "; Action: Settings or menu name");
        ini.SetValue("FixedButtons", "sSaveLabel",    bSaveLabel.c_str(),    "; Displayed name of the Save button");
        ini.SetValue("FixedButtons", "sSaveAction",   bSaveAction.c_str(),   "; Action: QuickSave");
        ini.SetValue("FixedButtons", "sModsLabel",    bModsLabel.c_str(),    "; Displayed name of the Mods button");
        ini.SetValue("FixedButtons", "sModsAction",   bModsAction.c_str(),   "; Action: ModsPanel");
        ini.SetValue("FixedButtons", "sFavLabel",     bFavLabel.c_str(),     "; Displayed name of the Journal button");
        ini.SetValue("FixedButtons", "sFavAction",    bFavAction.c_str(),    "; Action: Journal (RmlUi)");
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

        // Save stable player overrides first.
        for (const auto& kv : stableItemOverrides) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                     kv.second.posX, kv.second.posY, kv.second.posZ,
                     kv.second.rotX, kv.second.rotY, kv.second.rotZ, kv.second.scale);
            ini.SetValue("ItemOverrides", kv.first.c_str(), buf);
        }

        // Preserve legacy runtime FormID entries until the player edits or
        // resets that item, at which point ItemUtils writes a stable key.
        for (const auto& kv : itemOverrides) {
            char keyBuf[16];
            snprintf(keyBuf, sizeof(keyBuf), "0x%08X", kv.first);
            char buf[256];
            snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f",
                     kv.second.posX, kv.second.posY, kv.second.posZ,
                     kv.second.rotX, kv.second.rotY, kv.second.rotZ, kv.second.scale);
            ini.SetValue("ItemOverrides", keyBuf, buf);
        }

        CSimpleIniA mainOut;
        CSimpleIniA layoutOut;
        CSimpleIniA stateOut;
        mainOut.SetUnicode();
        layoutOut.SetUnicode();
        stateOut.SetUnicode();

        const auto copyKeys = [&](CSimpleIniA& destination,
                                  const char* section,
                                  std::initializer_list<const char*> keys) {
            for (const auto* key : keys) CopyIniKey(ini, destination, section, key);
        };

        copyKeys(mainOut, "General", {
            "bVerboseLogging", "bEditModeEnabled", "bLockPins",
            "bShowTutorials"
        });
        copyKeys(mainOut, "Interface", { "sLanguage" });
        copyKeys(mainOut, "MapMarker", {
            "bEnableMapMarker", "bDynamicRotation", "sMarkerNifPath"
        });
        CopyIniSection(ini, mainOut, "QuestMarker");
        copyKeys(mainOut, "Activation", {
            "iActivationMode", "fHoldTimeGrip", "fHoldTimeTrigger",
            "fHoldTimeThumbstick", "fHoldTime", "bUseLeftHandAsMenu",
            "bLastSavedLeftHand"
        });
        CopyIniSection(ini, mainOut, "Combat");
        copyKeys(mainOut, "Interaction", {
            "fRaycastMaxDistance", "bEnableFingerTouch",
            "fFingerTouchEnterDistance", "fFingerTouchExitDistance",
            "fHapticIntensity", "fHapticDuration"
        });
        CopyIniSection(ini, mainOut, "Debug");
        CopyIniSection(ini, mainOut, "FixedWidgets");
        CopyIniSection(ini, mainOut, "Wiggle");
        CopyIniSection(ini, mainOut, "RmlUi");
        CopyIniSection(ini, mainOut, "CategoryOverrides");
        CopyIniSection(ini, mainOut, "ItemOverrides");
        mainOut.SetValue("CategoryOverrides", nullptr, nullptr);
        mainOut.SetValue("ItemOverrides", nullptr, nullptr);

        copyKeys(layoutOut, "Visual", {
            "fMenuScale",
            "fMenuOffsetX", "fMenuOffsetY", "fMenuOffsetZ",
            "fMenuRotX", "fMenuRotY", "fMenuRotZ",
            "bEnableMenuLerp", "fMenuLerpSpeed"
        });
        copyKeys(layoutOut, "Buttons", {
            "fItemWeaponScale", "fItemArmorScale", "fItemPotionScale",
            "fItemFoodScale", "fItemMiscScale", "bNormalizeItemVisuals"
        });
        copyKeys(layoutOut, "Background", {
            "fScale", "fOffsetX", "fOffsetY", "fOffsetZ",
            "fRotX", "fRotY", "fRotZ"
        });
        copyKeys(layoutOut, "LaserPointer", {
            "fReticleScaleX", "fReticleScaleY", "fReticleScaleZ"
        });
        for (const auto* key : {
                 "sLaserNifPath", "sBackgroundNifPath", "sMapNifPath",
                 "sDevNifPath", "sMagicNifPath", "sInventoryNifPath",
                 "sUnknownNifPath", "sSettingsNifPath", "sSaveNifPath",
                 "sModsNifPath", "sFavNifPath", "sStatusNifPath",
                 "sHomeNifPath",
                 "bEnableButtonEditMode" }) {
            CopyIniKeyAs(ini, layoutOut, "Interaction", "LaserPointer", key);
        }
        CopyIniSection(ini, layoutOut, "FixedButtons");
        CopyIniSection(ini, layoutOut, "Slots");

        for (std::size_t i = 0; i < mapCalibrationPoints.size(); ++i) {
            const auto prefix = "Point" + std::to_string(i + 1);
            for (const auto* suffix : {
                     "Valid", "WorldX", "WorldY", "MapU", "MapV" }) {
                const auto keyPrefix = suffix == std::string_view("Valid") ? "b" : "f";
                const auto key = keyPrefix + prefix + suffix;
                CopyIniKey(ini, stateOut, "MapCalibration", key.c_str());
            }
        }
        stateOut.SetBoolValue(
            "Tutorials", "bPreviouslyEnabled", showTutorials,
            "; Internal state used to detect when tutorials are re-enabled");
        stateOut.SetBoolValue(
            "Tutorials", "bWelcomeComplete",
            isTutorialComplete(TutorialId::Welcome),
            "; Legacy Welcome tutorial completion state");
        stateOut.SetLongValue(
            "Tutorials", "uCompletedMask",
            static_cast<long>(tutorialCompletionMask),
            "; Completion bits shared by all RmlUi tutorials");

        const auto layoutPath = GetSiblingIniPath(iniPath, "DragonBoardVR_Layout.ini");
        const auto statePath = GetSiblingIniPath(iniPath, "DragonBoardVR_State.ini");
        const bool mainSaved = mainOut.SaveFile(iniPath.c_str()) >= 0;
        const bool layoutSaved = layoutOut.SaveFile(layoutPath.c_str()) >= 0;
        const bool stateSaved = stateOut.SaveFile(statePath.c_str()) >= 0;
        if (!mainSaved || !layoutSaved || !stateSaved) {
            logger::error(
                "DragonBoardVR: Failed to save split settings (main={}, layout={}, state={}).",
                mainSaved,
                layoutSaved,
                stateSaved);
            return;
        }
        logger::info(
            "DragonBoardVR: Settings saved to '{}', '{}' and '{}'.",
            iniPath,
            layoutPath,
            statePath);
    }
}
