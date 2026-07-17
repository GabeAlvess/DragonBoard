#pragma once

#include "DragonBoardVR_API.h"

#include <RE/Skyrim.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace vrui
{
    class VRUIItemEditPanel;
    class VRUIInventoryContainer;
    class VRUIMagicContainer;
}

namespace dragonboard::ui::rml
{
    class DragonBoardRmlUi;
    class RmlPanelHost
    {
    public:
        static RmlPanelHost& GetSingleton();

        bool OpenSettings();
        bool OpenDeveloper();
        bool OpenItemEdit(vrui::VRUIItemEditPanel* editor);
        bool OpenMods();
        bool OpenInventory(
            vrui::VRUIInventoryContainer* inventory,
            vrui::VRUIItemEditPanel* preview);
        bool OpenMagic(
            vrui::VRUIMagicContainer* magic,
            vrui::VRUIItemEditPanel* preview);
        bool OpenJournal();
        void RequestRmlWarmup();
        void Close();
        void OnVrButtonEvent(
            bool leftHand,
            bool triggerButton,
            bool gripButton,
            bool pressed);
        [[nodiscard]] bool IsModsOpen() const;
        [[nodiscard]] bool RequestHoveredModOptions();
        [[nodiscard]] bool RequestHoveredModRemoval();
        void UpdateGameThread(float deltaTime);
        void RenderPresentThread(float deltaTime);

        DragonBoardVR_API::PanelHandle RegisterExternalPanel(
            const DragonBoardVR_API::PanelDescriptor& descriptor) noexcept;
        void UnregisterExternalPanel(DragonBoardVR_API::PanelHandle panel) noexcept;
        bool ShowExternalPanel(DragonBoardVR_API::PanelHandle panel) noexcept;
        void HideExternalPanel(DragonBoardVR_API::PanelHandle panel) noexcept;
        [[nodiscard]] bool IsExternalPanelVisible(
            DragonBoardVR_API::PanelHandle panel) const noexcept;
        bool SetExternalElementText(
            DragonBoardVR_API::PanelHandle panel,
            const char* elementId,
            const char* text) noexcept;
        bool SetExternalElementAttribute(
            DragonBoardVR_API::PanelHandle panel,
            const char* elementId,
            const char* name,
            const char* value) noexcept;
        bool SetExternalElementClass(
            DragonBoardVR_API::PanelHandle panel,
            const char* elementId,
            const char* className,
            bool enabled) noexcept;

        [[nodiscard]] bool IsOpen() const { return _visible.load(); }
        [[nodiscard]] bool IsDeveloperOpen() const;

    private:
        void ResetPanelInput();

        enum class LocalPanelMode : std::uint8_t
        {
            kSettings,
            kDeveloper,
            kItemEdit,
            kMods,
            kInventory,
            kMagic,
            kJournal,
            kExternal
        };

        enum class RenderCommandType : std::uint8_t
        {
            kRegister,
            kUnregister,
            kShow,
            kSetText,
            kSetAttribute,
            kSetClass
        };

        struct RenderCommand
        {
            RenderCommandType type = RenderCommandType::kShow;
            DragonBoardVR_API::PanelHandle panel = DragonBoardVR_API::InvalidPanel;
            std::string first;
            std::string second;
            std::string third;
            bool enabled = false;
        };

        struct ExternalPanelClient
        {
            std::string id;
            std::string documentPath;
            DragonBoardVR_API::PanelEventCallback callback = nullptr;
            void* userData = nullptr;
        };

        struct ExternalEvent
        {
            DragonBoardVR_API::PanelHandle panel = DragonBoardVR_API::InvalidPanel;
            DragonBoardVR_API::PanelEventType type = DragonBoardVR_API::PanelEventType::Click;
            std::string elementId;
            std::string value;
            float numericValue = 0.0f;
        };

        enum class ItemEditAction : std::uint8_t
        {
            kNone,
            kApplyItem,
            kApplyCategory,
            kReset,
            kBack,
            kPinDashboard,
            kPinLeftHand,
            kPinWorld,
            kToggleLabel
        };

        enum class InventoryAction : std::uint8_t
        {
            kNone,
            kSelect,
            kEquip,
            kDrop,
            kPin,
            kFavorite,
            kClose,
            kFilterWeapons,
            kFilterArmor,
            kFilterConsumables,
            kFilterQuest,
            kFilterBooks,
            kFilterMisc
        };

