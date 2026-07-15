#include "ui/rml/DragonBoardRmlUi.h"
#include "ui/rml/DragonBoardRmlRenderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/SystemInterface.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        bool IsItemEditCard(const Rml::Element* element)
        {
            if (!element) return false;
            const std::string_view id(element->GetId());
            return id == "edit-pin-dashboard" || id == "edit-pin-left" ||
                   id == "edit-pin-world" || id == "edit-toggle-label";
        }

        constexpr const char* kContextName = "dragonboard_local_panels_rml";

        constexpr std::array<const char*, 3> kDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/settings.rml",
            "SKSE/Plugins/DragonBoardVR/ui/settings.rml",
            "Assets/ui/rml/settings.rml"
        };

        constexpr std::array<const char*, 3> kDeveloperDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/dev.rml",
            "SKSE/Plugins/DragonBoardVR/ui/dev.rml",
            "Assets/ui/rml/dev.rml"
        };

        constexpr std::array<const char*, 3> kItemEditDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/edit.rml",
            "SKSE/Plugins/DragonBoardVR/ui/edit.rml",
            "Assets/ui/rml/edit.rml"
        };

        constexpr std::array<const char*, 3> kFontCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/DragonBoardVR_Font.ttf",
            "C:/Windows/Fonts/BarlowCondensed-Regular.ttf",
            "C:/Windows/Fonts/arial.ttf"
        };

        constexpr std::array<const char*, 5> kPages{
            "general", "position", "visuals", "items", "labels"
        };

        constexpr std::array<const char*, 23> kSliders{
            "menuScale", "buttonSpacingX", "buttonSpacingY",
            "menuOffsetX", "menuOffsetY", "menuOffsetZ",
            "menuRotX", "menuRotY", "menuRotZ",
            "buttonMeshScale", "itemMeshScale", "containerGridOffsetZ", "reticleScale",
            "itemWeaponScale", "itemArmorScale", "itemPotionScale", "itemFoodScale", "itemMiscScale",
            "labelScale", "labelSpacing", "labelXOffset", "labelYOffset", "labelZOffset"
        };

        constexpr std::array<const char*, 2> kDeveloperPages{
            "commands", "info"
        };

        constexpr std::array<const char*, 4> kItemEditPages{
            "position", "rotation", "scale", "pin"
        };

        constexpr std::array<const char*, 7> kItemEditSliders{
            "editPosX", "editPosY", "editPosZ",
            "editRotX", "editRotY", "editRotZ", "editScale"
        };

        std::string EscapeRml(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value) {
                switch (character) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                default: escaped += character; break;
                }
            }
            return escaped;
        }
    }

    class DragonBoardRmlUi::UiEventListener final : public Rml::EventListener
    {
    public:
        explicit UiEventListener(DragonBoardRmlUi& owner) : _owner(owner) {}

        void ProcessEvent(Rml::Event& event) override
        {
            if (event.GetType() == "click") {
                // Events can target a block-level span or text node inside a
                // button. Resolve the actual button so its full visual card,
                // including title and description, has one reliable action.
                auto* element = event.GetTargetElement();
                while (element && element->GetTagName() != "button" &&
                       element->GetTagName() != "input" && !IsItemEditCard(element)) {
                    element = element->GetParentNode();
                }
                if (element && !element->GetId().empty()) {
                    _owner.HandleClick(element->GetId().c_str());
                }
            } else if (event.GetType() == "change") {
                auto* element = event.GetTargetElement();
                while (element && element->GetTagName() != "input") {
                    element = element->GetParentNode();
                }
                if (element && !element->GetId().empty()) {
                    _owner.HandleSliderChange(
                        element->GetId().c_str(),
                        event.GetParameter<float>("value", 0.0f));
                }
            }
        }

    private:
        DragonBoardRmlUi& _owner;
    };

    class DragonBoardRmlUi::SystemLogger final : public Rml::SystemInterface
    {
    public:
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
        {
            switch (type) {
            case Rml::Log::LT_ERROR:
            case Rml::Log::LT_ASSERT:
                logger::error("DragonBoardVR RmlUi: {}", message);
                break;
            case Rml::Log::LT_WARNING:
                logger::warn("DragonBoardVR RmlUi: {}", message);
                break;
            case Rml::Log::LT_INFO:
                logger::info("DragonBoardVR RmlUi: {}", message);
                break;
            default:
                logger::debug("DragonBoardVR RmlUi: {}", message);
                break;
            }
            return true;
        }
    };

    DragonBoardRmlUi::DragonBoardRmlUi() = default;

    DragonBoardRmlUi::~DragonBoardRmlUi()
    {
        Shutdown();
    }

    bool DragonBoardRmlUi::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        if (IsReady()) return true;
        Shutdown();

        _renderer = std::make_unique<DragonBoardRmlRenderer>();
        if (!_renderer->Initialize(device, context)) {
            logger::error("DragonBoardVR: failed to initialize the minimal RmlUi DX11 renderer.");
            Shutdown();
            return false;
        }

        _systemLogger = std::make_unique<SystemLogger>();
        Rml::SetSystemInterface(_systemLogger.get());
        Rml::SetRenderInterface(_renderer.get());
        if (!Rml::Initialise()) {
            logger::error("DragonBoardVR: RmlUi core initialization failed.");
            Shutdown();
            return false;
        }
        _rmlInitialized = true;

        _context = Rml::CreateContext(kContextName, Rml::Vector2i(1920, 1080), _renderer.get());
        if (!_context) {
            logger::error("DragonBoardVR: RmlUi context creation failed.");
            Shutdown();
            return false;
        }
        // Grip motion is already sampled continuously by the VR bridge. An
        // additional smooth-scroll queue makes the page keep moving after grip
        // release and shifts click targets under the trigger.
        _context->SetDefaultScrollBehavior(Rml::ScrollBehavior::Instant, 1.0f);

        bool fontLoaded = false;
        for (const auto* path : kFontCandidates) {
            if (!std::filesystem::exists(path)) continue;
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) continue;
            const auto size = stream.tellg();
            if (size <= 0) continue;
            _fontData.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            if (!stream.read(reinterpret_cast<char*>(_fontData.data()), size)) {
                _fontData.clear();
                continue;
            }
            const Rml::Span<const Rml::byte> fontBytes(
                _fontData.data(), _fontData.size());
            if (Rml::LoadFontFace(
                    fontBytes,
                    "DragonBoard",
                    Rml::Style::FontStyle::Normal,
                    Rml::Style::FontWeight::Normal)) {
                logger::info("DragonBoardVR: RmlUi font '{}' registered as DragonBoard.", path);
                fontLoaded = true;
                break;
            }
            _fontData.clear();
        }
        if (!fontLoaded) {
            logger::error("DragonBoardVR: RmlUi could not find a usable font.");
            Shutdown();
            return false;
        }

        const char* loadedSettingsDocument = nullptr;
        for (const auto* path : kDocumentCandidates) {
            if (!std::filesystem::exists(path)) continue;
            const auto loadStarted = std::chrono::steady_clock::now();
            _settingsDocument = _context->LoadDocument(path);
            const auto loadMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadStarted).count();
            logger::info(
                "DragonBoardVR: RmlUi settings document load took {} ms (path='{}', success={}).",
                loadMs,
                path,
                _settingsDocument != nullptr);
            if (_settingsDocument) {
                loadedSettingsDocument = path;
                break;
            }
        }
        const char* loadedDeveloperDocument = nullptr;
        for (const auto* path : kDeveloperDocumentCandidates) {
            if (!std::filesystem::exists(path)) continue;
            const auto loadStarted = std::chrono::steady_clock::now();
            _developerDocument = _context->LoadDocument(path);
            const auto loadMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadStarted).count();
            logger::info(
                "DragonBoardVR: RmlUi developer document load took {} ms (path='{}', success={}).",
                loadMs,
                path,
                _developerDocument != nullptr);
            if (_developerDocument) {
                loadedDeveloperDocument = path;
                break;
            }
        }
        const char* loadedItemEditDocument = nullptr;
        for (const auto* path : kItemEditDocumentCandidates) {
            if (!std::filesystem::exists(path)) continue;
            const auto loadStarted = std::chrono::steady_clock::now();
            _itemEditDocument = _context->LoadDocument(path);
            const auto loadMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loadStarted).count();
            logger::info(
                "DragonBoardVR: RmlUi item editor document load took {} ms (path='{}', success={}).",
                loadMs, path, _itemEditDocument != nullptr);
            if (_itemEditDocument) {
                loadedItemEditDocument = path;
                break;
            }
        }
        if (!_settingsDocument && !_developerDocument && !_itemEditDocument) {
            logger::error("DragonBoardVR: no RmlUi document could be loaded.");
            Shutdown();
            return false;
        }

        _eventListener = std::make_unique<UiEventListener>(*this);
        for (auto* document : { _settingsDocument, _developerDocument, _itemEditDocument }) {
            if (!document) continue;
            document->AddEventListener("change", _eventListener.get());
        }
        if (_settingsDocument) {
            for (const auto* page : kPages) {
                const std::string tabId = std::string("tab-") + page;
                BindClick(_settingsDocument, tabId.c_str());
            }
            for (const auto* slider : kSliders) BindSlider(_settingsDocument, slider);
            BindClick(_settingsDocument, "save");
            BindClick(_settingsDocument, "close");
            BindClick(_settingsDocument, "toggle-edit-mode");
            BindClick(_settingsDocument, "toggle-dev-panel");
            SelectSettingsPage("general");
            logger::info("DragonBoardVR: external RmlUi settings loaded from '{}'.", loadedSettingsDocument);
        }
        if (_developerDocument) {
            for (const auto* page : kDeveloperPages) {
                const std::string tabId = std::string("dev-tab-") + page;
                BindClick(_developerDocument, tabId.c_str());
            }
            BindClick(_developerDocument, "dev-execute");
            BindClick(_developerDocument, "dev-close");
            SelectDeveloperPage("commands");
            _developerDocument->Hide();
            logger::info("DragonBoardVR: external RmlUi developer panel loaded from '{}'.", loadedDeveloperDocument);
        }
        if (_itemEditDocument) {
            for (const auto* page : kItemEditPages) {
                const std::string tabId = std::string("edit-tab-") + page;
                BindClick(_itemEditDocument, tabId.c_str());
            }
            for (const auto* slider : kItemEditSliders) BindSlider(_itemEditDocument, slider);
            for (const auto* id : {
                     "edit-apply-item", "edit-apply-category", "edit-reset", "edit-back", "edit-close",
                     "edit-pin-dashboard", "edit-pin-left", "edit-pin-world", "edit-toggle-label" }) {
                BindClick(_itemEditDocument, id);
            }
            SelectItemEditPage("position");
            _itemEditDocument->Hide();
            logger::info("DragonBoardVR: external RmlUi item editor loaded from '{}'.", loadedItemEditDocument);
        }
        if (_settingsDocument) {
            ShowSettings();
        } else if (_developerDocument) {
            ShowDeveloper();
        } else {
            ShowItemEdit();
        }
        return true;
    }

    void DragonBoardRmlUi::Shutdown()
    {
        _triggerScrollLockDocument = nullptr;
        _triggerScrollLockActive = false;
        _triggerScrollReleasePending = false;
        _triggerCaptureMode = TriggerCaptureMode::kNone;
        _triggerCapturedSliderId.clear();
        _triggerCapturedActionId.clear();
        _triggerCaptureProgrammatic = false;
        _interactiveBindings.clear();
        _gripScrollActive = false;
        _gripScrollTarget = nullptr;
        _gripScrollTargetTop = 0.0f;
        _gripPointerScrollAccumulator = 0.0f;
        _settingsDocument = nullptr;
        _developerDocument = nullptr;
        _itemEditDocument = nullptr;
        _activeDocument = nullptr;
        _registeredPanels.clear();
        _panelEvents.clear();
        _eventListener.reset();
        if (_context) {
            Rml::RemoveContext(kContextName);
            _context = nullptr;
        }
        if (_rmlInitialized) {
            Rml::Shutdown();
            _rmlInitialized = false;
        }
        _fontData.clear();
        _systemLogger.reset();
        if (_renderer) {
            _renderer->Shutdown();
            _renderer.reset();
        }
    }

    bool DragonBoardRmlUi::IsReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _activeDocument;
    }

    bool DragonBoardRmlUi::IsSettingsReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _settingsDocument;
    }

    bool DragonBoardRmlUi::IsDeveloperReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _developerDocument;
    }

    bool DragonBoardRmlUi::IsItemEditReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _itemEditDocument;
    }

    bool DragonBoardRmlUi::ShowSettings()
    {
        if (!IsSettingsReady()) return false;
        if (_activeDocument == _settingsDocument) return true;
        HideAllDocuments();
        _settingsDocument->Show();
        _activeDocument = _settingsDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowDeveloper()
    {
        if (!IsDeveloperReady()) return false;
        if (_activeDocument == _developerDocument) return true;
        HideAllDocuments();
        _developerDocument->Show();
        _activeDocument = _developerDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowItemEdit()
    {
        if (!IsItemEditReady()) return false;
        if (_activeDocument == _itemEditDocument) return true;
        HideAllDocuments();
        _itemEditDocument->Show();
        _activeDocument = _itemEditDocument;
        return true;
    }

    bool DragonBoardRmlUi::RegisterPanel(
        std::uint32_t handle,
        std::string panelId,
        std::string documentPath)
    {
        if (!_context || !_eventListener || handle == 0 || panelId.empty() ||
            documentPath.empty() || _registeredPanels.contains(handle)) {
            return false;
        }
        for (const auto& [existingHandle, panel] : _registeredPanels) {
            (void)existingHandle;
            if (panel.id == panelId) {
                logger::warn("DragonBoardVR: RmlUi panel id '{}' is already registered.", panelId);
                return false;
            }
        }
        if (!std::filesystem::exists(documentPath)) {
            logger::warn(
                "DragonBoardVR: RmlUi panel '{}' document does not exist: {}",
                panelId,
                documentPath);
            return false;
        }

        auto* document = _context->LoadDocument(documentPath);
        if (!document) {
            logger::error(
                "DragonBoardVR: failed to load RmlUi panel '{}' from '{}'.",
                panelId,
                documentPath);
            return false;
        }
        document->AddEventListener("change", _eventListener.get());
        document->Hide();
        _registeredPanels.emplace(handle, RegisteredPanel{
            handle, std::move(panelId), std::move(documentPath), document });
        RegisterDocumentInteractives(document);
        logger::info(
            "DragonBoardVR: registered external RmlUi panel {} ('{}').",
            handle,
            _registeredPanels.at(handle).id);
        return true;
    }

    bool DragonBoardRmlUi::UnregisterPanel(std::uint32_t handle)
    {
        const auto it = _registeredPanels.find(handle);
        if (it == _registeredPanels.end()) return false;
        auto* document = it->second.document;
        if (_activeDocument == document) {
            _activeDocument = nullptr;
        }
        std::erase_if(_interactiveBindings, [document](const InteractiveBinding& binding) {
            return binding.document == document;
        });
        if (document) document->Close();
        _registeredPanels.erase(it);
        return true;
    }

    bool DragonBoardRmlUi::IsPanelReady(std::uint32_t handle) const
    {
        const auto* panel = FindPanel(handle);
        return _renderer && _renderer->IsReady() && _context && panel && panel->document;
    }

    bool DragonBoardRmlUi::ShowPanel(std::uint32_t handle)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document) return false;
        if (_activeDocument == panel->document) return true;
        HideAllDocuments();
        panel->document->Show();
        _activeDocument = panel->document;
        return true;
    }

    bool DragonBoardRmlUi::SetElementText(
        std::uint32_t handle, const char* elementId, const char* text)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document || !elementId || !*elementId) return false;
        auto* element = panel->document->GetElementById(elementId);
        if (!element) return false;
        element->SetInnerRML(EscapeRml(text ? text : ""));
        return true;
    }

    bool DragonBoardRmlUi::SetElementAttribute(
        std::uint32_t handle,
        const char* elementId,
        const char* name,
        const char* value)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document || !elementId || !*elementId || !name || !*name) {
            return false;
        }
        auto* element = panel->document->GetElementById(elementId);
        if (!element) return false;
        if (value) element->SetAttribute(name, value);
        else element->RemoveAttribute(name);
        return true;
    }

    bool DragonBoardRmlUi::SetElementClass(
        std::uint32_t handle,
        const char* elementId,
        const char* className,
        bool enabled)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document || !elementId || !*elementId ||
            !className || !*className) {
            return false;
        }
        auto* element = panel->document->GetElementById(elementId);
        if (!element) return false;
        element->SetClass(className, enabled);
        return true;
    }

    std::optional<DragonBoardRmlUi::PanelEvent> DragonBoardRmlUi::ConsumePanelEvent()
    {
        if (_panelEvents.empty()) return std::nullopt;
        auto event = std::move(_panelEvents.front());
        _panelEvents.pop_front();
        return event;
    }

    void DragonBoardRmlUi::ProcessInput(
        bool pointerOnPanel,
        float pointerU,
        float pointerV,
        bool triggerDown,
        bool gripDown,
        float stickX,
        float stickY,
        int width,
        int height)
    {
        if (!IsReady()) return;

        _currentTriggerDown = triggerDown;
        _currentGripDown = gripDown;

        const int pointerX = std::clamp(
            static_cast<int>(std::lround(std::clamp(pointerU, 0.0f, 1.0f) * width)), 0, width - 1);
        const int pointerY = std::clamp(
            static_cast<int>(std::lround(std::clamp(pointerV, 0.0f, 1.0f) * height)), 0, height - 1);

        // RmlUi's mouse interaction can move a scroll container while the
        // primary button is held (notably through internal draggable controls).
        // In VR the primary button is the trigger, whose contract is click/drag
        // only. Capture the scroll offsets before submitting the press and keep
        // them fixed until the matching release has completed.
        if (triggerDown && !_previousTriggerDown) {
            BeginTriggerScrollLock();
        }

        int submittedPointerX = pointerX;
        int submittedPointerY = pointerY;
        if (_triggerCaptureMode == TriggerCaptureMode::kButton) {
            submittedPointerX = _triggerCaptureX;
            submittedPointerY = _triggerCaptureY;
        } else if (_triggerCaptureMode == TriggerCaptureMode::kSlider) {
            submittedPointerY = _triggerCaptureY;
        }

        if (pointerOnPanel) {
            _context->ProcessMouseMove(submittedPointerX, submittedPointerY, 0);
            RestoreTriggerScrollLock();
            auto* hovered = _context->GetHoverElement();
            while (hovered && hovered != _activeDocument &&
                   hovered->GetTagName() != "button" &&
                   hovered->GetTagName() != "input" &&
                   !IsItemEditCard(hovered)) {
                hovered = hovered->GetParentNode();
            }
            if (hovered && hovered != _activeDocument && !hovered->GetId().empty()) {
                HandleHover(hovered->GetId().c_str());
            } else {
                _hoveredElementId.clear();
            }
            UpdateCursor(true, pointerX, pointerY);
            _pointerWasOnPanel = true;
        } else if (_pointerWasOnPanel) {
            _context->ProcessMouseLeave();
            UpdateCursor(false, 0, 0);
            _pointerWasOnPanel = false;
            _hoveredElementId.clear();
        }

        // Keep the global DragonBoard grip mapping intact. Smooth the scroll
        // locally while grip is held, then discard all pending motion on
        // release so the next trigger press can never inherit inertia.
        const bool scrollArmed = gripDown && !triggerDown;
        if (scrollArmed && pointerOnPanel) {
            if (!_gripScrollActive) {
                _gripScrollActive = true;
                _gripScrollPointerY = pointerY;
                _gripPointerScrollAccumulator = 0.0f;
                auto* hovered = _context->GetHoverElement();
                _gripScrollTarget = hovered ? hovered->GetClosestScrollableContainer() : nullptr;
                if (!_gripScrollTarget && _activeDocument) {
                    _gripScrollTarget = _activeDocument->GetElementById("page-scroll");
                }
                _gripScrollTargetTop = _gripScrollTarget ?
                    _gripScrollTarget->GetScrollTop() : 0.0f;
            } else {
                const float pointerDelta = static_cast<float>(pointerY - _gripScrollPointerY);
                _gripScrollPointerY = pointerY;
                constexpr float pointerFilter = 0.32f;
                _gripPointerScrollAccumulator +=
                    (pointerDelta - _gripPointerScrollAccumulator) * pointerFilter;
                constexpr float pointerSensitivity = 0.78f;
                _gripScrollTargetTop -= _gripPointerScrollAccumulator * pointerSensitivity;
            }

            if (_gripScrollTarget) {
                constexpr float stickDeadzone = 0.15f;
                constexpr float stickPixelsPerFrame = 9.0f;
                if (std::abs(stickY) > stickDeadzone) {
                    _gripScrollTargetTop += stickY * stickPixelsPerFrame;
                }
                const float maximum = std::max(
                    0.0f,
                    _gripScrollTarget->GetScrollHeight() - _gripScrollTarget->GetClientHeight());
                _gripScrollTargetTop = std::clamp(_gripScrollTargetTop, 0.0f, maximum);

                const float current = _gripScrollTarget->GetScrollTop();
                const float difference = _gripScrollTargetTop - current;
                if (std::abs(difference) >= 0.5f) {
                    constexpr float easing = 0.24f;
                    float step = difference * easing;
                    if (std::abs(step) < 1.0f) step = std::copysign(1.0f, step);
                    _gripScrollTarget->SetScrollTop(current + step);
                }
            }
        } else {
            _gripScrollActive = false;
            _gripScrollTarget = nullptr;
            _gripScrollTargetTop = 0.0f;
            _gripPointerScrollAccumulator = 0.0f;
        }

        if (triggerDown != _previousTriggerDown) {
            logger::info(
                "DragonBoardVR: RmlUi trigger {} (pointerOnPanel={}, x={}, y={}).",
                triggerDown ? "down" : "up",
                pointerOnPanel,
                pointerX,
                pointerY);
            if (!triggerDown || pointerOnPanel) {
                if (triggerDown) {
                    _triggerCapturedSliderId.clear();
                    _triggerCapturedActionId.clear();
                    _triggerCaptureProgrammatic = false;
                    auto* captureElement = _context->GetHoverElement();
                    while (captureElement && captureElement != _activeDocument &&
                           captureElement->GetTagName() != "button" &&
                           captureElement->GetTagName() != "input" &&
                           !IsItemEditCard(captureElement)) {
                        captureElement = captureElement->GetParentNode();
                    }
                    if (captureElement && captureElement->GetTagName() == "input") {
                        const auto inputType = captureElement->GetAttribute<Rml::String>(
                            "type", "text");
                        _triggerCaptureMode = inputType == "range" ?
                            TriggerCaptureMode::kSlider : TriggerCaptureMode::kButton;
                    } else if (captureElement && captureElement != _activeDocument) {
                        _triggerCaptureMode = TriggerCaptureMode::kButton;
                    } else {
                        _triggerCaptureMode = TriggerCaptureMode::kNone;
                    }
                    if (_triggerCaptureMode == TriggerCaptureMode::kNone) {
                        TriggerCaptureMode fallbackMode = TriggerCaptureMode::kNone;
                        if (auto* interactive = FindInteractiveAtPoint(
                                pointerX, pointerY, fallbackMode)) {
                            _triggerCaptureMode = fallbackMode;
                            if (fallbackMode == TriggerCaptureMode::kSlider) {
                                _triggerCapturedSliderId = interactive->GetId();
                            } else if (fallbackMode == TriggerCaptureMode::kButton) {
                                _triggerCapturedActionId = interactive->GetId();
                                _triggerCaptureProgrammatic = true;
                            }
                        }
                    } else if (_triggerCaptureMode == TriggerCaptureMode::kSlider) {
                        _triggerCapturedSliderId = captureElement->GetId();
                    }
                    _triggerCaptureX = pointerX;
                    _triggerCaptureY = pointerY;
                    const char* captureName = _triggerCaptureMode == TriggerCaptureMode::kSlider ?
                        "slider" : (_triggerCaptureMode == TriggerCaptureMode::kButton ? "button" : "none");
                    logger::info("DragonBoardVR: RmlUi trigger captured {} target.", captureName);
                    if (_triggerCaptureMode != TriggerCaptureMode::kSlider &&
                        !_triggerCaptureProgrammatic) {
                        _context->ProcessMouseButtonDown(0, 0);
                    }
                } else {
                    if (_triggerCaptureMode == TriggerCaptureMode::kSlider) {
                        UpdateCapturedSlider(pointerX);
                    } else if (_triggerCaptureProgrammatic && !_triggerCapturedActionId.empty()) {
                        HandleClick(_triggerCapturedActionId.c_str());
                    } else {
                        _context->ProcessMouseButtonUp(0, 0);
                    }
                    _triggerScrollReleasePending = true;
                    _triggerCaptureMode = TriggerCaptureMode::kNone;
                    _triggerCapturedSliderId.clear();
                    _triggerCapturedActionId.clear();
                    _triggerCaptureProgrammatic = false;
                }
            }
            _previousTriggerDown = triggerDown;
        }

        if (triggerDown && _triggerCaptureMode == TriggerCaptureMode::kSlider) {
            UpdateCapturedSlider(pointerX);
        }

        (void)stickX;
    }

    Rml::Element* DragonBoardRmlUi::FindInteractiveAtPoint(
        int x, int y, TriggerCaptureMode& mode) const
    {
        mode = TriggerCaptureMode::kNone;
        if (!_activeDocument) return nullptr;
        const Rml::Vector2f point(static_cast<float>(x), static_cast<float>(y));
        Rml::Element* best = nullptr;
        float bestArea = std::numeric_limits<float>::max();
        for (const auto& binding : _interactiveBindings) {
            if (binding.document != _activeDocument) continue;
            auto* element = binding.document->GetElementById(binding.id);
            if (!element || !element->IsVisible(true) ||
                !element->IsPointWithinElement(point)) {
                continue;
            }
            const float area = element->GetOffsetWidth() * element->GetOffsetHeight();
            if (area <= 0.0f || area >= bestArea) continue;
            best = element;
            bestArea = area;
            mode = binding.mode;
        }
        return best;
    }

    void DragonBoardRmlUi::RegisterInteractive(
        Rml::ElementDocument* document, const char* id, TriggerCaptureMode mode)
    {
        if (!document || !id || !*id) return;
        const auto existing = std::find_if(
            _interactiveBindings.begin(), _interactiveBindings.end(),
            [document, id](const InteractiveBinding& binding) {
                return binding.document == document && binding.id == id;
            });
        if (existing != _interactiveBindings.end()) {
            existing->mode = mode;
            return;
        }
        _interactiveBindings.push_back({ document, id, mode });
    }

    void DragonBoardRmlUi::RegisterDocumentInteractives(Rml::ElementDocument* document)
    {
        if (!document) return;

        Rml::ElementList buttons;
        document->GetElementsByTagName(buttons, "button");
        for (auto* element : buttons) {
            if (element && !element->GetId().empty()) {
                BindClick(document, element->GetId().c_str());
            }
        }

        Rml::ElementList inputs;
        document->GetElementsByTagName(inputs, "input");
        for (auto* element : inputs) {
            if (!element || element->GetId().empty()) continue;
            const auto type = element->GetAttribute<Rml::String>("type", "text");
            if (type == "range") BindSlider(document, element->GetId().c_str());
            else BindClick(document, element->GetId().c_str());
        }
    }

    DragonBoardRmlUi::RegisteredPanel* DragonBoardRmlUi::FindPanel(std::uint32_t handle)
    {
        const auto it = _registeredPanels.find(handle);
        return it != _registeredPanels.end() ? &it->second : nullptr;
    }

    const DragonBoardRmlUi::RegisteredPanel* DragonBoardRmlUi::FindPanel(
        std::uint32_t handle) const
    {
        const auto it = _registeredPanels.find(handle);
        return it != _registeredPanels.end() ? &it->second : nullptr;
    }

    std::uint32_t DragonBoardRmlUi::FindPanelHandle(Rml::ElementDocument* document) const
    {
        for (const auto& [handle, panel] : _registeredPanels) {
            if (panel.document == document) return handle;
        }
        return 0;
    }

    void DragonBoardRmlUi::HideAllDocuments()
    {
        if (_settingsDocument) _settingsDocument->Hide();
        if (_developerDocument) _developerDocument->Hide();
        if (_itemEditDocument) _itemEditDocument->Hide();
        for (auto& [handle, panel] : _registeredPanels) {
            (void)handle;
            if (panel.document) panel.document->Hide();
        }
    }

    void DragonBoardRmlUi::UpdateCapturedSlider(int pointerX)
    {
        if (!_activeDocument || _triggerCapturedSliderId.empty()) return;
        auto* element = _activeDocument->GetElementById(_triggerCapturedSliderId);
        if (!element) return;

        const float width = element->GetClientWidth();
        if (width <= 0.0f) return;
        const float minimum = std::stof(element->GetAttribute<Rml::String>("min", "0"));
        const float maximum = std::stof(element->GetAttribute<Rml::String>("max", "1"));
        const float step = std::stof(element->GetAttribute<Rml::String>("step", "0"));
        const float fraction = std::clamp(
            (static_cast<float>(pointerX) - element->GetAbsoluteLeft()) / width,
            0.0f,
            1.0f);
        float value = minimum + fraction * (maximum - minimum);
        if (step > 0.0f) value = minimum + std::round((value - minimum) / step) * step;
        value = std::clamp(value, minimum, maximum);

        const float previous = std::stof(element->GetAttribute<Rml::String>("value", "0"));
        if (std::abs(previous - value) < 0.0001f) return;
        _synchronizingSliderValues = true;
        element->SetAttribute("value", Rml::CreateString("%.6f", value));
        _synchronizingSliderValues = false;
        HandleSliderChange(_triggerCapturedSliderId.c_str(), value);
    }

    void DragonBoardRmlUi::BeginTriggerScrollLock()
    {
        _triggerScrollLockDocument = _activeDocument;
        auto* page = _activeDocument ? _activeDocument->GetElementById("page-scroll") : nullptr;
        auto* nested = _activeDocument ? _activeDocument->GetElementById("dev-command-list") : nullptr;
        _triggerScrollLockPageTop = page ? page->GetScrollTop() : 0.0f;
        _triggerScrollLockNestedTop = nested ? nested->GetScrollTop() : 0.0f;
        _triggerScrollLockActive = true;
        _triggerScrollReleasePending = false;
        _triggerScrollSuppressionLogged = false;
    }

    void DragonBoardRmlUi::RestoreTriggerScrollLock()
    {
        if (!_triggerScrollLockActive) return;
        if (!_activeDocument || _activeDocument != _triggerScrollLockDocument) {
            _triggerScrollLockDocument = nullptr;
            _triggerScrollLockActive = false;
            _triggerScrollReleasePending = false;
            return;
        }

        auto* page = _activeDocument->GetElementById("page-scroll");
        auto* nested = _activeDocument->GetElementById("dev-command-list");
        const bool pageMoved = page && page->GetScrollTop() != _triggerScrollLockPageTop;
        const bool nestedMoved = nested && nested->GetScrollTop() != _triggerScrollLockNestedTop;
        if (pageMoved) page->SetScrollTop(_triggerScrollLockPageTop);
        if (nestedMoved) nested->SetScrollTop(_triggerScrollLockNestedTop);

        if ((pageMoved || nestedMoved) && !_triggerScrollSuppressionLogged) {
            logger::info(
                "DragonBoardVR: suppressed RmlUi trigger drag scroll (page={}, nested={}).",
                pageMoved,
                nestedMoved);
            _triggerScrollSuppressionLogged = true;
        }

        if (_triggerScrollReleasePending) {
            _triggerScrollLockDocument = nullptr;
            _triggerScrollLockActive = false;
            _triggerScrollReleasePending = false;
        }
    }

    void DragonBoardRmlUi::TraceScrollState()
    {
        if (!_activeDocument) return;

        auto* page = _activeDocument->GetElementById("page-scroll");
        auto* nested = _activeDocument->GetElementById("dev-command-list");
        const float pageTop = page ? page->GetScrollTop() : 0.0f;
        const float nestedTop = nested ? nested->GetScrollTop() : 0.0f;

        if (_observedScrollDocument != _activeDocument) {
            _observedScrollDocument = _activeDocument;
            _observedPageScrollTop = pageTop;
            _observedNestedScrollTop = nestedTop;
            return;
        }

        if (pageTop != _observedPageScrollTop) {
            logger::info(
                "DragonBoardVR: RmlUi page scrollTop {:.1f} -> {:.1f} (trigger={}, grip={}).",
                _observedPageScrollTop,
                pageTop,
                _currentTriggerDown,
                _currentGripDown);
            _observedPageScrollTop = pageTop;
        }
        if (nestedTop != _observedNestedScrollTop) {
            logger::info(
                "DragonBoardVR: RmlUi nested scrollTop {:.1f} -> {:.1f} (trigger={}, grip={}).",
                _observedNestedScrollTop,
                nestedTop,
                _currentTriggerDown,
                _currentGripDown);
            _observedNestedScrollTop = nestedTop;
        }
    }

    bool DragonBoardRmlUi::Render(ID3D11RenderTargetView* renderTarget, int width, int height)
    {
        if (!IsReady()) return false;
        const Rml::Vector2i dimensions(width, height);
        if (_context->GetDimensions() != dimensions) {
            _context->SetDimensions(dimensions);
        }
        if (!_context->Update()) return false;
        RestoreTriggerScrollLock();
        TraceScrollState();
        if (!_renderer->BeginFrame(renderTarget, width, height)) return false;
        try {
            const bool rendered = _context->Render();
            _renderer->EndFrame();
            return rendered;
        } catch (...) {
            _renderer->EndFrame();
            throw;
        }
    }

    bool DragonBoardRmlUi::ConsumeCloseRequested()
    {
        return std::exchange(_closeRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeSaveRequested()
    {
        return std::exchange(_saveRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeDeveloperPanelToggleRequested()
    {
        return std::exchange(_developerPanelToggleRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeEditModeToggleRequested()
    {
        return std::exchange(_editModeToggleRequested, false);
    }

    DragonBoardRmlUi::HapticCue DragonBoardRmlUi::ConsumeHapticCue()
    {
        return std::exchange(_pendingHapticCue, HapticCue::kNone);
    }

    std::optional<DragonBoardRmlUi::SliderChange> DragonBoardRmlUi::ConsumeSliderChange()
    {
        auto result = std::move(_sliderChange);
        _sliderChange.reset();
        return result;
    }

    std::optional<std::size_t> DragonBoardRmlUi::ConsumeDeveloperCommandRequested()
    {
        auto result = _developerCommandRequested;
        _developerCommandRequested.reset();
        return result;
    }

    DragonBoardRmlUi::ItemEditAction DragonBoardRmlUi::ConsumeItemEditAction()
    {
        return std::exchange(_itemEditAction, ItemEditAction::kNone);
    }

    void DragonBoardRmlUi::SetSliderValue(const char* id, float value)
    {
        if (!id) return;
        const std::string_view sliderId(id);
        auto* document = FindPanelHandle(_activeDocument) != 0 ? _activeDocument :
            (sliderId.starts_with("edit") ? _itemEditDocument : _settingsDocument);
        if (!document) return;
        auto* element = document->GetElementById(id);
        if (!element) return;

        _synchronizingSliderValues = true;
        element->SetAttribute("value", Rml::CreateString("%.6f", value));
        _synchronizingSliderValues = false;
        UpdateSliderValueLabel(id, value);
    }

    void DragonBoardRmlUi::SetItemEditInfo(const ItemEditInfo& info)
    {
        if (!_itemEditDocument) return;
        const auto setText = [this](const char* id, std::string value) {
            if (auto* element = _itemEditDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };
        setText("edit-item-name", info.itemName.empty() ? "Unnamed item" : info.itemName);
        setText("edit-category", info.category.empty() ? "Unknown" : info.category);
        setText("edit-form-id", Rml::CreateString("%08X", info.formID));
        setText("edit-model-path", info.modelPath.empty() ? "No model" : info.modelPath);
        SetSliderValue("editPosX", info.posX);
        SetSliderValue("editPosY", info.posY);
        SetSliderValue("editPosZ", info.posZ);
        SetSliderValue("editRotX", info.rotX);
        SetSliderValue("editRotY", info.rotY);
        SetSliderValue("editRotZ", info.rotZ);
        SetSliderValue("editScale", info.scale);

        if (auto* magic = _itemEditDocument->GetElementById("edit-magic-pin-actions")) {
            magic->SetProperty("display", info.magicItem ? "block" : "none");
        }
        if (auto* dashboard = _itemEditDocument->GetElementById("edit-pin-dashboard")) {
            dashboard->SetClass("disabled", info.boardPinnedToWorld);
        }
        setText("edit-pin-dashboard-state",
            info.boardPinnedToWorld ? "Unavailable while the board is world pinned" : "Attach this item to the dashboard");
        if (auto* world = _itemEditDocument->GetElementById("edit-pin-world")) {
            world->SetClass("disabled", !info.canPinToWorld);
        }
        setText("edit-pin-world-state",
            info.canPinToWorld ? "Preserve the current world pose" : "Requires a valid 3D preview pose");
        setText("edit-label-state", info.labelHidden ? "Hidden" : "Visible");
        if (auto* label = _itemEditDocument->GetElementById("edit-toggle-label")) {
            label->SetClass("enabled", !info.labelHidden);
        }
    }

    void DragonBoardRmlUi::SetDeveloperButtonEnabled(bool enabled)
    {
        if (!_settingsDocument) return;
        if (auto* toggle = _settingsDocument->GetElementById("toggle-dev-panel")) {
            toggle->SetClass("enabled", enabled);
        }
        if (auto* state = _settingsDocument->GetElementById("toggle-dev-state")) {
            state->SetInnerRML(enabled ? "Enabled" : "Disabled");
        }
    }

    void DragonBoardRmlUi::SetEditModeEnabled(bool enabled)
    {
        if (!_settingsDocument) return;
        if (auto* toggle = _settingsDocument->GetElementById("toggle-edit-mode")) {
            toggle->SetClass("enabled", enabled);
        }
        if (auto* state = _settingsDocument->GetElementById("toggle-edit-state")) {
            state->SetInnerRML(enabled ? "Enabled" : "Disabled");
        }
    }

    void DragonBoardRmlUi::SetDeveloperCommands(std::vector<DeveloperCommand> commands)
    {
        if (!_developerDocument) return;
        _developerCommands = std::move(commands);
        _selectedDeveloperCommand = 0;

        auto* list = _developerDocument->GetElementById("dev-command-list");
        if (!list) return;
        std::string markup;
        for (std::size_t index = 0; index < _developerCommands.size(); ++index) {
            markup += "<button id=\"dev-command-" + std::to_string(index) +
                "\" class=\"command-item\">" + EscapeRml(_developerCommands[index].label) + "</button><br />";
        }
        if (markup.empty()) markup = "<div class=\"empty-state\">No commands configured.</div>";
        list->SetInnerRML(markup);
        for (std::size_t index = 0; index < _developerCommands.size(); ++index) {
            const std::string id = "dev-command-" + std::to_string(index);
            BindClick(_developerDocument, id.c_str());
        }
        SelectDeveloperCommand(0);
    }

    void DragonBoardRmlUi::SetDeveloperInfo(const DeveloperInfo& info)
    {
        if (!_developerDocument) return;
        const auto setText = [this](const char* id, std::string value) {
            if (auto* element = _developerDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };

        setText("dev-fps", Rml::CreateString("%.1f", info.fps));
        setText("dev-frame-time", Rml::CreateString("%.2f ms", info.frameTimeMs));
        setText("dev-draw-calls", std::to_string(info.panelDrawCalls));
        setText("dev-texture-size", "1920 x 1080");
        setText("dev-version", info.pluginVersion);
        setText("dev-feature-level", Rml::CreateString("0x%X", info.d3dFeatureLevel));
        setText("dev-player-position", Rml::CreateString(
            "X %.1f   Y %.1f   Z %.1f", info.playerX, info.playerY, info.playerZ));
        setText("dev-cell", info.cellName.empty() ? "<none>" : info.cellName);
        setText("dev-cell-form", Rml::CreateString("%08X", info.cellFormId));
        setText("dev-worldspace", info.worldspaceName.empty() ? "<interior or none>" : info.worldspaceName);
        setText("dev-worldspace-form", info.worldspaceFormId == 0 ? "--------" :
            Rml::CreateString("%08X", info.worldspaceFormId));
    }

    int DragonBoardRmlUi::GetLastDrawCallCount() const
    {
        return _renderer ? _renderer->GetDrawCallCount() : 0;
    }

    void DragonBoardRmlUi::BindClick(Rml::ElementDocument* document, const char* id)
    {
        if (!document) return;
        if (auto* element = document->GetElementById(id)) {
            // Bind the actionable box itself. ProcessEvent still resolves a
            // nested text target back to its owning button or card.
            element->AddEventListener("click", _eventListener.get());
            RegisterInteractive(document, id, TriggerCaptureMode::kButton);
        } else {
            logger::warn("DragonBoardVR: RmlUi element '{}' is missing.", id);
        }
    }

    void DragonBoardRmlUi::BindSlider(Rml::ElementDocument* document, const char* id)
    {
        if (document && document->GetElementById(id)) {
            RegisterInteractive(document, id, TriggerCaptureMode::kSlider);
        } else {
            logger::warn("DragonBoardVR: RmlUi slider '{}' is missing.", id);
        }
    }

    void DragonBoardRmlUi::HandleClick(const char* id)
    {
        if (!id) return;
        RequestHaptic(ResolveClickHaptic(id));
        logger::info("DragonBoardVR: RmlUi click on '{}'.", id);
        if (const auto panel = FindPanelHandle(_activeDocument); panel != 0) {
            std::string value;
            if (const auto* element = _activeDocument->GetElementById(id)) {
                value = element->GetAttribute<Rml::String>("value", "");
                if (element->GetAttribute<Rml::String>(
                        "data-dragonboard-action", "") == "close") {
                    _closeRequested = true;
                }
            }
            _panelEvents.push_back(PanelEvent{
                panel, PanelEventType::kClick, id, std::move(value), 0.0f });
            return;
        }
        const std::string_view value(id);
        if (value == "close" || value == "dev-close") {
            _closeRequested = true;
        } else if (value == "edit-close" || value == "edit-back") {
            _itemEditAction = ItemEditAction::kBack;
        } else if (value == "edit-apply-item") {
            _itemEditAction = ItemEditAction::kApplyItem;
        } else if (value == "edit-apply-category") {
            _itemEditAction = ItemEditAction::kApplyCategory;
        } else if (value == "edit-reset") {
            _itemEditAction = ItemEditAction::kReset;
        } else if (value == "edit-pin-dashboard") {
            _itemEditAction = ItemEditAction::kPinDashboard;
        } else if (value == "edit-pin-left") {
            _itemEditAction = ItemEditAction::kPinLeftHand;
        } else if (value == "edit-pin-world") {
            _itemEditAction = ItemEditAction::kPinWorld;
        } else if (value == "edit-toggle-label") {
            _itemEditAction = ItemEditAction::kToggleLabel;
        } else if (value == "save") {
            _saveRequested = true;
        } else if (value == "toggle-edit-mode") {
            _editModeToggleRequested = true;
        } else if (value == "toggle-dev-panel") {
            _developerPanelToggleRequested = true;
        } else if (value.starts_with("tab-")) {
            SelectSettingsPage(id + 4);
        } else if (value.starts_with("dev-tab-")) {
            SelectDeveloperPage(id + 8);
        } else if (value.starts_with("edit-tab-")) {
            SelectItemEditPage(id + 9);
        } else if (value.starts_with("dev-command-")) {
            try {
                SelectDeveloperCommand(static_cast<std::size_t>(std::stoul(std::string(value.substr(12)))));
            } catch (...) {
                logger::warn("DragonBoardVR: invalid RmlUi developer command id '{}'.", id);
            }
        } else if (value == "dev-execute" && _selectedDeveloperCommand < _developerCommands.size()) {
            _developerCommandRequested = _selectedDeveloperCommand;
        }
    }

    void DragonBoardRmlUi::HandleHover(const char* id)
    {
        if (!id || !*id || _hoveredElementId == id) return;
        _hoveredElementId = id;
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastHoverHaptic < std::chrono::milliseconds(45)) return;
        _lastHoverHaptic = now;
        RequestHaptic(HapticCue::kHover);
    }

    void DragonBoardRmlUi::HandleSliderChange(const char* id, float value)
    {
        if (_synchronizingSliderValues || !id || !*id) return;
        logger::info("DragonBoardVR: RmlUi slider '{}' changed to {:.3f}.", id, value);
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastSliderHaptic >= std::chrono::milliseconds(55)) {
            _lastSliderHaptic = now;
            RequestHaptic(HapticCue::kSliderTick);
        }
        UpdateSliderValueLabel(id, value);
        if (const auto panel = FindPanelHandle(_activeDocument); panel != 0) {
            _panelEvents.push_back(PanelEvent{
                panel,
                PanelEventType::kChange,
                id,
                Rml::CreateString("%.6f", value),
                value });
            return;
        }
        _sliderChange = SliderChange{ id, value };
    }

    void DragonBoardRmlUi::RequestHaptic(HapticCue cue)
    {
        if (static_cast<std::uint8_t>(cue) >
            static_cast<std::uint8_t>(_pendingHapticCue)) {
            _pendingHapticCue = cue;
        }
    }

    DragonBoardRmlUi::HapticCue DragonBoardRmlUi::ResolveClickHaptic(const char* id) const
    {
        if (!id || !_activeDocument) return HapticCue::kPress;
        if (const auto* element = _activeDocument->GetElementById(id)) {
            const auto type = element->GetAttribute<Rml::String>("data-haptic", "");
            if (type == "none") return HapticCue::kNone;
            if (type == "light") return HapticCue::kHover;
            if (type == "slider") return HapticCue::kSliderTick;
            if (type == "strong") return HapticCue::kStrong;
            if (type == "error") return HapticCue::kError;
        }
        return HapticCue::kPress;
    }

    void DragonBoardRmlUi::UpdateSliderValueLabel(const char* id, float value)
    {
        if (!id) return;
        const std::string_view sliderId(id);
        auto* document = sliderId.starts_with("edit") ? _itemEditDocument : _settingsDocument;
        if (!document) return;
        const std::string valueId = std::string("value-") + id;
        auto* label = document->GetElementById(valueId);
        if (!label) return;

        const bool degrees = sliderId.starts_with("menuRot") || sliderId.starts_with("editRot");
        label->SetInnerRML(Rml::CreateString(degrees ? "%.1f deg" : "%.2f", value));
    }

    void DragonBoardRmlUi::SelectSettingsPage(const char* selectedPage)
    {
        if (!_settingsDocument || !selectedPage) return;
        for (const auto* page : kPages) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("tab-") + page;
            const std::string pageId = std::string("page-") + page;
            if (auto* tab = _settingsDocument->GetElementById(tabId)) tab->SetClass("active", active);
            if (auto* content = _settingsDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
    }

    void DragonBoardRmlUi::SelectDeveloperPage(const char* selectedPage)
    {
        if (!_developerDocument || !selectedPage) return;
        for (const auto* page : kDeveloperPages) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("dev-tab-") + page;
            const std::string pageId = std::string("dev-page-") + page;
            if (auto* tab = _developerDocument->GetElementById(tabId)) tab->SetClass("active", active);
            if (auto* content = _developerDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
    }

    void DragonBoardRmlUi::SelectItemEditPage(const char* selectedPage)
    {
        if (!_itemEditDocument || !selectedPage) return;
        for (const auto* page : kItemEditPages) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("edit-tab-") + page;
            const std::string pageId = std::string("edit-page-") + page;
            if (auto* tab = _itemEditDocument->GetElementById(tabId)) tab->SetClass("active", active);
            if (auto* content = _itemEditDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
    }

    void DragonBoardRmlUi::SelectDeveloperCommand(std::size_t index)
    {
        if (!_developerDocument || _developerCommands.empty()) {
            _selectedDeveloperCommand = 0;
            UpdateDeveloperCommandDetails();
            return;
        }
        _selectedDeveloperCommand = std::min(index, _developerCommands.size() - 1);
        for (std::size_t commandIndex = 0; commandIndex < _developerCommands.size(); ++commandIndex) {
            const std::string id = "dev-command-" + std::to_string(commandIndex);
            if (auto* element = _developerDocument->GetElementById(id)) {
                element->SetClass("active", commandIndex == _selectedDeveloperCommand);
            }
        }
        UpdateDeveloperCommandDetails();
    }

    void DragonBoardRmlUi::UpdateDeveloperCommandDetails()
    {
        if (!_developerDocument) return;
        const auto setText = [this](const char* id, std::string value) {
            if (auto* element = _developerDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };
        const bool valid = _selectedDeveloperCommand < _developerCommands.size();
        if (!valid) {
            setText("dev-command-title", "No command selected");
            setText("dev-command-description", "Add commands to DragonBoardVR_DevCommands.ini.");
            setText("dev-command-input", "");
            if (auto* warning = _developerDocument->GetElementById("dev-command-warning")) {
                warning->SetProperty("display", "none");
            }
            if (auto* execute = _developerDocument->GetElementById("dev-execute")) {
                execute->SetAttribute("disabled", "");
                execute->SetAttribute("data-haptic", "error");
            }
            return;
        }

        const auto& command = _developerCommands[_selectedDeveloperCommand];
        setText("dev-command-title", command.label);
        setText("dev-command-description", command.description);
        setText("dev-command-input", command.command);
        if (auto* warning = _developerDocument->GetElementById("dev-command-warning")) {
            warning->SetProperty("display", command.dangerous ? "block" : "none");
        }
        if (auto* execute = _developerDocument->GetElementById("dev-execute")) {
            execute->RemoveAttribute("disabled");
            execute->SetAttribute("data-haptic", command.dangerous ? "strong" : "normal");
        }
    }

    void DragonBoardRmlUi::UpdateCursor(bool visible, int x, int y)
    {
        auto* cursor = _activeDocument ? _activeDocument->GetElementById("vr-cursor") : nullptr;
        if (!cursor) return;
        cursor->SetProperty("display", visible ? "block" : "none");
        if (visible) {
            cursor->SetProperty("left", std::to_string(x) + "px");
            cursor->SetProperty("top", std::to_string(y) + "px");
        }
    }
}
