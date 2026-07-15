#pragma once

#include "ImGuiVRHelperTypes.h"

#include <RE/Skyrim.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ImGuiContext;

namespace ImGuiVRHelperPluginAPI
{
    struct Frame;
    struct IImGuiVRHelperInterface001;
    struct IImGuiVRHelperInterface006;
    struct IImGuiVRHelperInterface007;
    struct IImGuiVRHelperInterface008;
    struct IImGuiVRHelperInterface010;
}

namespace dragonboard::ui::rml
{
    class DragonBoardRmlUi;
}

namespace vrui
{
    class VRUIItemEditPanel;
}

namespace dragonboard::ui::imgui
{
    class DragonBoardSettingsMenu
    {
    public:
        static DragonBoardSettingsMenu& GetSingleton();

        bool Connect();
        bool Open();
        bool OpenDev();
        bool OpenItemEdit(vrui::VRUIItemEditPanel* editor);
        void RequestRmlWarmup();
        void Close();
        void CloseHostedPanel();
        void OnDominantVrButtonEvent(bool triggerButton, bool gripButton, bool pressed);
        void UpdateGameThread(float deltaTime);
        void RenderPresentThread(float deltaTime);

        [[nodiscard]] bool IsConnected() const { return _connected.load(); }
        [[nodiscard]] bool IsOpen() const { return _visible.load(); }
        [[nodiscard]] bool IsDevOpen() const;

    private:
        void ResetStandaloneInput();

        enum class LocalPanelMode : std::uint8_t
        {
            kSettings,
            kDeveloper,
            kItemEdit
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

        DragonBoardSettingsMenu() = default;
        ~DragonBoardSettingsMenu();

        static void OnHelperFrame(const ImGuiVRHelperPluginAPI::Frame* frame, void* user);

        // Standalone renderer and physical DragonBoard host.
        bool EnsurePresentHookInstalled();
        bool InitializeStandaloneRenderer();
        void RenderStandalone(float deltaTime);
        void PumpStandaloneInput(float width, float height);
        void DrawLocalPanel();
        void DrawItemEditPanel();
        void SyncRmlSettingsFromDraft();
        void ApplyRmlSliderChange(std::string_view id, float value);
        void SyncRmlDeveloperCommands();
        void SyncRmlDeveloperInfo();
        void SyncRmlItemEdit();
        void ApplyRmlItemEditSliderChange(std::string_view id, float value);
        void ApplyItemEditDraftGameThread();
        void ExecuteItemEditActionGameThread(ItemEditAction action);

        // Settings view. Implemented in DragonBoardSettingsView.cpp.
        void DrawSettings();
        void CaptureSettingsGameThread();
        void ApplyDraftGameThread();
        void MarkChanged();

        // Developer view. Implemented in DragonBoardDeveloperView.cpp.
        void DrawDeveloperPanel();
        void CaptureDevGameInfoGameThread();
        void LoadDevCommandsGameThread();
        void QueueDevCommand(const DevCommandEntry& entry);

        // Physical surface integration for local and externally hosted panels.
        void UpdateClientSurfaceGameThread();
        bool UpdateScenePanelGameThread(RE::NiNode* backgroundNode);
        void SetExternalHostGameThread(bool enabled);

        ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface001* _helper = nullptr;
        ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface006* _helper006 = nullptr;
        ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface007* _helper007 = nullptr;
        ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface008* _helper008 = nullptr;
        ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface010* _helper010 = nullptr;
        uint32_t _clientId = 0;

        ImGuiContext* _imguiContext = nullptr;
        ID3D11Device* _device = nullptr;
        ID3D11DeviceContext* _context = nullptr;
        ID3D11Texture2D* _settingsTexture = nullptr;
        ID3D11RenderTargetView* _settingsRtv = nullptr;
        ID3D11ShaderResourceView* _settingsSrv = nullptr;
        std::atomic<bool> _rendererReady{ false };
        std::atomic<bool> _rmlWarmupRequested{ false };
        std::atomic<bool> _rmlWarmupAttempted{ false };
        std::unique_ptr<dragonboard::ui::rml::DragonBoardRmlUi> _rmlUi;
        std::atomic<bool> _useRmlSettings{ true };
        std::atomic<bool> _rmlSettingsSyncPending{ true };
        std::atomic<bool> _useRmlDeveloper{ true };
        std::atomic<bool> _rmlDeveloperSyncPending{ true };
        std::atomic<bool> _useRmlItemEdit{ true };
        std::atomic<bool> _rmlItemEditSyncPending{ true };
        std::atomic<std::uint8_t> _pendingRmlHapticCue{ 0 };

        std::atomic<bool> _connected{ false };
        std::atomic<bool> _helperConnectionAttempted{ false };
        std::atomic<bool> _visible{ false };
        std::atomic<bool> _applyPending{ false };
        std::atomic<bool> _savePending{ false };
        std::atomic<bool> _closePending{ false };
        std::atomic<bool> _menuOnLeftHand{ true };
        std::atomic<LocalPanelMode> _localPanelMode{ LocalPanelMode::kSettings };

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
        int _settingsPage = 0;
        int _developerPage = 0;

        bool _previousTriggerDown = false;
        bool _rmlPreviousTriggerDown = false;
        bool _deferredRmlTransformApply = false;
        float _scrollAccumulatorX = 0.0f;
        float _scrollAccumulatorY = 0.0f;
        std::atomic<float> _pointerU{ 0.0f };
        std::atomic<float> _pointerV{ 0.0f };
        std::atomic<bool> _standaloneTriggerDown{ false };
        std::atomic<bool> _standaloneGripDown{ false };
        std::atomic<float> _standaloneStickX{ 0.0f };
        std::atomic<float> _standaloneStickY{ 0.0f };
        bool _pointerWasOnPanel = false;
        bool _surfaceSubmitted = false;
        bool _hostSizeSubmitted = false;
        bool _externalHostEnabled = false;
        bool _scenePanelVisible = false;
        std::atomic<bool> _pointerInHostedPanel{ false };
        uint32_t _hostedClientId = 0;
        RE::NiPointer<RE::NiNode> _screenNode;
        RE::NiPointer<RE::NiSourceTexture> _screenSourceTexture;
        RE::BSGraphics::Texture* _originalRendererTexture = nullptr;
        std::unique_ptr<RE::BSGraphics::Texture> _sceneTextureBridge;
        ImGuiVRHelperPluginAPI::PanelTextureHandle _panelTexture{};
        ID3D11Texture2D* _boundTexture = nullptr;
        ID3D11ShaderResourceView* _boundSrv = nullptr;
    };
}