        enum class MagicAction : std::uint8_t
        {
            kNone,
            kSelect,
            kEquip,
            kEdit,
            kPinDashboard,
            kPinLeftHand,
            kPinWorld,
            kToggleLabel,
            kFavorite,
            kClose,
            kFilterDestruction,
            kFilterConjuration,
            kFilterRestoration,
            kFilterIllusion,
            kFilterAlteration,
            kFilterPowers,
            kFilterPassive
        };

        enum class JournalAction : std::uint8_t
        {
            kNone,
            kSelectQuest,
            kToggleTracking,
            kTrackObjective,
            kSettings,
            kClose
        };

        struct DevCommandEntry
        {
            std::string label;
            std::string command;
            std::string description;
            bool dangerous = false;
        };

        struct DevGameInfoSnapshot
        {
            std::string cellName;
            std::string worldspaceName;
            std::uint32_t cellFormId = 0;
            std::uint32_t worldspaceFormId = 0;
            float playerX = 0.0f;
            float playerY = 0.0f;
            float playerZ = 0.0f;
        };

        struct SettingsDraft
        {
            bool editModeEnabled = true;
            bool showDevButton = false;
            float menuScale = 1.0f;
            float buttonSpacingX = 2.4f;
            float buttonSpacingY = 1.2f;

            float menuOffsetX = 0.5f;
            float menuOffsetY = -18.5f;
            float menuOffsetZ = 0.0f;
            float menuRotX = -10.0f;
            float menuRotY = 36.0f;
            float menuRotZ = 0.0f;

            float buttonMeshScale = 1.5f;
            float itemMeshScale = 1.27f;
            float containerGridOffsetZ = 1.0f;
            float reticleScale = 2.5f;

            float itemWeaponScale = 1.0f;
            float itemArmorScale = 1.0f;
            float itemPotionScale = 1.0f;
            float itemFoodScale = 1.0f;
            float itemMiscScale = 1.0f;

            float labelScale = 1.0f;
            float labelSpacing = 0.2f;
            float labelXOffset = 0.0f;
            float labelYOffset = 0.3f;
            float labelZOffset = 0.0f;
        };

        struct ItemEditDraft
        {
            std::string category;
            std::string itemName;
            std::string modelPath;
            std::string sourcePanel;
            std::uint32_t formID = 0;
            float posX = 0.0f;
            float posY = 0.0f;
            float posZ = 0.0f;
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float scale = 1.0f;
            bool magicItem = false;
            bool boardPinnedToWorld = false;
            bool labelHidden = false;
            bool canPinToWorld = false;
        };

        struct InventoryEntry
        {
            std::string name;
            std::string category;
            std::string description;
            std::string equipmentMarker;
            std::string equipmentState;
            std::string editCategory;
            std::string modelPath;
            std::uint32_t formID = 0;
            std::int32_t count = 0;
            float attack = 0.0f;
            float defense = 0.0f;
            float weight = 0.0f;
            std::int32_t value = 0;
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float xOff = 0.0f;
            float yOff = 0.0f;
            float zOff = 0.0f;
            float scaleMult = 1.0f;
            bool hasAttack = false;
            bool hasDefense = false;
            bool equipped = false;
            bool equippedLeft = false;
            bool equippedRight = false;
            bool favorited = false;
            bool canEquip = false;
        };

        struct MagicEntry
        {
            std::string name;
            std::string category;
            std::string description;
            std::string modelPath;
            std::string iconPath;
            std::string castingType;
            std::string delivery;
            std::string skillLevel;
            std::string duration;
            std::string range;
            std::uint32_t formID = 0;
            float magickaCost = 0.0f;
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float xOff = 0.0f;
            float yOff = 0.0f;
            float zOff = 0.0f;
            float scaleMult = 1.0f;
            bool equipped = false;
            bool equippedLeft = false;
            bool equippedRight = false;
            bool favorited = false;
            bool canEquip = false;
        };

        struct JournalObjectiveEntry
        {
            std::uint16_t objectiveID = 0;
            std::uint32_t instanceID = 0;
            std::string text;
            std::string state;
            bool completed = false;
            bool failed = false;
            bool hasTargets = false;
        };

