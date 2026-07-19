#pragma once

#include "ui/rml/RmlVirtualList.h"

#include <memory>
#include <optional>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace Rml
{
    class Context;
    class Element;
    class ElementDocument;
}

namespace dragonboard::ui::rml
{
    class DragonBoardRmlRenderer;

    class DragonBoardRmlUi
    {
    public:
        enum class PanelEventType : std::uint8_t
        {
            kClick,
            kChange
        };

        struct PanelEvent
        {
            std::uint32_t panel = 0;
            PanelEventType type = PanelEventType::kClick;
            std::string elementId;
            std::string value;
            float numericValue = 0.0f;
        };

        enum class HapticCue : std::uint8_t
        {
            kNone = 0,
            kHover,
            kSliderTick,
            kPress,
            kStrong,
            kError
        };

        struct SliderChange
        {
            std::string id;
            float value = 0.0f;
        };

        struct DeveloperCommand
        {
            std::string label;
            std::string command;
            std::string description;
            bool dangerous = false;
        };

        struct DeveloperInfo
        {
            struct TimingStats
            {
                float lastMs = 0.0f;
                float averageMs = 0.0f;
                float p95Ms = 0.0f;
                float p99Ms = 0.0f;
            };

            float fps = 0.0f;
            float frameTimeMs = 0.0f;
            TimingStats present;
            TimingStats update;
            TimingStats beginFrame;
            TimingStats render;
            TimingStats endFrame;
            TimingStats dx11State;
            TimingStats dx11RenderTargets;
            TimingStats dx11ViewportScissor;
            TimingStats dx11Rasterizer;
            TimingStats dx11BlendDepth;
            TimingStats dx11InputAssembly;
            TimingStats dx11Shaders;
            TimingStats dx11Resources;
            TimingStats total;
            int panelDrawCalls = 0;
            std::size_t domElements = 0;
            float rendersPerSecond = 0.0f;
            std::uint64_t cachedFrames = 0;
            int renderWidth = 0;
            int renderHeight = 0;
            std::string activeDocument;
            std::string dirtyReason;
            std::string pluginVersion;
            std::uint32_t d3dFeatureLevel = 0;
            float playerX = 0.0f;
            float playerY = 0.0f;
            float playerZ = 0.0f;
            std::string cellName;
            std::uint32_t cellFormId = 0;
            std::string worldspaceName;
            std::uint32_t worldspaceFormId = 0;
            std::array<std::string, 5> mapCalibrationStatus{};
            std::string mapCalibrationSummary;
        };

        struct RenderTiming
        {
            float updateMs = 0.0f;
            float beginFrameMs = 0.0f;
            float renderMs = 0.0f;
            float endFrameMs = 0.0f;
            float dx11StateMs = 0.0f;
            float dx11RenderTargetsMs = 0.0f;
            float dx11ViewportScissorMs = 0.0f;
            float dx11RasterizerMs = 0.0f;
            float dx11BlendDepthMs = 0.0f;
            float dx11InputAssemblyMs = 0.0f;
            float dx11ShadersMs = 0.0f;
            float dx11ResourcesMs = 0.0f;
            float totalMs = 0.0f;
            int drawCalls = 0;
            std::size_t domElements = 0;
            int width = 0;
            int height = 0;
            std::string activeDocument;
        };

        struct MapCalibrationRequest
        {
            std::size_t cityIndex = 0;
            float pointerU = 0.0f;
            float pointerV = 0.0f;
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

        enum class ModsAction : std::uint8_t { kNone, kAdd, kClose, kActivate };
        enum class InventoryAction : std::uint8_t
        {
            kNone,
            kSelect,
            kEquip,
            kDrop,
            kPin,
            kFavorite,
            kClose,
            kSearch,
            kClearSearch,
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
            kSearch,
            kClearSearch,
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

        struct JournalActionRequest
        {
            JournalAction action = JournalAction::kNone;
            std::uint32_t formID = 0;
            std::uint32_t instanceID = 0;
            std::uint32_t objectiveInstanceID = 0;
            std::uint16_t objectiveID = 0;
        };

