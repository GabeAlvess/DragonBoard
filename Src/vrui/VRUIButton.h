#pragma once

#include "VRUIWidget.h"
#include "VRUITextHelper.h"
#include "VRUIModelHelper.h"
#include "VRUIItemUtils.h"
#include <RE/B/BSModelDB.h>
#include <functional>

namespace vrui
{
    /// Button states for visual feedback
    enum class ButtonState : uint8_t
    {
        Normal,
        Hovered,
        Pressed
    };

    /// A pressable VR button widget.
    /// Supports hover/press visual feedback and callback handlers.
    class VRUIButton : public VRUIWidget
    {
    public:
        using PressCallback = std::function<void(VRUIButton*, EquipHand)>;
        using HoverCallback = std::function<void(VRUIButton*, bool)>;

        /// Create button with procedural quad (label only)
        explicit VRUIButton(const std::string& label,
                            float width = 3.0f, float height = 1.5f);

        /// Create button with custom NIF mesh and optional overhead texture path
        VRUIButton(const std::string& label, const std::string& nifPath, const std::string& texturePath = "",
                   float width = 3.0f, float height = 1.5f, bool deferInit = false);

        /// Create button with item-type-specific rotation/scale/position-offset overrides
        /// Use NAN for rotation values that should fall back to INI settings.
        VRUIButton(const std::string& label, const std::string& nifPath, const std::string& texturePath,
                   float width, float height,
                   float itemRotX, float itemRotY, float itemRotZ,
                   float itemXOffset, float itemYOffset, float itemZOffset,
                   float itemScaleMult, bool deferInit = false,
                   ItemUtils::ItemTransformSource transformSource = ItemUtils::ItemTransformSource::TypeFallback);
        void update(float deltaTime) override;
        void setLocalScale(float scale) override;

        // --- State ---
        ButtonState getState() const { return _state; }
        /// Get the target scale for the current button state
        float getTargetScale() const { return _targetScale; }
        void setState(ButtonState state);

        static void resetFrameLoadCounter() { s_visualsLoadedThisFrame = 0; }
        static int s_visualsLoadedThisFrame;

        // --- Event handlers ---
        void setOnPressHandler(PressCallback callback) { _onPressHandler = std::move(callback); }
        void setOnReleaseHandler(PressCallback callback) { _onReleaseHandler = std::move(callback); }
        void setOnLongPressHandler(PressCallback callback) { _onLongPressHandler = std::move(callback); }
        void setOnHoverHandler(HoverCallback callback) { _onHoverHandler = std::move(callback); }
        void setOnGripDragHandler(PressCallback callback) { _onGripDragHandler = std::move(callback); }
        void setOnSecondaryPressHandler(PressCallback callback) { _onSecondaryPressHandler = std::move(callback); }
        void setOnSecondaryLongPressHandler(PressCallback callback) { _onSecondaryLongPressHandler = std::move(callback); }
        void setOnGrabReleaseHandler(std::function<void(VRUIButton*)> callback) { _onGrabReleaseHandler = std::move(callback); }

        // --- Edit Mode ---
        void startGrab();
        void releaseGrab();
        bool isGrabbed() const { return _isGrabbed; }

        // --- Persistence ---
        void setCanBePersistent(bool val) { _canBePersistent = val; }
        bool canBePersistent() const { return _canBePersistent; }
        void setPersistent(bool val) { _isPersistent = val; }
        bool isPersistent() const { return _isPersistent; }

        // --- Input dispatch (called by VRMenuManager) ---
        void onRayEnter() override;
        void onRayExit() override;
        void onTriggerPress(EquipHand hand = EquipHand::kRight) override;
        void onTriggerRelease(EquipHand hand = EquipHand::kRight) override;
        void onTriggerLongPress(EquipHand hand = EquipHand::kRight) override;
        void onSecondaryPress() override;
        void onSecondaryLongPress() override;
        
        int getSlotIndex() const { return _slotIndex; }
        void setSlotIndex(int index) { _slotIndex = index; }

        int getFixedWidgetIndex() const { return _fixedWidgetIndex; }
        void setFixedWidgetIndex(int index) { _fixedWidgetIndex = index; }
 
        const std::string& getLabel() const { return _label; }
        void setLabel(const std::string& text) { _label = text; if (_isVisualsInitialized) refreshLabel(); }
        const std::string& getButtonId() const { return _buttonId; }
        void setButtonId(const std::string& id) { _buttonId = id; }
        
        const std::string& getSublabel() const { return _sublabel; }
        void setSublabel(const std::string& text) { _sublabel = text; if (_isVisualsInitialized) refreshLabel(); }

        void setMaxCharsPerLine(int val) { _maxCharsPerLine = val; if (_isVisualsInitialized) refreshLabel(); }
        int getMaxCharsPerLine() const { return _maxCharsPerLine; }
        void setVisualOffset(float x, float y, float z) { _visualOffsetX = x; _visualOffsetY = y; _visualOffsetZ = z; }
        void setOverlayNif(const std::string& nifPath, float x, float y, float z, float scaleMult = 1.0f)
        {
            _overlayNifPath = nifPath;
            _overlayOffsetX = x;
            _overlayOffsetY = y;
            _overlayOffsetZ = z;
            _overlayScaleMult = scaleMult;
        }

