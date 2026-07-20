#pragma once

#include "DragonBoardVR_API.h"
#include "ui/rml/RmlInputBridge.h"
#include "ui/rml/RmlInventoryPresenter.h"
#include "ui/rml/RmlJournalPresenter.h"
#include "ui/rml/RmlMagicPresenter.h"
#include "ui/rml/RmlPerformanceMetrics.h"
#include "ui/rml/RmlEntranceAnimation.h"
#include "ui/rml/RmlRenderScheduler.h"
#include "ui/rml/RmlSurfaceGrabController.h"
#include "vrui/MapCalibration.h"

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
#include <utility>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace vrui
{
    class VRUIItemEditPanel;
    class VRUIWidget;
    class VRUIInventoryContainer;
    class VRUIMagicContainer;
}

namespace dragonboard::ui::rml
{
    class DragonBoardRmlUi;
    class StatusWidget;
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
        [[nodiscard]] std::shared_ptr<vrui::VRUIWidget> GetPreviewInteractionTarget();
        void UpdateGameThread(float deltaTime);
        void RenderPresentThread(float deltaTime);

        DragonBoardVR_API::PanelHandle RegisterExternalPanel(
            const DragonBoardVR_API::PanelDescriptor& descriptor) noexcept;
        void UnregisterExternalPanel(DragonBoardVR_API::PanelHandle panel) noexcept;
        bool ShowExternalPanel(DragonBoardVR_API::PanelHandle panel) noexcept;
        void HideExternalPanel(DragonBoardVR_API::PanelHandle panel) noexcept;
        [[nodiscard]] bool IsExternalPanelVisible(
            DragonBoardVR_API::PanelHandle panel) const noexcept;
        [[nodiscard]] DragonBoardVR_API::PanelState GetExternalPanelState(
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

        [[nodiscard]] bool SampleFingerTouchSurface(
            const RE::NiPoint3& worldPoint,
            float& pointerU,
            float& pointerV,
            float& signedDistance) const;
        void SetFingerTouchInput(
            bool active,
            bool pointerOnPanel,
            float pointerU,
            float pointerV,
            bool pressed,
            bool scrolling,
            bool leftHand);

        [[nodiscard]] bool IsOpen() const { return _visible.load(); }
        [[nodiscard]] bool IsSettingsOpen() const;
        [[nodiscard]] bool IsDeveloperOpen() const;
        [[nodiscard]] bool IsInventoryOpen() const;
        [[nodiscard]] bool IsMagicOpen() const;
        [[nodiscard]] bool IsJournalOpen() const;
        void RequestQuestMarkerRestore();

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
            DragonBoardVR_API::PanelState state = DragonBoardVR_API::PanelState::Queued;
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
            std::array<vrui::MapCalibrationPoint, vrui::kMapCalibrationPointCount> mapCalibrationPoints{};
        };

        struct SettingsDraft
        {
            bool editModeEnabled = true;
            bool showDevButton = false;
            bool worldPinned = false;
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

        using InventoryEntry = RmlInventoryPresenter::Entry;

        using MagicEntry = RmlMagicPresenter::Entry;

        using JournalObjectiveEntry = RmlJournalPresenter::Objective;
        using JournalQuestEntry = RmlJournalPresenter::Quest;
        using JournalStatEntry = RmlJournalPresenter::Stat;

        RmlPanelHost();
        ~RmlPanelHost();

        // Render thread and physical DragonBoard host.
        bool EnsurePresentHookInstalled();
        bool InitializeRenderer();
        bool EnsureRenderTargetSizePresentThread(std::uint32_t width, std::uint32_t height);
        void AdvanceRmlPrewarmPresentThread();
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
        bool BeginMagicSearchKeyboardPresentThread();
        void UpdateMagicSearchKeyboardPresentThread();
        void ApplyMagicSearchQueryPresentThread(std::string query);
        [[nodiscard]] bool TryMapMagicVisibleIndex(
            std::size_t visibleIndex,
            std::size_t& magicIndex);
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
            std::uint32_t formID,
            std::uint32_t instanceID,
            std::uint32_t objectiveInstanceID,
            std::uint16_t objectiveID);
        bool SetQuestTrackedGameThread(std::uint32_t formID, bool tracked);
        bool CacheQuestObjectiveTargetGameThread(
            std::uint32_t formID,
            std::uint32_t instanceID,
            std::uint16_t objectiveID);
        void RefreshTrackedQuestObjectiveGameThread();
        void RefreshMovingQuestTargetGameThread();
        bool RestoreQuestMarkerGameThread();
        void PersistQuestMarkerSelectionGameThread();
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
        void CaptureMapCalibrationGameThread(std::size_t cityIndex, float pointerU, float pointerV);
        [[nodiscard]] bool PanelUvToMapMarkerLocal(
            float pointerU, float pointerV, float& panelX, float& panelY) const;
        void LoadDevCommandsGameThread();
        void AddDevCommandGameThread(std::string command);
        void QueueDevCommand(const DevCommandEntry& entry);