        struct InventoryItemInfo
        {
            std::string name;
            std::string category;
            std::string description;
            std::string equipmentMarker;
            std::string equipmentState;
            std::uint32_t formID = 0;
            std::int32_t count = 0;
            float attack = 0.0f;
            float defense = 0.0f;
            float weight = 0.0f;
            std::int32_t value = 0;
            bool hasAttack = false;
            bool hasDefense = false;
            bool equipped = false;
            bool equippedLeft = false;
            bool equippedRight = false;
            bool favorited = false;
            bool canEquip = false;
        };

        struct InventoryInfo
        {
            std::vector<InventoryItemInfo> items;
            std::size_t selectedIndex = 0;
            std::string playerName;
            std::uint16_t playerLevel = 1;
            std::int32_t gold = 0;
            float currentWeight = 0.0f;
            float carryWeight = 0.0f;
            std::string activeFilter;
            std::string searchQuery;
        };

        struct MagicItemInfo
        {
            std::string name;
            std::string category;
            std::string description;
            std::string iconPath;
            std::string castingType;
            std::string delivery;
            std::string skillLevel;
            std::string duration;
            std::string range;
            std::uint32_t formID = 0;
            float magickaCost = 0.0f;
            bool equipped = false;
            bool equippedLeft = false;
            bool equippedRight = false;
            bool favorited = false;
            bool canEquip = false;
            bool hasModelPreview = false;
        };

        struct MagicInfo
        {
            std::vector<MagicItemInfo> items;
            std::size_t selectedIndex = 0;
            std::string playerName;
            std::uint16_t playerLevel = 1;
            float currentMagicka = 0.0f;
            float maximumMagicka = 0.0f;
            std::string activeFilter;
            std::string searchQuery;
            bool editModeEnabled = false;
        };

        struct JournalObjectiveInfo
        {
            std::uint16_t objectiveID = 0;
            std::uint32_t instanceID = 0;
            std::string text;
            std::string state;
            bool completed = false;
            bool failed = false;
            bool hasTargets = false;
        };

        struct JournalQuestInfo
        {
            std::uint32_t formID = 0;
            std::uint32_t instanceID = 0;
            std::string title;
            std::string summary;
            std::string type;
            bool active = false;
            bool completed = false;
            bool failed = false;
            std::vector<JournalObjectiveInfo> objectives;
        };

        struct JournalStatInfo
        {
            std::string label;
            std::string value;
        };

        struct JournalInfo
        {
            std::vector<JournalQuestInfo> quests;
            std::size_t selectedIndex = 0;
            std::string playerName;
            std::uint16_t playerLevel = 1;
            std::vector<JournalStatInfo> characterStats;
            std::vector<JournalStatInfo> skills;
            std::vector<JournalStatInfo> generalStats;
        };

        struct ItemEditInfo
        {
            std::string category;
            std::string itemName;
            std::string modelPath;
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

        DragonBoardRmlUi();
        ~DragonBoardRmlUi();

        bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
        bool LoadNextBuiltinDocument();
        [[nodiscard]] bool AreBuiltinDocumentsLoaded() const;
        void Shutdown();
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] bool IsSettingsReady() const;
        [[nodiscard]] bool IsDeveloperReady() const;
        [[nodiscard]] bool IsItemEditReady() const;
        [[nodiscard]] bool IsModsReady() const;
        [[nodiscard]] bool IsInventoryReady() const;
        [[nodiscard]] bool IsMagicReady() const;
        [[nodiscard]] bool IsJournalReady() const;
        bool ShowSettings();
        bool ShowDeveloper();
        bool ShowItemEdit();
        bool ShowMods();
        bool ShowInventory();
        bool ShowMagic();
        bool ShowJournal();

        // Generic document API. These methods are called only on the Present
        // thread; RmlPanelHost owns the cross-thread command queue.
        bool RegisterPanel(
            std::uint32_t handle,
            std::string panelId,
            std::string documentPath);
        bool UnregisterPanel(std::uint32_t handle);
        [[nodiscard]] bool IsPanelReady(std::uint32_t handle) const;
        bool ShowPanel(std::uint32_t handle);
        bool SetElementText(std::uint32_t handle, const char* elementId, const char* text);
        bool SetElementAttribute(
            std::uint32_t handle,
            const char* elementId,
            const char* name,
            const char* value);
        bool SetElementClass(
            std::uint32_t handle,
            const char* elementId,
            const char* className,
            bool enabled);
        [[nodiscard]] std::optional<PanelEvent> ConsumePanelEvent();