        struct JournalQuestEntry
        {
            std::uint32_t formID = 0;
            std::uint32_t instanceID = 0;
            std::string title;
            std::string summary;
            std::string type;
            bool active = false;
            bool completed = false;
            bool failed = false;
            std::vector<JournalObjectiveEntry> objectives;
        };

        struct JournalStatEntry
        {
            std::string label;
            std::string value;
        };

        RmlPanelHost() = default;
        ~RmlPanelHost();

        // Render thread and physical DragonBoard host.
        bool EnsurePresentHookInstalled();
        bool InitializeRenderer();
        void RenderPanel(float deltaTime);
        void SyncRmlSettingsFromDraft();
        void ApplyRmlSliderChange(std::string_view id, float value);
        void SyncRmlDeveloperCommands();
        void SyncRmlDeveloperInfo();
        void SyncRmlItemEdit();
        void SyncRmlMods();
        void SyncRmlInventory();
        void SyncRmlMagic();
        void SyncRmlJournal();
        bool BeginDeveloperCommandKeyboardPresentThread();
        void UpdateDeveloperCommandKeyboardPresentThread();
        void QueueDeveloperCommandAdditionPresentThread(std::string command);
        bool BeginInventorySearchKeyboardPresentThread();
        void UpdateInventorySearchKeyboardPresentThread();
        void ApplyInventorySearchQueryPresentThread(std::string query);
        [[nodiscard]] bool TryMapInventoryVisibleIndex(
            std::size_t visibleIndex,
            std::size_t& inventoryIndex);
        [[nodiscard]] static bool InventoryEntryMatchesSearch(
            const InventoryEntry& entry,
            std::string_view query);
        bool ReconcileInventorySelectionForSearchLocked();
        bool BeginMagicSearchKeyboardPresentThread();
        void UpdateMagicSearchKeyboardPresentThread();
        void ApplyMagicSearchQueryPresentThread(std::string query);
        [[nodiscard]] bool TryMapMagicVisibleIndex(
            std::size_t visibleIndex,
            std::size_t& magicIndex);
        [[nodiscard]] static bool MagicEntryMatchesSearch(
            const MagicEntry& entry,
            std::string_view query);
        bool ReconcileMagicSelectionForSearchLocked();
        void ApplyRmlItemEditSliderChange(std::string_view id, float value);
        void ApplyItemEditDraftGameThread();
        void ExecuteItemEditActionGameThread(ItemEditAction action);
        void CaptureInventoryGameThread(bool preserveSelection);
        void UpdateInventoryPreviewGameThread();
        void ExecuteInventoryActionGameThread(
            InventoryAction action,
            std::size_t index,
            bool leftHand);
        void CaptureMagicGameThread(bool preserveSelection);
        void UpdateMagicPreviewGameThread();
        void ExecuteMagicActionGameThread(
            MagicAction action,
            std::size_t index,
            bool leftHand);
        void CaptureJournalGameThread(bool preserveSelection);
        void ExecuteJournalActionGameThread(
            JournalAction action,
            std::size_t index);
        bool SetQuestTrackedGameThread(std::uint32_t formID, bool tracked);
        void ApplyRenderCommandsPresentThread();
        void CollectExternalEventsPresentThread();
        void DispatchExternalEventsGameThread();
        bool QueueElementCommand(
            RenderCommandType type,
            DragonBoardVR_API::PanelHandle panel,
            const char* first,
            const char* second,
            const char* third = nullptr,
            bool enabled = false) noexcept;

        // Built-in Settings adapter.
        void CaptureSettingsGameThread();
        void ApplyDraftGameThread();

        // Built-in Developer adapter.
        void CaptureDevGameInfoGameThread();
        void LoadDevCommandsGameThread();
        void AddDevCommandGameThread(std::string command);
        void QueueDevCommand(const DevCommandEntry& entry);

        // Physical RmlUi surface attached to the DragonBoard scene graph.
        void UpdateSurfaceGameThread();
        bool UpdateScenePanelGameThread(RE::NiNode* backgroundNode);