        // Physical RmlUi surface attached to the DragonBoard scene graph.
        void UpdateSurfaceGameThread();
        void UpdateStatusSurfaceHoverGameThread();
        void UpdateSurfaceGrabsGameThread(float deltaTime);
        void CaptureStatusSurfaceGameThread(float deltaTime);
        void RenderStatusSurfacePresentThread();
        bool EnsureStatusRenderTargetPresentThread();
        bool UpdateStatusSceneSurfaceGameThread(RE::NiNode* backgroundNode);
        bool UpdateScenePanelGameThread(RE::NiNode* backgroundNode);

        ID3D11Device* _device = nullptr;
        ID3D11DeviceContext* _context = nullptr;
        ID3D11Texture2D* _panelRenderTexture = nullptr;
        ID3D11RenderTargetView* _panelRenderTarget = nullptr;
        ID3D11ShaderResourceView* _panelShaderResource = nullptr;
        std::uint32_t _panelWidth = 1920;
        std::uint32_t _panelHeight = 1080;
        std::atomic<bool> _rendererReady{ false };
        std::atomic<bool> _rmlWarmupRequested{ false };
        std::atomic<bool> _rmlWarmupAttempted{ false };
        std::size_t _rmlPrewarmStep = 0;
        std::size_t _rmlPrewarmFrameCount = 0;
        std::int64_t _rmlPrewarmTotalMs = 0;
        bool _rmlPrewarmComplete = false;
        std::unique_ptr<dragonboard::ui::rml::DragonBoardRmlUi> _rmlUi;
        std::unique_ptr<dragonboard::ui::rml::StatusWidget> _statusWidget;
        std::atomic<bool> _rmlSettingsSyncPending{ true };
        std::atomic<bool> _rmlDeveloperSyncPending{ true };
        std::atomic<bool> _rmlDeveloperInfoSyncPending{ true };
        std::atomic<bool> _rmlItemEditSyncPending{ true };
        std::atomic<bool> _rmlModsSyncPending{ true };
        std::atomic<bool> _rmlInventorySyncPending{ true };
        std::atomic<bool> _rmlMagicSyncPending{ true };
        std::atomic<bool> _rmlJournalSyncPending{ true };
        RmlInputBridge _inputBridge;
        bool _fingerTouchInputActive = false;
        bool _fingerTouchPointerOnPanel = false;
        bool _fingerTouchPressed = false;
        bool _fingerTouchScrolling = false;
        bool _fingerTouchLeftHand = false;
        float _fingerTouchPointerU = 0.0f;
        float _fingerTouchPointerV = 0.0f;