        void ProcessInput(
            bool pointerOnPanel,
            float pointerU,
            float pointerV,
            bool triggerDown,
            bool gripDown,
            float stickX,
            float stickY,
            int width,
            int height,
            float deltaTime);
        [[nodiscard]] bool RequiresContinuousRendering() const;
        bool Render(
            ID3D11RenderTargetView* renderTarget,
            int renderWidth,
            int renderHeight,
            int logicalWidth,
            int logicalHeight);

        [[nodiscard]] bool ConsumeCloseRequested();
        [[nodiscard]] bool ConsumeSaveRequested();
        [[nodiscard]] bool ConsumeEditModeToggleRequested();
        [[nodiscard]] bool ConsumeDeveloperPanelToggleRequested();
        [[nodiscard]] bool ConsumeWorldPinToggleRequested();
        [[nodiscard]] HapticCue ConsumeHapticCue();
        [[nodiscard]] std::optional<SliderChange> ConsumeSliderChange();
        [[nodiscard]] std::optional<std::size_t> ConsumeDeveloperCommandRequested();
        [[nodiscard]] bool ConsumeDeveloperAddCommandRequested();
        [[nodiscard]] std::optional<MapCalibrationRequest> ConsumeMapCalibrationRequest();
        [[nodiscard]] bool ConsumeMapCalibrationResetRequested();
        [[nodiscard]] ItemEditAction ConsumeItemEditAction();
        [[nodiscard]] std::pair<ModsAction, std::size_t> ConsumeModsAction();
        [[nodiscard]] std::pair<InventoryAction, std::size_t> ConsumeInventoryAction();
        [[nodiscard]] std::pair<MagicAction, std::size_t> ConsumeMagicAction();
        [[nodiscard]] JournalActionRequest ConsumeJournalAction();
        [[nodiscard]] std::optional<std::size_t> GetHoveredModsIndex() const;
        [[nodiscard]] bool IsPreviewInteractionZoneHovered() const
        {
            return _previewInteractionZoneHovered;
        }
        void SetSliderValue(const char* id, float value);
        void SetItemEditInfo(const ItemEditInfo& info);
        void SetMods(const std::vector<std::string>& labels);
        void SetInventory(InventoryInfo info);
        void SetMagic(MagicInfo info);
        void SetJournal(const JournalInfo& info);
        void SetEditModeEnabled(bool enabled);
        void SetDeveloperButtonEnabled(bool enabled);
        void SetWorldPinned(bool pinned);
        void SetDeveloperCommands(
            std::vector<DeveloperCommand> commands,
            std::size_t selectedIndex = 0);
        void SetDeveloperInfo(const DeveloperInfo& info);
        [[nodiscard]] int GetLastDrawCallCount() const;
        [[nodiscard]] const RenderTiming& GetLastRenderTiming() const;
        [[nodiscard]] DragonBoardRmlRenderer* GetRenderer() const { return _renderer.get(); }

    private:
        class UiEventListener;
        class SystemLogger;

        enum class TriggerCaptureMode : std::uint8_t
        {
            kNone,
            kButton,
            kSlider
        };

        struct InteractiveBinding
        {
            Rml::ElementDocument* document = nullptr;
            std::string id;
            TriggerCaptureMode mode = TriggerCaptureMode::kNone;
        };

        struct RegisteredPanel
        {
            std::uint32_t handle = 0;
            std::string id;
            std::string documentPath;
            Rml::ElementDocument* document = nullptr;
        };

        struct VirtualListRow
        {
            Rml::Element* button = nullptr;
            Rml::Element* state = nullptr;
            Rml::Element* nameViewport = nullptr;
            Rml::Element* nameTrack = nullptr;
            Rml::Element* stack = nullptr;
            std::size_t itemIndex = static_cast<std::size_t>(-1);
            std::string contentKey;
            std::string classNames;
        };