        ID3D11Device* _device = nullptr;
        ID3D11DeviceContext* _context = nullptr;
        ID3D11Texture2D* _panelRenderTexture = nullptr;
        ID3D11RenderTargetView* _panelRenderTarget = nullptr;
        ID3D11ShaderResourceView* _panelShaderResource = nullptr;
        std::atomic<bool> _rendererReady{ false };
        std::atomic<bool> _rmlWarmupRequested{ false };
        std::atomic<bool> _rmlWarmupAttempted{ false };
        std::unique_ptr<dragonboard::ui::rml::DragonBoardRmlUi> _rmlUi;
        std::atomic<bool> _rmlSettingsSyncPending{ true };
        std::atomic<bool> _rmlDeveloperSyncPending{ true };
        std::atomic<bool> _rmlItemEditSyncPending{ true };
        std::atomic<bool> _rmlModsSyncPending{ true };
        std::atomic<bool> _rmlInventorySyncPending{ true };
        std::atomic<bool> _rmlMagicSyncPending{ true };
        std::atomic<bool> _rmlJournalSyncPending{ true };
        std::atomic<std::uint8_t> _pendingRmlHapticCue{ 0 };

        std::atomic<bool> _visible{ false };
        std::atomic<bool> _applyPending{ false };
        std::atomic<bool> _savePending{ false };
        std::atomic<LocalPanelMode> _localPanelMode{ LocalPanelMode::kSettings };
        std::atomic<DragonBoardVR_API::PanelHandle> _activeExternalPanel{
            DragonBoardVR_API::InvalidPanel };

        mutable std::mutex _externalMutex;
        std::unordered_map<DragonBoardVR_API::PanelHandle, ExternalPanelClient> _externalPanels;
        std::deque<RenderCommand> _renderCommands;
        std::deque<ExternalEvent> _externalEvents;
        std::atomic<DragonBoardVR_API::PanelHandle> _nextExternalPanel{ 1 };