        std::atomic<bool> _visible{ false };
        std::atomic<bool> _applyPending{ false };
        std::atomic<bool> _savePending{ false };
        std::atomic<bool> _worldPinTogglePending{ false };
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
        std::atomic<std::size_t> _mapCalibrationCityPending{ static_cast<std::size_t>(-1) };
        std::atomic<float> _mapCalibrationPointerU{ 0.0f };
        std::atomic<float> _mapCalibrationPointerV{ 0.0f };
        std::atomic<bool> _mapCalibrationResetPending{ false };
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
        RmlInventoryPresenter _inventoryPresenter;
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
        std::mutex _magicMutex;
        RmlMagicPresenter _magicPresenter;
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
        RmlJournalPresenter _journalPresenter;
        std::atomic<JournalAction> _journalActionPending{ JournalAction::kNone };
        std::atomic<std::uint32_t> _journalActionFormID{ 0 };
        std::atomic<std::uint32_t> _journalActionInstanceID{ 0 };
        std::atomic<std::uint32_t> _journalActionObjectiveInstanceID{ 0 };
        std::atomic<std::uint16_t> _journalActionObjectiveID{ 0 };
        float _journalPollAccumulator = 0.0f;
        float _questTargetResolveDelay = -1.0f;
        std::uint32_t _questTargetResolveFormID = 0;
        std::uint32_t _questTargetResolveInstanceID = 0;
        std::uint16_t _questTargetResolveObjectiveID = 0;
        std::uint8_t _questTargetResolveAttempts = 0;
        float _questMarkerPollAccumulator = 0.0f;
        std::uint32_t _questMarkerWatchFormID = 0;
        std::uint32_t _questMarkerWatchQuestInstanceID = 0;
        std::uint32_t _questMarkerWatchObjectiveInstanceID = 0;
        std::uint16_t _questMarkerWatchObjectiveID = 0;
        float _questMovingTargetPollAccumulator = 0.0f;
        RE::ObjectRefHandle _questMovingTargetHandle{};
        float _questMarkerRestoreDelay = -1.0f;
        std::uint8_t _questMarkerRestoreAttempts = 0;
        std::mutex _itemEditMutex;
        ItemEditDraft _itemEditDraft;
        vrui::VRUIItemEditPanel* _itemEditBackend = nullptr;
        std::atomic<bool> _itemEditApplyPending{ false };
        std::atomic<ItemEditAction> _itemEditActionPending{ ItemEditAction::kNone };
        float _devInfoRefreshAccumulator = 0.0f;
        float _developerInfoPresentAccumulator = 0.0f;
        int _selectedDevCommand = 0;
        RmlPerformanceMetrics _performanceMetrics;
        RmlEntranceAnimation _entranceAnimation;
        RmlRenderScheduler _renderScheduler;
        bool _rmlWasVisiblePresentThread = false;
        bool _entranceInputSuppressedPresentThread = false;
        std::optional<LocalPanelMode> _lastRmlPanelModePresentThread;
        DragonBoardVR_API::PanelHandle _lastRmlExternalPanelPresentThread =
            DragonBoardVR_API::InvalidPanel;
        bool _deferredRmlTransformApply = false;
        bool _scenePanelVisible = false;
        struct StatusSurfaceSnapshot
        {
            std::int32_t gold = 0;
            float weight = 0.0f;
            float capacity = 0.0f;
            std::string location = "SKYRIM";
        };
        std::mutex _statusSurfaceMutex;
        StatusSurfaceSnapshot _statusSurfaceSnapshot;
        std::atomic<bool> _statusSurfaceDataPending{ true };
        std::atomic<bool> _statusSurfaceHomeVisible{ false };
        float _statusSurfacePollAccumulator = 0.0f;
        struct SurfacePointerState
        {
            std::atomic<float> u{ 0.5f };
            std::atomic<float> v{ 0.5f };
            std::atomic<bool> visible{ false };
        };
        struct SurfaceState
        {
            explicit SurfaceState(
                std::string surfaceId,
                std::uint32_t surfaceFlags = DragonBoardVR_API::DefaultSurfaceFeatures) :
                id(std::move(surfaceId)),
                flags(surfaceFlags),
                pointer(std::make_unique<SurfacePointerState>())
            {}
            SurfaceState(SurfaceState&&) noexcept = default;
            SurfaceState& operator=(SurfaceState&&) noexcept = default;
            SurfaceState(const SurfaceState&) = delete;
            SurfaceState& operator=(const SurfaceState&) = delete;

            std::string id;
            RE::NiPointer<RE::NiNode> node;
            RE::NiPointer<RE::NiNode> visualNode;
            RE::NiPointer<RE::BSLightingShaderProperty> shaderProperty;
            RE::NiPointer<RE::NiSourceTexture> sourceTexture;
            const void* geometryRendererData = nullptr;
            RE::BSGraphics::Texture* originalRendererTexture = nullptr;
            std::unique_ptr<RE::BSGraphics::Texture> textureBridge;
            ID3D11Texture2D* renderTexture = nullptr;
            ID3D11RenderTargetView* renderTarget = nullptr;
            ID3D11ShaderResourceView* shaderResource = nullptr;
            std::uint32_t textureWidth = 0;
            std::uint32_t textureHeight = 0;
            std::uint32_t flags = DragonBoardVR_API::DefaultSurfaceFeatures;
            bool sceneVisible = false;
            bool pointerHovered = false;
            std::unique_ptr<SurfacePointerState> pointer;
            RmlSurfaceGrabController grabController;
            DragonBoardVR_API::SurfaceEventCallback callback = nullptr;
            void* userData = nullptr;
        };
        static constexpr DragonBoardVR_API::SurfaceHandle kMainSurfaceHandle = 1;
        static constexpr DragonBoardVR_API::SurfaceHandle kStatusSurfaceHandle = 2;
        [[nodiscard]] SurfaceState& MainSceneSurface();
        [[nodiscard]] const SurfaceState& MainSceneSurface() const;
        [[nodiscard]] SurfaceState& StatusSceneSurface();
        static bool HasSurfaceFlag(
            const SurfaceState& surface,
            DragonBoardVR_API::SurfaceFlags flag);
        static void SetSurfacePointer(
            SurfaceState& surface, float u, float v, bool visible);
        static void RegisterAndApplySurfaceTransform(SurfaceState& surface);
        static void PersistSurfaceTransform(const SurfaceState& surface);
        std::unordered_map<DragonBoardVR_API::SurfaceHandle, SurfaceState> _sceneSurfaces;
    };
}