        void BindClick(Rml::ElementDocument* document, const char* id);
        void BindSlider(Rml::ElementDocument* document, const char* id);
        void HandleClick(const char* id);
        void HandleHover(const char* id);
        void HandleSliderChange(const char* id, float value);
        void RequestHaptic(HapticCue cue);
        [[nodiscard]] HapticCue ResolveClickHaptic(const char* id) const;
        void UpdateSliderValueLabel(const char* id, float value);
        void SelectSettingsPage(const char* page);
        void SelectDeveloperPage(const char* page);
        void SelectItemEditPage(const char* page);
        void SelectJournalPage(const char* page);
        void SelectDeveloperCommand(std::size_t index);
        void UpdateDeveloperCommandDetails();
        void UpdateCursor(bool visible, int x, int y);
        [[nodiscard]] Rml::Element* FindInteractiveAtPoint(
            int x, int y, TriggerCaptureMode& mode) const;
        void RegisterInteractive(
            Rml::ElementDocument* document, const char* id, TriggerCaptureMode mode);
        void RegisterDocumentInteractives(Rml::ElementDocument* document);
        [[nodiscard]] RegisteredPanel* FindPanel(std::uint32_t handle);
        [[nodiscard]] const RegisteredPanel* FindPanel(std::uint32_t handle) const;
        [[nodiscard]] std::uint32_t FindPanelHandle(Rml::ElementDocument* document) const;
        void HideAllDocuments();
        void UpdateCapturedSlider(int pointerX);
        void UpdateInventoryMarquee(float deltaTime);
        void ResetInventoryMarquee();
        void UpdateInventoryVirtualRows(bool refreshVisibleRows = false);
        void ResetInventoryVirtualRows();
        void UpdateMagicVirtualRows(bool refreshVisibleRows = false);
        void ResetMagicVirtualRows();
        void UpdateInventoryLongPress(float deltaTime);
        void ResetInventoryLongPress();
        void BeginTriggerScrollLock();
        void RestoreTriggerScrollLock();
        void TraceScrollState();
        void CaptureGripScrollHoverLock();
        void ApplyGripScrollHoverLock();
        void ClearGripScrollHoverLock();

