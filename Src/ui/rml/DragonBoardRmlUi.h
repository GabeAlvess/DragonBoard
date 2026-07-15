#pragma once

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
            float fps = 0.0f;
            float frameTimeMs = 0.0f;
            int panelDrawCalls = 0;
            std::string pluginVersion;
            std::uint32_t d3dFeatureLevel = 0;
            float playerX = 0.0f;
            float playerY = 0.0f;
            float playerZ = 0.0f;
            std::string cellName;
            std::uint32_t cellFormId = 0;
            std::string worldspaceName;
            std::uint32_t worldspaceFormId = 0;
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
        void Shutdown();
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] bool IsSettingsReady() const;
        [[nodiscard]] bool IsDeveloperReady() const;
        [[nodiscard]] bool IsItemEditReady() const;
        bool ShowSettings();
        bool ShowDeveloper();
        bool ShowItemEdit();

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
            int height);
        bool Render(ID3D11RenderTargetView* renderTarget, int width, int height);

        [[nodiscard]] bool ConsumeCloseRequested();
        [[nodiscard]] bool ConsumeSaveRequested();
        [[nodiscard]] bool ConsumeEditModeToggleRequested();
        [[nodiscard]] bool ConsumeDeveloperPanelToggleRequested();
        [[nodiscard]] HapticCue ConsumeHapticCue();
        [[nodiscard]] std::optional<SliderChange> ConsumeSliderChange();
        [[nodiscard]] std::optional<std::size_t> ConsumeDeveloperCommandRequested();
        [[nodiscard]] ItemEditAction ConsumeItemEditAction();
        void SetSliderValue(const char* id, float value);
        void SetItemEditInfo(const ItemEditInfo& info);
        void SetEditModeEnabled(bool enabled);
        void SetDeveloperButtonEnabled(bool enabled);
        void SetDeveloperCommands(std::vector<DeveloperCommand> commands);
        void SetDeveloperInfo(const DeveloperInfo& info);
        [[nodiscard]] int GetLastDrawCallCount() const;

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
        void BeginTriggerScrollLock();
        void RestoreTriggerScrollLock();
        void TraceScrollState();

        std::unique_ptr<DragonBoardRmlRenderer> _renderer;
        std::unique_ptr<UiEventListener> _eventListener;
        std::unique_ptr<SystemLogger> _systemLogger;
        std::vector<unsigned char> _fontData;
        Rml::Context* _context = nullptr;
        Rml::ElementDocument* _settingsDocument = nullptr;
        Rml::ElementDocument* _developerDocument = nullptr;
        Rml::ElementDocument* _itemEditDocument = nullptr;
        Rml::ElementDocument* _activeDocument = nullptr;
        std::unordered_map<std::uint32_t, RegisteredPanel> _registeredPanels;
        std::deque<PanelEvent> _panelEvents;
        bool _rmlInitialized = false;
        bool _previousTriggerDown = false;
        bool _pointerWasOnPanel = false;
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
        std::string _triggerCapturedActionId;
        bool _triggerCaptureProgrammatic = false;
        std::vector<InteractiveBinding> _interactiveBindings;
        bool _gripScrollActive = false;
        Rml::Element* _gripScrollTarget = nullptr;
        int _gripScrollPointerY = 0;
        float _gripScrollTargetTop = 0.0f;
        float _gripPointerScrollAccumulator = 0.0f;
        bool _closeRequested = false;
        bool _saveRequested = false;
        bool _editModeToggleRequested = false;
        bool _developerPanelToggleRequested = false;
        HapticCue _pendingHapticCue = HapticCue::kNone;
        std::string _hoveredElementId;
        std::chrono::steady_clock::time_point _lastHoverHaptic{};
        std::chrono::steady_clock::time_point _lastSliderHaptic{};
        bool _synchronizingSliderValues = false;
        std::optional<SliderChange> _sliderChange;
        std::vector<DeveloperCommand> _developerCommands;
        std::size_t _selectedDeveloperCommand = 0;
        std::optional<std::size_t> _developerCommandRequested;
        ItemEditAction _itemEditAction = ItemEditAction::kNone;
    };
}
