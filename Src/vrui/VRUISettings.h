#pragma once

#include "MapCalibration.h"

#include <array>
#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace vrui
{
    /// Activation mode: which button(s) activate the menu
    enum class ActivationMode : int
    {
        Grip               = 0,  // hold grip (default)
        Trigger            = 1,  // hold trigger
        Thumbstick         = 2,  // press thumbstick
        GripPlusThumbstick  = 3,  // grip + thumbstick simultaneously
        GripPlusY          = 4,  // grip + Y (left hand)
        GripPlusB          = 5,  // grip + B (right hand)
        Hotkey8            = 6   // keyboard key 8 or F8
    };

    enum class TutorialId : std::uint8_t
    {
        Welcome = 0,
        Inventory,
        Magic,
        Settings,
        Mods,
        Journal,
        Pin
    };

    struct ItemOffsetData {
        float posX = 0.0f;
        float posY = 0.0f;
        float posZ = 0.0f;
        float rotX = 0.0f;
        float rotY = 0.0f;
        float rotZ = 0.0f;
        float scale = 1.0f;
    };

    struct FixedWidgetItem {
        std::string name;
        std::string nifPath;
        std::string category;
        std::string actionFunc; // Pinned console command or function
        uint32_t formID = 0;
        float posX, posY, posZ;
        float rotX, rotY, rotZ;
        float scale;
    };

    struct CaseInsensitiveComparator {
        bool operator()(const std::string& a, const std::string& b) const {
#ifdef WIN32
            return _stricmp(a.c_str(), b.c_str()) < 0;
#else
            return strcasecmp(a.c_str(), b.c_str()) < 0;
#endif
        }
    };

    /// Configuration settings loaded from DragonBoardVR.ini
    struct VRUISettings
    {
        std::map<std::string, ItemOffsetData, CaseInsensitiveComparator> categoryOverrides;
        std::map<std::string, ItemOffsetData, CaseInsensitiveComparator> categoryButtons;
        // Stable overrides use "Plugin.esp|LOCAL_FORM_ID" keys so a load-order
        // change does not redirect a player's correction to another item.
        std::map<std::string, ItemOffsetData, CaseInsensitiveComparator> stableItemOverrides;
        // Legacy full runtime FormIDs remain readable for backwards compatibility.
        std::map<uint32_t, ItemOffsetData> itemOverrides;
        std::vector<FixedWidgetItem> fixedWidgets;

        static constexpr int kMaxSlots = 8;

        VRUISettings();

        // -----------------------------------------------------------------------
        // [General]
        // -----------------------------------------------------------------------
        bool verboseLogging = false;        // Enable trace-level logging (very spammy)
        bool editModeEnabled = true;        // Master switch for item editing (MCM)
        bool lockPins = false;               // Prevent pinned items from being edited, grabbed, or removed
        bool alwaysOnDisplay = false;        // Keep physical-board panels active after the board is released
        std::string uiLanguage = "en";
        bool showTutorials = true;
        bool tutorialsPreviouslyEnabled = true;
        std::uint32_t tutorialCompletionMask = 0;
        bool tutorialPositionResetRequested = false;

        // -----------------------------------------------------------------------
        // [Combat]
        // -----------------------------------------------------------------------
        bool slowTimeOnOpen = true;
        float slowTimeMultiplier = 0.25f;

        // -----------------------------------------------------------------------
        // [RmlUi]
        // -----------------------------------------------------------------------
        bool rmlRenderOnDirty = true;        // Reuse the last panel texture while unchanged
        int rmlMaxActiveFPS = 60;             // Maximum texture refresh rate during interaction
        int rmlRenderWidth = 1280;
        int rmlRenderHeight = 720;
        bool rmlEntranceAnimation = true;     // Reveal the page surface from its center when opened
        std::string rmlEntranceStyle = "radial";
        float rmlEntranceDuration = 0.25f;    // Entrance duration in seconds
        float rmlEntranceFeather = 0.10f;     // Soft radial edge relative to the panel diagonal

        // -----------------------------------------------------------------------
        // [Activation]
        // -----------------------------------------------------------------------
        ActivationMode activationMode     = ActivationMode::Hotkey8;
        float activationHoldTimeGrip      = 0.0f;   // fHoldTimeGrip
        float activationHoldTimeTrigger   = 0.3f;   
        float activationHoldTimeThumbstick= 0.15f;  
        float activationHoldTime          = 0.2f;   // fHoldTime
        bool  useLeftHandAsMenu           = true;    

        // -----------------------------------------------------------------------
        // -----------------------------------------------------------------------
        // [PhysicalBoard]
        // -----------------------------------------------------------------------
        bool physicalBoardEnabled = true;
        std::string physicalBoardPlugin = "DragonBoardVR.esp";
        std::uint32_t physicalBoardLocalFormID = 0x000800;
        std::uint32_t physicalBoardVrikProxyLocalFormID = 0x000801;
        float physicalBoardScale = 1.0f;
        float physicalBoardMeshScale = 1.0f;
        float physicalRmlSurfaceOffsetX = 0.0f;
        float physicalRmlSurfaceOffsetY = 0.0f;
        float physicalRmlSurfaceOffsetZ = 0.0f;
        float physicalRmlSurfaceRotX = 0.0f;
        float physicalRmlSurfaceRotY = 0.0f;
        float physicalRmlSurfaceRotZ = 0.0f;
        float physicalRmlSurfaceScale = 1.0f;
        // [Visual]  — ALL panels share these values
        // -----------------------------------------------------------------------
        float menuScale     = 1.12f;
        float menuOffsetX   =   1.0f;        // Internal layout default
        float menuOffsetY   = -17.0f;        // Internal layout default
        float menuOffsetZ   =  -3.5f;        // Internal layout default
        float menuRotX      = -10.0f;        // fMenuRotX
        float menuRotY      =  36.0f;        // fMenuRotY
        float menuRotZ      = 85.0f;         // Internal layout default
        
        float containerGridOffsetZ = 0.42f;  // Internal layout default
        
        bool  bEnableMenuLerp  = true;       
        float fMenuLerpSpeed   = 5.0f;       

        // -----------------------------------------------------------------------
        // [Buttons]
        // -----------------------------------------------------------------------
        float buttonSpacingX  = 2.6f;       // Internal layout default
        float buttonSpacingY  = 1.2f;       // fButtonSpacingY
        float buttonMeshScale = 1.5f;       
        float itemMeshScale   = 1.29f;      // Base scale of in-button 3D item models
        float itemWeaponScale = 1.0f;       // Specific multiplier for weapons/shields
        float itemArmorScale  = 1.0f;       // Specific multiplier for armor/clothes
        float itemPotionScale = 3.31f;      // Specific multiplier for potions
        float itemFoodScale   = 0.87f;      // Specific multiplier for food/ingredients
        float itemMiscScale   = 0.90f;      // Specific multiplier for misc/clutter/books
        bool  normalizeItemVisuals = true;  // Center visible geometry and fit it uniformly
        bool  useNifInventoryMarkerRotation = true; // Use BSInvMarker only without an INI override
        int   gridColumns     = 5;          // Grid columns for main panel and dynamic containers
        int   gridPageSize    = 20;         // Items per page in grid containers

        // -----------------------------------------------------------------------
        // [Background] — Tablet NIF
        // -----------------------------------------------------------------------
        bool  showBackground    = true;
        float backgroundScale   = 1.55f;
        float backgroundOffsetX =  0.0f;
        float backgroundOffsetY = -1.0f;
        float backgroundOffsetZ =  0.0f;
        float backgroundRotX    =  0.0f;
        float backgroundRotY    =  0.0f;
        float backgroundRotZ    =  0.0f;
        float rmlSurfaceOffsetX  =  0.0f;
        float rmlSurfaceOffsetY  =  0.0f;
        float rmlSurfaceOffsetZ  =  0.0f;
        float rmlSurfaceRotX     =  0.0f;
        float rmlSurfaceRotY     =  0.0f;
        float rmlSurfaceRotZ     =  0.0f;
        float rmlSurfaceScale    =  1.0f;

        // -----------------------------------------------------------------------
        // [Interaction]
        // -----------------------------------------------------------------------
        float raycastMaxDistance = 100.0f;
        bool  bEnableRotationSnapping = true;
        float fSnapDistanceThreshold = 1.0f;
        bool  hapticOnHover      = true;
        bool  hapticOnPress      = true;
        float hapticIntensity    = 0.1f;    // fHapticIntensity = 0.1
        float hapticDuration     = 0.04f;
        float equipCooldown      = 0.3f;    // fEquipCooldown = 0.3
        bool  enableFingerTouch  = true;
        float fingerTouchTipExtension = 2.0f;
        float fingerTouchOffsetX = 0.0f;
        float fingerTouchOffsetY = -0.2f;
        float fingerTouchOffsetZ = 0.0f;
        float fingerTouchEnterDistance = 5.0f;
        float fingerTouchExitDistance = 6.0f;
        float fingerTouchHoverDistance = 1.5f;
        float fingerTouchPressDistance = 0.45f;
        float fingerTouchReleaseDistance = 1.0f;
        float fingerTouchScrollDeadzone = 55.0f;
        float fingerTouchMaxActivationSpeed = 50.0f;

        std::string laserNifPath      = "DragonBoardVR/IconPlane.nif"; // laser.nif crashes SkyrimVR parser (Outfit Studio export incompatibility)
        std::string backgroundNifPath = "DragonBoardVR/dragonboard.nif";

        // -----------------------------------------------------------------------
        // [LaserPointer]
        // -----------------------------------------------------------------------
        // Reticle (cursor glued to the panel surface)
        float reticleScaleX = 2.12f;
        float reticleScaleY = 2.12f;
        float reticleScaleZ = 2.12f;
        float reticleOffsetX = 0.0f;
        float reticleOffsetY = -0.75f;
        float reticleOffsetZ = 0.0f;
        float reticleRotX    = -90.0f;   // fReticleRotX = -90
        float reticleRotY    = 0.0f;
        float reticleRotZ    = 0.0f;

        // Laser beam (child of dominant hand, stretches to reticle)
        float laserThickness = 0.005f; // Base X/Y scale (thin)
        float laserScaleX    = 1.0f;   // X scale multiplier
        float laserScaleY    = 1.0f;   // Y scale multiplier
        float laserScaleZ    = 1.0f;   // Z scale multiplier (on the length)
        float laserOffsetX   = 0.0f;
        float laserOffsetY   = 0.0f;
        float laserOffsetZ   = 0.0f;   // Z offset shifts start point along controller forward
        float laserRotX      = 0.0f;
        float laserRotY      = 0.0f;
        float laserRotZ      = 0.0f;
        std::string mapNifPath         = "DragonBoardVR/IconPlane.nif";
        std::string devNifPath         = "DragonBoardVR/IconPlane.nif";
        std::string magicNifPath       = "DragonBoardVR/IconPlane.nif";
        std::string inventoryNifPath   = "DragonBoardVR/IconPlane.nif";
        std::string unknownNifPath     = "DragonBoardVR/Unknow.nif";
        std::string settingsNifPath    = "DragonBoardVR/IconPlane.nif";
        std::string saveNifPath        = "DragonBoardVR/IconPlane.nif";
        std::string modsNifPath        = "DragonBoardVR/IconPlane.nif";
        std::string favNifPath         = "DragonBoardVR/IconPlane.nif";

        std::string statusNifPath      = "DragonBoardVR/IconPlane.nif";
        std::string homeNifPath        = "DragonBoardVR/IconPlane.nif";

        // -----------------------------------------------------------------------
        // [Interaction] — Edit Mode
        // -----------------------------------------------------------------------
        bool bEnableButtonEditMode = true;  // Allow grab-and-move of UI buttons

        // -----------------------------------------------------------------------
        // [Labels]
        // -----------------------------------------------------------------------
        float labelScale          = 0.7f;
        float labelXOffset        = 0.0f;
        float labelYOffset        = 0.3f;
        float labelZOffset        = 0.0f;
        float labelSpacing        = 0.25f;
        float labelYOffsetDynamic = 0.2f;   // Y offset for labels in dynamic containers (inventory/magic/favorites)

        // -----------------------------------------------------------------------
        // [Wiggle]
        // -----------------------------------------------------------------------
        bool  enableWorldPinWiggle = true;
        float wigglePosAmplitude   = 0.10f;
        float wiggleSideAmplitude  = 0.04f;
        float wiggleRotAmplitude   = 3.0f;
        float wiggleSpeed          = 1.0f;

        // -----------------------------------------------------------------------
        // [Debug]
        // -----------------------------------------------------------------------
        bool debugMode = false;
        bool fingerTrackingProbe = false;
        bool fingerTrackingProbeMarkers = true;
        float fingerTrackingMarkerScale = 1.00f;
        float fingerTrackingTipExtension = 2.0f;
        float fingerTrackingProbeInterval = 0.25f;

        // -----------------------------------------------------------------------
        // [FixedButtons] — per-button position/rotation
        // -----------------------------------------------------------------------
        std::string bStatusLabel  = "Skills";     
        std::string bStatusAction = "StatsMenu";  
        float bStatusPosX = 14.000000f; float bStatusPosY = 0.250000f; float bStatusPosZ = -6.000000f;
        float bStatusRotX = 0.000000f; float bStatusRotY = 0.000000f; float bStatusRotZ = -20.000000f;
        float bStatusScale = 1.000000f;

        std::string bInvLabel  = "Inventory";     
        std::string bInvAction = "InventoryPanel"; 
        float bInvPosX = 14.000000f; float bInvPosY = 0.250000f; float bInvPosZ = 1.000000f;
        float bInvRotX = 0.000000f; float bInvRotY = 0.000000f; float bInvRotZ = -20.000000f;
        float bInvScale = 1.000000f;

        std::string bMagicLabel  = "Magic";     
        std::string bMagicAction = "MagicPanel"; 
        float bMagicPosX = 14.000000f; float bMagicPosY = 0.250000f; float bMagicPosZ = -2.500000f;
        float bMagicRotX = 0.000000f; float bMagicRotY = 0.000000f; float bMagicRotZ = -20.000000f;
        float bMagicScale = 1.000000f;

        std::string bSysLabel  = "Settings";    
        std::string bSysAction = "Settings";
        float bSysPosX = -12.000000f;  float bSysPosY = 0.000000f;  float bSysPosZ = -9.000000f;
        float bSysRotX = 0.000000f;  float bSysRotY = 0.000000f; float bSysRotZ = 0.000000f;
        float bSysScale = 0.800000f;   

        std::string bSaveLabel  = "Save";      
    std::string bSaveAction = "Save";
        float bSavePosX = 14.000000f; float bSavePosY = 0.250000f; float bSavePosZ = 8.000000f;
        float bSaveRotX = 0.000000f; float bSaveRotY = 0.000000f; float bSaveRotZ = -20.000000f;
        float bSaveScale = 1.099043f;

        float bHomePosX = 0.000000f;  float bHomePosY = 0.250000f; float bHomePosZ = -9.000000f;
        float bHomeRotX = -20.000000f; float bHomeRotY =  0.0f; float bHomeRotZ = 0.0f;
        float bHomeScale = 1.000000f;

        std::string bModsLabel  = "Mods";     
        std::string bModsAction = "ModsPanel"; 
        float bModsPosX = -14.000000f; float bModsPosY = 0.250000f; float bModsPosZ = 8.000000f;
        float bModsRotX = 0.000000f; float bModsRotY = 0.000000f; float bModsRotZ = 20.000000f;
        float bModsScale = 1.000000f;

        std::string bFavLabel  = "Journal";
        std::string bFavAction = "Journal";
        float bFavPosX = -14.000000f;  float bFavPosY =  0.250000f;  float bFavPosZ = 4.500000f;
        float bFavRotX = 0.000000f; float bFavRotY = 0.000000f; float bFavRotZ = 20.000000f;
        float bFavScale = 1.000000f;



        float bAddFuncPosX = -12.000000f; float bAddFuncPosY = 0.000000f; float bAddFuncPosZ = -5.000000f;
        float bAddFuncRotX =  0.000000f; float bAddFuncRotY =  0.000000f; float bAddFuncRotZ =  0.000000f;
        float bAddFuncScale = 0.800000f;

        float bGoldPosX = 0.0f; float bGoldPosY = 0.0f; float bGoldPosZ = -14.0f;
        float bGoldRotX = 0.0f; float bGoldRotY = 0.0f; float bGoldRotZ =   0.0f;
        float bGoldScale = 1.0f;

        std::string bMapLabel  = "Map";     
        std::string bMapAction = "MapMenu";  
        float bMapPosX = 14.000000f; float bMapPosY = 0.250000f; float bMapPosZ = 4.500000f;
        float bMapRotX = 0.000000f; float bMapRotY = 0.000000f; float bMapRotZ = -20.000000f;
        float bMapScale = 1.000000f;   

        std::string bDevLabel  = "Dev";     
        std::string bDevAction = "DevPanel"; 
        float bDevPosX = -16.000000f; float bDevPosY = 0.000000f; float bDevPosZ = -2.000000f;
        float bDevRotX = 0.000000f; float bDevRotY = 0.000000f; float bDevRotZ = 0.000000f;
        float bDevScale = 0.800000f;   
        bool  showDevButton = false;

        std::string bGalleryLabel = "Gallery";
        std::string bGalleryAction = "Gallery";
        float bGalleryPosX = -14.0f; float bGalleryPosY = 0.25f; float bGalleryPosZ = 0.0f;
        float bGalleryRotX = 0.0f; float bGalleryRotY = 0.0f; float bGalleryRotZ = 20.0f;
        float bGalleryScale = 1.0f;

        bool statusWidgetVisible = false;
        int galleryCaptureTimerSeconds = 0;
        int galleryGridColumns = 8;
        int galleryThumbnailWidth = 512;
        int galleryMaximumVisibleMarkers = 64;
        int galleryMaximumPinnedPanels = 8;
        float galleryCameraMarkerScale = 0.30f;
        float galleryCameraMarkerRotX = 90.0f;
        float galleryCameraMarkerRotY = 0.0f;
        float galleryCameraMarkerRotZ = 180.0f;
        float galleryPhotoPanelDefaultScale = 0.5f;
        float galleryPhotoPanelDefaultPosX = 0.0f;
        float galleryPhotoPanelDefaultPosY = 0.0f;
        float galleryPhotoPanelDefaultPosZ = 0.5f;
        float galleryPhotoPanelDefaultRotX = 0.0f;
        float galleryPhotoPanelDefaultRotY = 0.0f;
        float galleryPhotoPanelDefaultRotZ = 0.0f;
        std::string defaultPanelAction = "MainPanel";

        // -----------------------------------------------------------------------
        // [Slots] — 8 slots max
        // -----------------------------------------------------------------------
        std::string slotActions[kMaxSlots] = {
            "Save", "Wait", "TweenMenu",
            "Inventory", "Magic", "Map",
            "Journal", "None"
        };
        std::string slotTextures[kMaxSlots];
        std::string slotNifs[kMaxSlots];
        std::string slotLabels[kMaxSlots];
        std::string slotSublabels[kMaxSlots];

        float slotPosX[kMaxSlots] = {0};
        float slotPosY[kMaxSlots] = {0};
        float slotPosZ[kMaxSlots] = {0};
        float slotRotX[kMaxSlots] = {0};
        float slotRotY[kMaxSlots] = {0};
        float slotRotZ[kMaxSlots] = {0};
        float slotScaleUser[kMaxSlots];
        bool  slotFloating[kMaxSlots]      = {false};
        bool  slotFloatingCache[kMaxSlots] = {false};

        // -----------------------------------------------------------------------
        // -----------------------------------------------------------------------
        
        // -----------------------------------------------------------------------
        // [MapMarker]
        // -----------------------------------------------------------------------
        bool  bEnableMapMarker = true;
        float mapWorldMinX = -77000.0f;
        float mapWorldMaxX =  54000.0f;
        float mapWorldMinY = -90000.0f;
        float mapWorldMaxY =  78000.0f;
        
        float mapMarkerScale  = 1.0f;
        float mapWidth        = -10.0f;
        float mapHeight       = 8.2f;
        
        bool  bMapMarkerDynamicRotation = true;
        float mapMarkerRotX             = 90.0f;
        float mapMarkerRotY             = 0.0f;
        float mapMarkerRotZ             = 180.0f;
        float mapMarkerRotOffset        = 0.0f; 
        float mapMarkerOffsetX          = 2.0f;
        float mapMarkerOffsetY          = 0.0f;
        float mapMarkerOffsetZ          = -0.75f;

        std::string mapMarkerNifPath = "DragonBoardVR/Player.nif"; 
        std::array<MapCalibrationPoint, kMapCalibrationPointCount> mapCalibrationPoints{};

        // [QuestMarker]
        bool  bEnableQuestMarker = true;
        float questMarkerScale = 0.25f;
        float questMarkerRotX = 90.0f;
        float questMarkerRotY = 0.0f;
        float questMarkerRotZ = 180.0f;
        static constexpr std::size_t kQuestMarkerSlotCount = 3;
        std::array<std::uint32_t, kQuestMarkerSlotCount> questMarkerLastFormIDs{};
        std::array<std::uint32_t, kQuestMarkerSlotCount>
            questMarkerLastQuestInstanceIDs{};
        std::array<std::uint32_t, kQuestMarkerSlotCount>
            questMarkerLastObjectiveInstanceIDs{};
        std::array<std::uint16_t, kQuestMarkerSlotCount>
            questMarkerLastObjectiveIDs{};

        // -----------------------------------------------------------------------
        // API
        // -----------------------------------------------------------------------
        void load(const std::string& iniPath);
        void save(const std::string& iniPath) const;
        bool isTutorialComplete(TutorialId tutorial) const;
        void setTutorialComplete(TutorialId tutorial, bool complete = true);
        void resetTutorialProgress();
        void setUseLeftHandAsMenu(bool useLeftHand, bool nativeLeftHandedMode = false);
        bool isMenuPoseMirrored() const;
        bool isPoseMirroredForHand(bool leftHand) const;
        bool isNativeLeftHandedMode() const { return _nativeLeftHandedMode; }
        static std::string getDefaultIniPath();
        static std::string getDefaultLayoutIniPath();
        static std::string getDefaultStateIniPath();
        static VRUISettings& get();

    private:
        bool _nativeLeftHandedMode = false;
        // VRUISettings(); // Already declared above
    };
}
