#pragma once

#include "DragonBoardVR_API.h"

#include <RE/Skyrim.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
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
        void RequestRmlWarmup();
        void Close();
        void OnDominantVrButtonEvent(bool triggerButton, bool gripButton, bool pressed);
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
        void ApplyRmlItemEditSliderChange(std::string_view id, float value);
        void ApplyItemEditDraftGameThread();
        void ExecuteItemEditActionGameThread(ItemEditAction action);
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
        std::mutex _itemEditMutex;
        ItemEditDraft _itemEditDraft;
        vrui::VRUIItemEditPanel* _itemEditBackend = nullptr;
        std::atomic<bool> _itemEditApplyPending{ false };
        std::atomic<ItemEditAction> _itemEditActionPending{ ItemEditAction::kNone };
        float _devInfoRefreshAccumulator = 0.0f;
        int _selectedDevCommand = 0;
        float _presentFps = 0.0f;
        float _presentFrameMs = 0.0f;
        int _panelDrawCalls = 0;
        bool _rmlPreviousTriggerDown = false;
        bool _deferredRmlTransformApply = false;
        std::atomic<float> _pointerU{ 0.0f };
        std::atomic<float> _pointerV{ 0.0f };
        std::atomic<bool> _triggerDown{ false };
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