        /// Refreshes the 3D text label using character NIFs
        void refreshLabel();

        /// Load visual meshes post-construction (vtable is ready)
        void initializeVisuals() override;
        
        /// Trigger entrance scale animation if allowed
        void triggerEntranceAnimation(float& accumDelay) override;

        /// Disable the pop-in scale animation (for inventory items where it causes visual noise)
        void setNoPopAnimation(bool val) { _noPopAnimation = val; }

        // --- Equipped indicator ---
        /// Show or hide the DragonBoardVR/isEquipped.nif indicator overlay
        void setEquipped(bool equipped);
        bool isEquipped() const { return _isEquipped; }

        // --- Dynamic label offset (for inventory/magic/favorites containers) ---
        /// When true, uses settings.labelYOffsetDynamic (Y axis) instead of the standard label position
        void setUseDynamicLabelOffset(bool val) { _useDynamicLabelOffset = val; }
        bool useDynamicLabelOffset() const { return _useDynamicLabelOffset; }

        // --- Hover-only labels (for item grids) ---
        /// When true, the label nodes stay hidden unless this button is currently hovered by the laser.
        void setShowLabelsOnHoverOnly(bool val);
        bool showLabelsOnHoverOnly() const { return _showLabelsOnHoverOnly; }
        void setItemTransformSource(ItemUtils::ItemTransformSource source)
        {
            _itemTransformSource = source;
        }
        
        // --- Dashboard / HIGGS Proximity Equip ---
        void setDashboardPinned(bool pinned);
        bool isDashboardPinned() const { return _isDashboardPinned; }
        bool isInHiggsProximityForHand(bool leftHand) const
        {
            return _wasInHiggsProximity && _higgsProximityIsLeft == leftHand;
        }
        void setAmbientWiggleEnabled(bool enabled) { _ambientWiggleEnabled = enabled; }
        bool isAmbientWiggleEnabled() const { return _ambientWiggleEnabled; }
        void setWorldLockedToHeadSpace(bool enabled, const RE::NiPoint3& worldPos = RE::NiPoint3{},
                                       const RE::NiMatrix3& worldRot = RE::NiMatrix3{}, float worldScale = 1.0f,
                                       const RE::NiPoint3& headWorldPos = RE::NiPoint3{});
        bool isWorldLockedToHeadSpace() const { return _worldLockedToHeadSpace; }
        void setItemRotationPersistence(uint32_t formID, float posX, float posY, float posZ, float scaleMult,
                                        float baseRotX = 0.0f, float baseRotY = 0.0f, float baseRotZ = 0.0f)
        {
            _persistItemRotationOnGrab = (formID != 0);
            _itemOverrideFormID = formID;
            _persistItemPosX = posX;
            _persistItemPosY = posY;
            _persistItemPosZ = posZ;
            _persistItemScale = scaleMult;
            _persistItemBaseRotX = baseRotX;
            _persistItemBaseRotY = baseRotY;
            _persistItemBaseRotZ = baseRotZ;
        }
        bool persistsItemRotationOnGrab() const { return _persistItemRotationOnGrab; }
        void setItemRotationUsesLayoutEuler(bool enabled)
        {
            _persistItemRotationUsesLayoutEuler = enabled;
        }
        RE::NiNode* getPrimaryVisualNode() const { return _primaryVisualNode.get(); }
        void setPrimaryVisualIdentityOnLoad(bool enabled);
        void setPrimaryVisualTransform(const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scaleMult);
        bool getPrimaryVisualTransform(
            RE::NiPoint3& pos,
            RE::NiMatrix3& rot,
            float& scaleMult) const;

    private:

        std::string _label;
        std::string _buttonId;
        std::string _sublabel;
        std::string _nifPath;
        std::string _texturePath;
        std::string _overlayNifPath;

        // Per-item visual overrides (set via extended constructor, NAN = use INI setting)
        float _itemRotOverrideX = std::numeric_limits<float>::quiet_NaN();
        float _itemRotOverrideY = std::numeric_limits<float>::quiet_NaN();
        float _itemRotOverrideZ = std::numeric_limits<float>::quiet_NaN();
        float _itemXOffset = 0.0f;       // X offset applied to the model node
        float _itemYOffset = 0.0f;       // Y offset applied to the model node
        float _itemZOffset = 0.0f;       // Z offset applied to the model node
        float _itemScaleMult = 1.0f;     // Scale multiplier on top of the normal item scale
        ItemUtils::ItemTransformSource _itemTransformSource = ItemUtils::ItemTransformSource::TypeFallback;
        float _visualOffsetX = 0.0f;
        float _visualOffsetY = 0.0f;
        float _visualOffsetZ = 0.0f;
        float _overlayOffsetX = 0.0f;
        float _overlayOffsetY = 0.0f;
        float _overlayOffsetZ = 0.0f;
        float _overlayScaleMult = 1.0f;
        int _maxCharsPerLine = 12;
        