        std::unique_ptr<DragonBoardRmlRenderer> _renderer;
        std::unique_ptr<UiEventListener> _eventListener;
        std::unique_ptr<SystemLogger> _systemLogger;
        std::vector<unsigned char> _fontData;
        Rml::Context* _context = nullptr;
        Rml::ElementDocument* _settingsDocument = nullptr;
        Rml::ElementDocument* _developerDocument = nullptr;
        Rml::ElementDocument* _itemEditDocument = nullptr;
        Rml::ElementDocument* _modsDocument = nullptr;
        Rml::ElementDocument* _inventoryDocument = nullptr;
        Rml::ElementDocument* _magicDocument = nullptr;
        Rml::ElementDocument* _journalDocument = nullptr;
        Rml::ElementDocument* _activeDocument = nullptr;
        std::unordered_map<std::uint32_t, RegisteredPanel> _registeredPanels;
        std::deque<PanelEvent> _panelEvents;
        std::optional<MapCalibrationRequest> _mapCalibrationRequest;
        bool _mapCalibrationResetRequested = false;
        bool _rmlInitialized = false;
        std::size_t _builtinDocumentLoadStep = 0;
        bool _previousTriggerDown = false;
        bool _pointerWasOnPanel = false;
        bool _previewInteractionZoneHovered = false;
        bool _pointerSmoothingInitialized = false;
        float _smoothedPointerX = 0.0f;
        float _smoothedPointerY = 0.0f;
        int _latestPointerX = 0;
        int _latestPointerY = 0;
        int _inputWidth = 1920;
        int _inputHeight = 1080;
        std::size_t _selectedMapCalibrationCity = 0;
        bool _currentTriggerDown = false;
        bool _currentGripDown = false;
        Rml::ElementDocument* _observedScrollDocument = nullptr;
        float _observedPageScrollTop = 0.0f;
        float _observedNestedScrollTop = 0.0f;
        Rml::ElementDocument* _triggerScrollLockDocument = nullptr;
        float _triggerScrollLockPageTop = 0.0f;
        float _triggerScrollLockNestedTop = 0.0f;
        bool _triggerScrollLockActive = false;
        bool _triggerScrollReleasePending = false;
        bool _triggerScrollSuppressionLogged = false;
        TriggerCaptureMode _triggerCaptureMode = TriggerCaptureMode::kNone;
        int _triggerCaptureX = 0;
        int _triggerCaptureY = 0;
        std::string _triggerCapturedSliderId;
        bool _sliderPointerInitialized = false;
        int _sliderAcceptedPointerX = 0;
        float _sliderSmoothedPointerX = 0.0f;
        std::string _triggerCapturedActionId;
        bool _triggerCaptureProgrammatic = false;
        std::vector<InteractiveBinding> _interactiveBindings;
        bool _gripScrollActive = false;
        enum class GripScrollHoverList : std::uint8_t
        {
            kNone,
            kInventory,
            kMagic
        };
        GripScrollHoverList _gripScrollHoverList = GripScrollHoverList::kNone;
        std::size_t _gripScrollHoverItemIndex = static_cast<std::size_t>(-1);
        Rml::Element* _gripScrollTarget = nullptr;
        int _gripScrollPointerY = 0;
        float _gripScrollTargetTop = 0.0f;
        float _gripPointerScrollAccumulator = 0.0f;
        bool _closeRequested = false;
        bool _saveRequested = false;
        bool _editModeToggleRequested = false;
        bool _developerPanelToggleRequested = false;
        bool _worldPinToggleRequested = false;
        HapticCue _pendingHapticCue = HapticCue::kNone;
        std::string _hoveredElementId;
        std::string _modsListMarkup;
        std::string _developerCommandListMarkup;
        std::string _inventoryMarqueeElementId;
        RmlVirtualList _inventoryVirtualList{ 548.0f, 108.0f, 4 };
        std::vector<InventoryItemInfo> _inventoryVirtualItems;
        std::vector<VirtualListRow> _inventoryVirtualRows;
        std::size_t _inventoryVirtualSelectedIndex = static_cast<std::size_t>(-1);
        std::string _inventoryVirtualContextKey;
        bool _inventoryVirtualInitialized = false;
        RmlVirtualList _magicVirtualList{ 548.0f, 108.0f, 4 };
        std::vector<MagicItemInfo> _magicVirtualItems;
        std::vector<VirtualListRow> _magicVirtualRows;
        std::size_t _magicVirtualSelectedIndex = static_cast<std::size_t>(-1);
        std::string _magicVirtualContextKey;
        bool _magicVirtualInitialized = false;
        std::string _journalQuestListMarkup;
        std::vector<std::uint64_t> _journalActiveQuestOrder;
        float _inventoryMarqueeOffset = 0.0f;
        float _inventoryMarqueePause = 0.0f;
        bool _inventoryMarqueeAtEnd = false;
        bool _inventoryMarqueeActive = false;
        std::string _inventoryLongPressElementId;
        float _inventoryLongPressTimer = 0.0f;
        bool _inventoryLongPressTriggered = false;
        std::chrono::steady_clock::time_point _lastHoverHaptic{};
        std::chrono::steady_clock::time_point _lastSliderHaptic{};
        bool _synchronizingSliderValues = false;
        std::optional<SliderChange> _sliderChange;
        std::vector<DeveloperCommand> _developerCommands;
        std::size_t _selectedDeveloperCommand = 0;
        std::optional<std::size_t> _developerCommandRequested;
        bool _developerAddCommandRequested = false;
        ItemEditAction _itemEditAction = ItemEditAction::kNone;
        ModsAction _modsAction = ModsAction::kNone;
        std::size_t _modsActionIndex = 0;
        InventoryAction _inventoryAction = InventoryAction::kNone;
        std::size_t _inventoryActionIndex = 0;
        MagicAction _magicAction = MagicAction::kNone;
        std::size_t _magicActionIndex = 0;
        JournalAction _journalAction = JournalAction::kNone;
        std::uint32_t _journalActionFormID = 0;
        std::uint32_t _journalActionInstanceID = 0;
        std::uint32_t _journalActionObjectiveInstanceID = 0;
        std::uint16_t _journalActionObjectiveID = 0;
        std::uint32_t _journalSelectedFormID = 0;
        std::uint32_t _journalSelectedInstanceID = 0;
        RenderTiming _lastRenderTiming;
        std::chrono::steady_clock::time_point _lastDomCountSample{};
        std::size_t _lastDomElementCount = 0;
        Rml::ElementDocument* _lastDomCountDocument = nullptr;
        bool _pointerMotionActive = false;
    };
}