        std::mutex _draftMutex;
        SettingsDraft _draft;
        std::mutex _devMutex;
        std::vector<DevCommandEntry> _devCommands;
        DevGameInfoSnapshot _devGameInfo;
        std::string _pendingDevCommand;
        std::string _pendingDevCommandLabel;
        bool _pendingDevCommandDangerous = false;
        std::atomic<bool> _devCommandPending{ false };
        std::string _pendingDevCommandAddition;
        std::atomic<bool> _devCommandAdditionPending{ false };
        std::atomic<bool> _developerKeyboardCloseRequested{ false };
        std::uint64_t _developerKeyboardOverlayHandle = 0;
        bool _developerKeyboardOpen = false;
        struct ModEntry { std::string label; std::string iconPath; std::string command; };
        std::mutex _modsMutex;
        std::vector<ModEntry> _mods;
        std::atomic<bool> _modsAddPending{ false };
        std::atomic<bool> _modsClosePending{ false };
        std::atomic<std::size_t> _modsActivatePending{ static_cast<std::size_t>(-1) };
        std::atomic<std::size_t> _modsOptionsPending{ static_cast<std::size_t>(-1) };
        std::atomic<std::size_t> _modsRemovePending{ static_cast<std::size_t>(-1) };
        std::atomic<std::size_t> _modsHoveredIndex{ static_cast<std::size_t>(-1) };
        std::mutex _inventoryMutex;
        std::vector<InventoryEntry> _inventoryItems;
        std::vector<std::size_t> _inventoryVisibleIndices;
        std::size_t _inventorySelectedIndex = 0;
        std::uint32_t _inventorySelectedFormID = 0;
        std::string _inventoryActiveFilter;
        std::string _inventorySearchQuery;
        std::string _inventoryPlayerName;
        std::uint16_t _inventoryPlayerLevel = 1;
        std::int32_t _inventoryGold = 0;
        float _inventoryCurrentWeight = 0.0f;
        float _inventoryCarryWeight = 0.0f;
        vrui::VRUIInventoryContainer* _inventoryBackend = nullptr;
        vrui::VRUIItemEditPanel* _inventoryPreviewBackend = nullptr;
        std::atomic<InventoryAction> _inventoryActionPending{ InventoryAction::kNone };
        std::atomic<std::size_t> _inventoryActionIndex{ 0 };
        std::atomic<bool> _inventoryActionLeftHand{ false };
        std::atomic<bool> _inventoryPreviewRefreshPending{ false };
        std::atomic<bool> _inventoryKeyboardCloseRequested{ false };
        std::uint64_t _inventoryKeyboardOverlayHandle = 0;
        bool _inventoryKeyboardOpen = false;
        float _inventoryRefreshDelay = -1.0f;
        float _inventoryPollAccumulator = 0.0f;
        std::uint64_t _inventoryStateSignature = 0;
        std::mutex _magicMutex;
        std::vector<MagicEntry> _magicItems;
        std::vector<std::size_t> _magicVisibleIndices;
        std::size_t _magicSelectedIndex = 0;
        std::uint32_t _magicSelectedFormID = 0;
        std::string _magicActiveFilter;
        std::string _magicSearchQuery;
        std::string _magicPlayerName;
        std::uint16_t _magicPlayerLevel = 1;
        float _magicCurrentMagicka = 0.0f;
        float _magicMaximumMagicka = 0.0f;
        vrui::VRUIMagicContainer* _magicBackend = nullptr;
        vrui::VRUIItemEditPanel* _magicPreviewBackend = nullptr;
        std::atomic<MagicAction> _magicActionPending{ MagicAction::kNone };
        std::atomic<std::size_t> _magicActionIndex{ 0 };
        std::atomic<bool> _magicActionLeftHand{ false };
        std::atomic<bool> _magicPreviewRefreshPending{ false };
        std::atomic<bool> _magicKeyboardCloseRequested{ false };
        std::uint64_t _magicKeyboardOverlayHandle = 0;
        bool _magicKeyboardOpen = false;
        float _magicRefreshDelay = -1.0f;
        float _magicPollAccumulator = 0.0f;
        std::uint64_t _magicStateSignature = 0;
        std::mutex _journalMutex;
        std::vector<JournalQuestEntry> _journalQuests;
        std::vector<JournalStatEntry> _journalCharacterStats;
        std::vector<JournalStatEntry> _journalSkills;
        std::vector<JournalStatEntry> _journalGeneralStats;
        std::size_t _journalSelectedIndex = 0;
        std::uint32_t _journalSelectedFormID = 0;
        std::uint32_t _journalSelectedInstanceID = 0;
        std::string _journalPlayerName;
        std::uint16_t _journalPlayerLevel = 1;
        std::atomic<JournalAction> _journalActionPending{ JournalAction::kNone };
        std::atomic<std::size_t> _journalActionIndex{ 0 };
        float _journalPollAccumulator = 0.0f;
        std::uint64_t _journalStateSignature = 0;
        std::mutex _itemEditMutex;
        ItemEditDraft _itemEditDraft;
        vrui::VRUIItemEditPanel* _itemEditBackend = nullptr;
        std::atomic<bool> _itemEditApplyPending{ false };
        std::atomic<ItemEditAction> _itemEditActionPending{ ItemEditAction::kNone };
        float _devInfoRefreshAccumulator = 0.0f;
        int _selectedDevCommand = 0;
        std::array<float, 20> _presentFrameTimeHistory{};
        std::size_t _presentFrameTimeHistoryIndex = 0;
        std::size_t _presentFrameTimeHistoryCount = 0;
        float _presentFrameTimeHistorySum = 0.0f;
        float _presentFps = 0.0f;
        float _presentFrameMs = 0.0f;
        int _panelDrawCalls = 0;
        bool _rmlPreviousTriggerDown = false;
        bool _deferredRmlTransformApply = false;
        std::atomic<float> _pointerU{ 0.0f };
        std::atomic<float> _pointerV{ 0.0f };
        std::atomic<bool> _triggerDown{ false };
        std::atomic<bool> _leftTriggerDown{ false };
        std::atomic<bool> _rightTriggerDown{ false };
        std::atomic<bool> _lastTriggerWasLeft{ false };
        std::atomic<bool> _gripDown{ false };
        std::atomic<float> _stickX{ 0.0f };
        std::atomic<float> _stickY{ 0.0f };
        bool _scenePanelVisible = false;
        std::atomic<bool> _pointerInHostedPanel{ false };
        RE::NiPointer<RE::NiNode> _screenNode;
        RE::NiPointer<RE::NiSourceTexture> _screenSourceTexture;
        RE::BSGraphics::Texture* _originalRendererTexture = nullptr;
        std::unique_ptr<RE::BSGraphics::Texture> _sceneTextureBridge;
    };
}