        RE::NiPointer<RE::NiNode> _labelNode;
        RE::NiPointer<RE::NiNode> _sublabelNode;
        RE::NiPointer<RE::NiNode> _primaryVisualNode;
        RE::NiPointer<RE::NiNode> _animatedVisualRoot;
        float _primaryVisualReferenceScale = 1.0f;
        std::vector<RE::NiPointer<RE::NiAVObject>> _labelCharNodes;
        std::vector<RE::NiPointer<RE::NiAVObject>> _sublabelCharNodes;
        RE::NiPoint3 _grabInitialLabelPos{ 0.0f, 0.0f, 0.0f };
        RE::NiMatrix3 _grabInitialLabelRot{};
        float _grabInitialLabelScale = 1.0f;
        RE::NiPoint3 _grabInitialSublabelPos{ 0.0f, 0.0f, 0.0f };
        RE::NiMatrix3 _grabInitialSublabelRot{};
        float _grabInitialSublabelScale = 1.0f;

        ButtonState _state = ButtonState::Normal;
        float _targetScale = 1.0f;   // Target scale for smooth lerp
        float _currentScale = 1.0f;  // Current interpolated scale
        int _slotIndex = -1;
        int _fixedWidgetIndex = -1;


        bool _isVisualsInitialized = true;
        float _deferInitTimer = 0.0f;
        bool _primaryVisualIdentityOnLoad = false;
        bool _noPopAnimation = false;

        float _grabTimer = 0.0f;
        bool _isGrabbed = false;
        bool _canBePersistent = false;
        bool _isPersistent = false;
        bool _isTwoHandScaling = false;
        float _twoHandInitialDist = 0.0f;
        float _twoHandInitialScale = 1.0f;
        RE::NiPoint3 _grabOffsetLocalHand;
        RE::NiPoint3 _grabInitialHandPos;
        RE::NiPoint3 _grabInitialButtonPos;
        RE::NiPoint3 _grabInitialLocalButtonPos;
        float _grabInitialLocalButtonScale = 1.0f;
        RE::NiMatrix3 _grabInitialLocalButtonRot;
        RE::NiMatrix3 _grabInitialHandRot;
        RE::NiMatrix3 _grabInitialButtonRot;
        RE::NiPoint3 _grabInitialEditableLocalPos{ 0.0f, 0.0f, 0.0f };
        float _grabInitialEditableLocalScale = 1.0f;
        RE::NiMatrix3 _grabInitialEditableLocalRot{};
        RE::NiMatrix3 _grabInitialEditableWorldRot{};

        PressCallback _onPressHandler;
        PressCallback _onReleaseHandler;
        PressCallback _onLongPressHandler;
        HoverCallback _onHoverHandler;
        PressCallback _onGripDragHandler;
        PressCallback _onSecondaryPressHandler;
        PressCallback _onSecondaryLongPressHandler;
        std::function<void(VRUIButton*)> _onGrabReleaseHandler;
        
        bool _isGripDragging = false;
        RE::NiPoint3 _gripDragStartHandPos;

        // Equipped item indicator (DragonBoardVR/isEquipped.nif overlay)
        bool _isEquipped = false;
        RE::NiPointer<RE::NiNode> _equippedIndicatorNode;

        // Dynamic label offset (for inventory/magic/favorites containers)
        bool _useDynamicLabelOffset = false;
        bool _showLabelsOnHoverOnly = false;
        bool _isLaserHovered = false;
        bool _ambientWiggleEnabled = false;
        bool _worldLockedToHeadSpace = false;
        RE::NiPoint3 _lockedWorldPos{ 0.0f, 0.0f, 0.0f };
        RE::NiMatrix3 _lockedWorldRot{};
        float _lockedWorldScale = 1.0f;
        RE::NiPoint3 _headAnchorLocalPos{ 0.0f, 0.0f, 0.0f };
        RE::NiMatrix3 _headAnchorLocalRot{};
        bool _hasHeadAnchorLocalTransform = false;
        
        bool _isDashboardPinned = false;
        bool _wasDominantGripDown = false;
        bool _wasNonDominantGripDown = false;
        bool _persistItemRotationOnGrab = false;
        bool _persistItemRotationUsesLayoutEuler = false;
        uint32_t _itemOverrideFormID = 0;
        float _persistItemPosX = 0.0f;
        float _persistItemPosY = 0.0f;
        float _persistItemPosZ = 0.0f;
        float _persistItemScale = 1.0f;
        float _persistItemBaseRotX = 0.0f;
        float _persistItemBaseRotY = 0.0f;
        float _persistItemBaseRotZ = 0.0f;
        
        // Proximity state: tracks per-frame hover-scale effect for HIGGS equip
        bool _wasInHiggsProximity = false; // was any hand in range last frame?
        bool _higgsProximityIsLeft = false; // which hand is in range?
        float _wiggleTime = 0.0f;
        float _wigglePhaseSeed = 0.0f;

        void updateLabelVisibility();
        RE::NiNode* getVisualParentNode() const;
        void updateAmbientWiggle(float deltaTime);
        void updateHeadLockedWorldTransform();
    };
}
