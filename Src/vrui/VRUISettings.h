#pragma once

#include "MapCalibration.h"

#include <array>
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

        // -----------------------------------------------------------------------
        // [Activation]
        // -----------------------------------------------------------------------
        ActivationMode activationMode     = ActivationMode::GripPlusY; // iActivationMode = 4
        float activationHoldTimeGrip      = 0.0f;   // fHoldTimeGrip
        float activationHoldTimeTrigger   = 0.3f;   
        float activationHoldTimeThumbstick= 0.15f;  
        float activationHoldTime          = 0.2f;   // fHoldTime
        bool  useLeftHandAsMenu           = true;    

        // -----------------------------------------------------------------------
        // [Visual]  — ALL panels share these values
        // -----------------------------------------------------------------------
        float menuScale     = 1.0f;          
        float menuOffsetX   =   0.5f;        // fMenuOffsetX
        float menuOffsetY   = -18.5f;        // fMenuOffsetY
        float menuOffsetZ   =   0.0f;        // fMenuOffsetZ
        float menuRotX      = -10.0f;        // fMenuRotX
        float menuRotY      =  36.0f;        // fMenuRotY
        float menuRotZ      = 0.0f;          // fMenuRotZ (0 = front face toward the player)
        
        float containerGridOffsetZ = 1.0f;   // fContainerGridOffsetZ
        
        bool  bEnableMenuLerp  = true;       
        float fMenuLerpSpeed   = 5.0f;       

        // -----------------------------------------------------------------------
        // [Buttons]
        // -----------------------------------------------------------------------
        float buttonSpacingX  = 2.4f;       // fButtonSpacingX
        float buttonSpacingY  = 1.2f;       // fButtonSpacingY
        float buttonMeshScale = 1.5f;       
        float itemMeshScale   = 1.27f;      // Base scale of in-button 3D item models
        float itemWeaponScale = 1.0f;       // Specific multiplier for weapons/shields
        float itemArmorScale  = 1.0f;       // Specific multiplier for armor/clothes
        float itemPotionScale = 1.0f;       // Specific multiplier for potions
        float itemFoodScale   = 1.0f;       // Specific multiplier for food/ingredients
        float itemMiscScale   = 1.0f;       // Specific multiplier for misc/clutter/books
        bool  normalizeItemVisuals = true;  // Center visible geometry and fit it uniformly
        bool  useNifInventoryMarkerRotation = true; // Use BSInvMarker only without an INI override
        int   gridColumns     = 5;          // Grid columns for main panel and dynamic containers
        int   gridPageSize    = 20;         // Items per page in grid containers

        // -----------------------------------------------------------------------
        // [Background] — Tablet NIF
        // -----------------------------------------------------------------------
        bool  showBackground    = true;
        float backgroundScale   = 11.55f;   // fScale = 11.55
        float backgroundOffsetX =  0.0f;
        float backgroundOffsetY = -1.0f;
        float backgroundOffsetZ =  0.0f;
        float backgroundRotX    = 90.0f;
        float backgroundRotY    =  0.0f;
        float backgroundRotZ    = 180.0f;

        // -----------------------------------------------------------------------
        // [Interaction]
        // -----------------------------------------------------------------------
        float raycastMaxDistance = 100.0f;
        bool  bEnableRotationSnapping = true;
        float fSnapDistanceThreshold = 4.0f;    // fSnapDistanceThreshold = 4.0
        bool  hapticOnHover      = true;
        bool  hapticOnPress      = true;
        float hapticIntensity    = 0.1f;    // fHapticIntensity = 0.1
        float hapticDuration     = 0.04f;
        float equipCooldown      = 0.3f;    // fEquipCooldown = 0.3

        std::string laserNifPath      = "DragonBoardVR/IconPlane.nif"; // laser.nif crashes SkyrimVR parser (Outfit Studio export incompatibility)
        std::string backgroundNifPath = "DragonBoardVR/Tablet.nif";

        // -----------------------------------------------------------------------
        // [LaserPointer]
        // -----------------------------------------------------------------------
        // Reticle (cursor glued to the panel surface)
        float reticleScaleX = 2.5f;     // fReticleScaleX = 2.5
        float reticleScaleY = 2.5f;     
        float reticleScaleZ = 2.5f;     
        float reticleOffsetX = 0.0f;
        float reticleOffsetY = -1.9f;   // fReticleOffsetY = -1.9
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
        std::string nextPageNifPath    = "DragonBoardVR/IconPlane.nif";
        std::string homeNifPath        = "DragonBoardVR/IconPlane.nif";
        std::string prevPageNifPath    = "DragonBoardVR/IconPlane.nif";

        // -----------------------------------------------------------------------
        // [Interaction] — Edit Mode
        // -----------------------------------------------------------------------
        bool bEnableButtonEditMode = true;  // Allow grab-and-move of UI buttons

        // -----------------------------------------------------------------------
        // [Labels]
        // -----------------------------------------------------------------------
        float labelScale          = 1.0f;
        float labelXOffset        = 0.0f;
        float labelYOffset        = 0.3f;
        float labelZOffset        = 0.0f;
        float labelSpacing        = 0.2f;
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
        std::string bSysAction = "MCM_Panel";  
        float bSysPosX = -12.000000f;  float bSysPosY = 0.000000f;  float bSysPosZ = -9.000000f;
        float bSysRotX = 0.000000f;  float bSysRotY = 0.000000f; float bSysRotZ = 0.000000f;
        float bSysScale = 0.800000f;   

        std::string bSaveLabel  = "Save";      
        std::string bSaveAction = "QuickSave";  
        float bSavePosX = 14.169888f; float bSavePosY = 0.251898f; float bSavePosZ = -2.571962f;
        float bSaveRotX = -0.571924f; float bSaveRotY = 0.971138f; float bSaveRotZ = -15.558892f;
        float bSaveScale = 1.099043f;

        float bPrevPosX = 3.000000f;  float bPrevPosY = 0.250000f; float bPrevPosZ = -9.000000f;
        float bPrevRotX = -20.000000f; float bPrevRotY = 0.0f; float bPrevRotZ = 0.0f;
        float bPrevScale = 1.000000f;

        float bHomePosX = 0.000000f;  float bHomePosY = 0.250000f; float bHomePosZ = -9.000000f;
        float bHomeRotX = -20.000000f; float bHomeRotY =  0.0f; float bHomeRotZ = 0.0f;
        float bHomeScale = 1.000000f;

        float bNextPosX = -3.000000f; float bNextPosY = 0.250000f; float bNextPosZ = -9.000000f;
        float bNextRotX = -20.000000f; float bNextRotY = 0.0f; float bNextRotZ = 0.0f;
        float bNextScale = 1.000000f;

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
        bool  showDevButton = false; // bShowDevButton = false
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
        float mapWorldMinX = -57000.0f; 
        float mapWorldMaxX =  80000.0f;
        float mapWorldMinY = -80000.0f;
        float mapWorldMaxY =  78000.0f;
        
        float mapMarkerScale  = 1.2f;
        float mapWidth        = -7.9f;  
        float mapHeight       = 9.1f;
        
        bool  bMapMarkerDynamicRotation = true;
        float mapMarkerRotX             = 90.0f;
        float mapMarkerRotY             = 0.0f;
        float mapMarkerRotZ             = 180.0f;
        float mapMarkerRotOffset        = 0.0f; 
        float mapMarkerOffsetX          = -1.0f;
        float mapMarkerOffsetY          = 0.0f;
        float mapMarkerOffsetZ          = -0.9f;

        std::string mapMarkerNifPath = "DragonBoardVR/Player.nif"; 
        std::array<MapCalibrationPoint, kMapCalibrationPointCount> mapCalibrationPoints{};

        // [QuestMarker]
        bool  bEnableQuestMarker = true;
        float questMarkerScale = 0.5f;
        float questMarkerRotX = 90.0f;
        float questMarkerRotY = 0.0f;
        float questMarkerRotZ = 180.0f;
        std::string questMarkerNifPath = "meshes\\DragonBoardVR\\QuestMarker.nif";
        std::uint32_t questMarkerLastFormID = 0;
        std::uint32_t questMarkerLastQuestInstanceID = 0;
        std::uint32_t questMarkerLastObjectiveInstanceID = 0;
        std::uint16_t questMarkerLastObjectiveID = 0;

        // -----------------------------------------------------------------------
        // API
        // -----------------------------------------------------------------------
        void load(const std::string& iniPath);
        void save(const std::string& iniPath) const;
        void setUseLeftHandAsMenu(bool useLeftHand);
        static std::string getDefaultIniPath();
        static VRUISettings& get();

    private:
        // VRUISettings(); // Already declared above
    };
}
