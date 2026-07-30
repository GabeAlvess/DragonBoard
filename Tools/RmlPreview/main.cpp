#include "ui/rml/DragonBoardRmlRenderer.h"
#include "ui/rml/LocalizationManager.h"
#include "ui/rml/RmlPerformanceMetrics.h"
#include "ui/rml/RmlVirtualList.h"
#include "RmlSourceEditor.h"
#include "RmlVisualInspector.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Debugger.h>

#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using dragonboard::ui::rml::DragonBoardRmlRenderer;
    using dragonboard::ui::rml::LocalizationManager;
    using dragonboard::ui::rml::RmlPerformanceMetrics;
    using dragonboard::ui::rml::RmlVirtualList;
    using dragonboard::tools::RmlSourceEditor;
    using dragonboard::tools::RmlVisualInspector;

    constexpr int kCanvasWidth = 1920;
    constexpr int kCanvasHeight = 1080;
    // Keep the client surface identical to the RmlUi canvas. A smaller DXGI
    // window clips the 1920x1080 back buffer instead of scaling it, hiding the
    // right and bottom edges from visual-review screenshots.
    constexpr int kWindowWidth = kCanvasWidth;
    constexpr int kWindowHeight = kCanvasHeight;
    constexpr const char* kContextName = "dragonboard_rml_preview";
    constexpr UINT kCommandOpen = 1001;
    constexpr UINT kCommandReload = 1002;
    constexpr UINT kCommandExit = 1003;
    constexpr UINT kCommandInspector = 1004;
    constexpr UINT kCommandEditor = 1005;
    constexpr UINT kCommandDocumentBase = 2000;
    constexpr std::array<std::size_t, 3> kSyntheticDatasetSizes{ 25, 250, 1000 };

    constexpr std::array<const char*, 5> kSettingsPages{
        "general", "position", "visuals", "items", "labels"
    };
    constexpr std::array<const char*, 2> kDeveloperPages{
        "commands", "info"
    };
    constexpr std::array<const char*, 2> kJournalPages{
        "quests", "stats"
    };
    constexpr std::array<const char*, 4> kItemEditPages{
        "position", "rotation", "scale", "pin"
    };
    constexpr std::array<const char*, 23> kSliders{
        "menuScale", "buttonSpacingX", "buttonSpacingY",
        "menuOffsetX", "menuOffsetY", "menuOffsetZ",
        "menuRotX", "menuRotY", "menuRotZ",
        "buttonMeshScale", "itemMeshScale", "containerGridOffsetZ", "reticleScale",
        "itemWeaponScale", "itemArmorScale", "itemPotionScale", "itemFoodScale", "itemMiscScale",
        "labelScale", "labelSpacing", "labelXOffset", "labelYOffset", "labelZOffset"
    };

    struct MockCommand
    {
        const char* label;
        const char* command;
        const char* description;
        bool dangerous;
    };

    constexpr std::array<MockCommand, 8> kMockCommands{
        MockCommand{ "TGM", "tgm", "Toggle god mode for the player.", false },
        MockCommand{ "Immortal", "tim", "Toggle immortal mode without making the player invulnerable.", false },
        MockCommand{ "All map", "tmm 1", "Reveal all map markers.", true },
        MockCommand{ "Kill", "kill", "Kill the actor currently targeted by the console.", true },
        MockCommand{ "Gold", "player.additem f 1000", "Add 1000 gold to the player inventory.", false },
        MockCommand{ "Toggle Menu", "tm", "Toggle Skyrim user-interface visibility.", false },
        MockCommand{ "Unlock", "unlock", "Unlock the object currently targeted by the console.", true },
        MockCommand{ "RaceMenu", "showracemenu", "Open character creation and race selection.", true }
    };

    class PreviewSystemInterface final : public Rml::SystemInterface
    {
    public:
        double GetElapsedTime() override
        {
            return std::chrono::duration<double>(
                std::chrono::steady_clock::now() - _start).count();
        }

        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
        {
            const char* level = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? "error" :
                type == Rml::Log::LT_WARNING ? "warn" : "info";
            std::cerr << "[RmlUi " << level << "] " << message << '\n';
            return true;
        }

    private:
        std::chrono::steady_clock::time_point _start = std::chrono::steady_clock::now();
    };

    class PreviewApp;
    PreviewApp* g_app = nullptr;

    class PreviewEventListener final : public Rml::EventListener
    {
    public:
        explicit PreviewEventListener(PreviewApp& app) : _app(app) {}
        void ProcessEvent(Rml::Event& event) override;

    private:
        PreviewApp& _app;
    };

    enum class PreviewDocument
    {
        Settings,
        Developer,
        ItemEdit
    };

    class PreviewApp
    {
    public:
        bool Initialize(
            HINSTANCE instance,
            const std::optional<std::filesystem::path>& requestedDocument,
            std::string_view requestedLanguage)
        {
            _assetsDirectory = FindAssetsDirectory();
            if (!CreatePreviewWindow(instance) || !CreateD3D()) return false;
            if (!_renderer.Initialize(_device.Get(), _deviceContext.Get())) return false;

            Rml::SetSystemInterface(&_systemInterface);
            Rml::SetRenderInterface(&_renderer);
            if (!Rml::Initialise()) return false;
            _rmlInitialized = true;

            _context = Rml::CreateContext(
                kContextName, Rml::Vector2i(kCanvasWidth, kCanvasHeight), &_renderer);
            if (!_context || !LoadFont()) return false;
            _localization.Load(requestedLanguage);

            _listener = std::make_unique<PreviewEventListener>(*this);
            if (!_sourceEditor.Create(
                    instance,
                    _window,
                    [this](const std::filesystem::path&) { ReloadDocument(false); },
                    [this](const std::filesystem::path& path, std::string source) {
                        PreviewEditedSource(path, std::move(source));
                    },
                    [this]() {
                        if (!_inspector.Undo()) return false;
                        auto overridePath = _documentPath;
                        overridePath.replace_extension(".editor-overrides.rcss");
                        _sourceEditor.SetGeneratedContent(
                            overridePath, _inspector.SerializeOverrides(), "");
                        return true;
                    },
                    [this](std::string status) { SetStatus(std::move(status)); })) {
                std::cerr << "Source editor could not be initialized.\n";
                return false;
            }
            if (!_inspector.Create(instance, _window, [this](std::string status) {
                    SetStatus(std::move(status));
                })) {
                std::cerr << "Visual inspector could not be initialized.\n";
                return false;
            }
            if (!Rml::Debugger::Initialise(_context)) {
                std::cerr << "RmlUi debugger could not be initialized.\n";
            } else {
                _debuggerInitialized = true;
                Rml::Debugger::SetVisible(false);
            }

            std::filesystem::path initialDocument;
            if (requestedDocument && std::filesystem::is_regular_file(*requestedDocument)) {
                initialDocument = std::filesystem::absolute(*requestedDocument);
            } else if (!_assetsDirectory.empty()) {
                initialDocument = _assetsDirectory / "settings.rml";
            } else {
                initialDocument = SelectDocumentWithDialog();
            }
            if (initialDocument.empty() || !LoadDocument(initialDocument)) return false;
            _lastObservedWrite = LatestAssetWriteTime();
            SetStatus("Ready");
            return true;
        }

        int Run()
        {
            MSG message{};
            while (!_exitRequested) {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT) _exitRequested = true;
                    if (_sourceEditor.HandleShortcut(message)) continue;
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                if (_exitRequested) break;

                const auto frameStart = std::chrono::steady_clock::now();
                _inspector.Update();
                CheckHotReload();
                UpdateSyntheticVirtualRows(false);
                const auto updateStart = std::chrono::steady_clock::now();
                if (!_context->Update()) {
                    std::cerr << "RmlUi context update failed.\n";
                    break;
                }
                const auto updateEnd = std::chrono::steady_clock::now();
                const auto beginStart = updateEnd;
                if (!_renderer.BeginFrame(_renderTarget.Get(), kCanvasWidth, kCanvasHeight)) break;
                const auto beginEnd = std::chrono::steady_clock::now();
                const auto renderStart = beginEnd;
                const bool rendered = _context->Render();
                const auto renderEnd = std::chrono::steady_clock::now();
                const auto endStart = renderEnd;
                _renderer.EndFrame();
                const auto endEnd = std::chrono::steady_clock::now();
                if (!rendered) break;
                _swapChain->Present(1, 0);
                RecordPreviewPerformance(
                    frameStart,
                    updateStart,
                    updateEnd,
                    beginStart,
                    beginEnd,
                    renderStart,
                    renderEnd,
                    endStart,
                    endEnd);
            }
            return 0;
        }

        void Shutdown()
        {
            _sourceEditor.Destroy();
            _inspector.Destroy();
            if (_debuggerInitialized) {
                Rml::Debugger::Shutdown();
                _debuggerInitialized = false;
            }
            if (_document) {
                _document->Close();
                _document = nullptr;
            }
            _listener.reset();
            if (_context) {
                Rml::RemoveContext(kContextName);
                _context = nullptr;
            }
            if (_rmlInitialized) {
                Rml::Shutdown();
                _rmlInitialized = false;
            }
            _fontData.clear();
            _renderer.Shutdown();
            _renderTarget.Reset();
            _swapChain.Reset();
            _deviceContext.Reset();
            _device.Reset();
            if (_window) {
                DestroyWindow(_window);
                _window = nullptr;
            }
        }

        ~PreviewApp()
        {
            Shutdown();
        }

        LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            switch (message) {
            case WM_CLOSE:
                _exitRequested = true;
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case WM_DROPFILES: {
                const auto drop = reinterpret_cast<HDROP>(wParam);
                std::wstring path(32768, L'\0');
                const UINT length = DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size()));
                DragFinish(drop);
                path.resize(length);
                if (!path.empty()) LoadDocument(std::filesystem::path(path));
                return 0;
            }
            case WM_COMMAND: {
                const UINT command = LOWORD(wParam);
                if (command == kCommandOpen) {
                    const auto path = SelectDocumentWithDialog();
                    if (!path.empty()) LoadDocument(path);
                } else if (command == kCommandReload) {
                    ReloadDocument();
                } else if (command == kCommandExit) {
                    _exitRequested = true;
                } else if (command == kCommandInspector) {
                    _inspector.Toggle();
                } else if (command == kCommandEditor) {
                    _sourceEditor.Toggle();
                } else if (command >= kCommandDocumentBase &&
                           command < kCommandDocumentBase + _documentFiles.size()) {
                    LoadDocument(_documentFiles[command - kCommandDocumentBase]);
                }
                return 0;
            }
            case WM_MOUSEMOVE:
                if (_context) {
                    const auto [x, y] = ScaleMouse(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                    _context->ProcessMouseMove(x, y, GetModifiers());
                    if ((wParam & MK_LBUTTON) && _sourceEditor.IsMoveModeEnabled() && _dragCandidate) {
                        float deltaX = static_cast<float>(x - _dragStartX);
                        float deltaY = static_cast<float>(y - _dragStartY);
                        const bool constrainAxis = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        if (constrainAxis) {
                            if (_dragAxis == 0 && (std::abs(deltaX) >= 3.0f || std::abs(deltaY) >= 3.0f)) {
                                _dragAxis = std::abs(deltaX) >= std::abs(deltaY) ? 1 : 2;
                            }
                            if (_dragAxis == 1) deltaY = 0.0f;
                            else if (_dragAxis == 2) deltaX = 0.0f;
                        } else {
                            _dragAxis = 0;
                        }
                        if (!_directDragging && (std::abs(deltaX) >= 3.0f || std::abs(deltaY) >= 3.0f)) {
                            _directDragging = _inspector.BeginMove(_dragCandidate);
                        }
                        if (_directDragging) _inspector.UpdateMove(deltaX, deltaY);
                    }
                }
                return 0;
            case WM_LBUTTONDOWN:
                SetCapture(window);
                if (_context) {
                    const auto [x, y] = ScaleMouse(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                    _dragStartX = x;
                    _dragStartY = y;
                    _dragCandidate = nullptr;
                    _context->ProcessMouseMove(x, y, GetModifiers());
                    _context->ProcessMouseButtonDown(0, GetModifiers());
                }
                return 0;
            case WM_LBUTTONUP:
                ReleaseCapture();
                if (_context) {
                    _suppressNextClick = _directDragging;
                    _context->ProcessMouseButtonUp(0, GetModifiers());
                    if (_directDragging) {
                        const auto selector = _inspector.EndMove();
                        if (!selector.empty()) {
                            auto overridePath = _documentPath;
                            overridePath.replace_extension(".editor-overrides.rcss");
                            _sourceEditor.SetGeneratedContent(
                                overridePath, _inspector.SerializeOverrides(), selector);
                        }
                    }
                }
                _dragCandidate = nullptr;
                _directDragging = false;
                _dragAxis = 0;
                return 0;
            case WM_MOUSEWHEEL:
                if (_context) {
                    const float steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                    const auto previewFile = _documentPath.filename().string();
                    if (previewFile == "inventory.rml" ||
                        previewFile == "magic.rml") {
                        const float itemCount = static_cast<float>(
                            kSyntheticDatasetSizes[_syntheticDatasetIndex]);
                        const float maximum = std::max(0.0f, itemCount * 120.0f - 600.0f);
                        auto& logicalScrollTop = previewFile == "inventory.rml" ?
                            _inventoryPreviewSyncedScrollTop :
                            _magicPreviewSyncedScrollTop;
                        logicalScrollTop = std::clamp(
                            logicalScrollTop - steps * 120.0f,
                            0.0f,
                            maximum);
                        UpdateSyntheticVirtualRows(true);
                    } else {
                        _context->ProcessMouseWheel(
                            Rml::Vector2f(0.0f, -steps), GetModifiers());
                    }
                }
                return 0;
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) {
                    _exitRequested = true;
                } else if (wParam == VK_F1) {
                    SwitchDocument(PreviewDocument::Settings);
                } else if (wParam == VK_F2) {
                    SwitchDocument(PreviewDocument::Developer);
                } else if (wParam == VK_F3) {
                    SwitchDocument(PreviewDocument::ItemEdit);
                } else if (wParam == VK_F5) {
                    ReloadDocument();
                } else if (wParam == VK_F6) {
                    CycleSyntheticDataset();
                } else if (wParam == VK_F7) {
                    if (_documentPath.filename() == "welcome.rml") {
                        ShowPinTutorialPreview();
                    } else {
                        ToggleSyntheticEmptySearch();
                    }
                } else if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    const auto path = SelectDocumentWithDialog();
                    if (!path.empty()) LoadDocument(path);
                } else if (wParam == VK_F8 && _debuggerInitialized) {
                    Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
                } else if (wParam == VK_F9) {
                    _inspector.Toggle();
                } else if (wParam == VK_F10) {
                    _sourceEditor.Toggle();
                }
                return 0;
            default:
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }

        void HandleUiEvent(Rml::Event& event)
        {
            auto* element = event.GetTargetElement();
            auto* selectedElement = element;
            while (selectedElement && selectedElement != _document &&
                   selectedElement->GetTagName().starts_with("#")) {
                selectedElement = selectedElement->GetParentNode();
            }
            _inspector.SelectElement(selectedElement);
            _sourceEditor.SelectElement(selectedElement);

            if (event.GetType() == "mousedown") {
                _dragCandidate = selectedElement;
                return;
            }

            // Text and RmlUi's internal slider elements can be the event target.
            // Resolve them back to the nearest identified control so clicking a
            // button label behaves exactly like clicking its background.
            while (element && element != _document && element->GetId().empty()) {
                element = element->GetParentNode();
            }
            if (!element) return;
            const std::string id = element->GetId();
            if (id.empty()) return;

            if (event.GetType() == "change") {
                const float value = event.GetParameter<float>("value", 0.0f);
                if (auto* label = _document->GetElementById("value-" + id)) {
                    const bool degrees = id.starts_with("menuRot");
                    label->SetInnerRML(Rml::CreateString(degrees ? "%.1f deg" : "%.2f", value));
                }
                SetStatus("Changed " + id);
                return;
            }

            if (event.GetType() != "click") return;
            if (_suppressNextClick) {
                _suppressNextClick = false;
                return;
            }
            if (id.starts_with("tab-")) {
                SelectPage(kSettingsPages, "tab-", "page-", id.substr(4));
            } else if (id.starts_with("dev-tab-")) {
                SelectPage(kDeveloperPages, "dev-tab-", "dev-page-", id.substr(8));
            } else if (id == "journal-tab-quests") {
                SelectJournalPreviewPage("quests");
            } else if (id == "journal-tab-stats") {
                SelectJournalPreviewPage("stats");
            } else if (id == "journal-settings") {
                SetJournalQuestNavigationVisible(false);
                SelectPage(kJournalPages, "journal-tab-", "journal-page-", "");
                SetStatus("Settings replaces Journal in game");
            } else if (id == "mods-tab-actions") {
                SelectModsPreviewPage(false);
            } else if (id == "mods-tab-ini") {
                SelectModsPreviewPage(true);
            } else if (id == "edit-tab-pin") {
                SetStatus("Action: pin to dashboard");
            } else if (id.starts_with("edit-tab-")) {
                SelectPage(kItemEditPages, "edit-tab-", "edit-page-", id.substr(9));
            } else if (id.starts_with("dev-command-")) {
                try {
                    SelectMockCommand(static_cast<std::size_t>(std::stoul(id.substr(12))));
                } catch (...) {
                    SetStatus("Invalid command id");
                }
            } else if (id == "toggle-dev-panel") {
                _developerButtonEnabled = !_developerButtonEnabled;
                element->SetClass("enabled", _developerButtonEnabled);
                if (auto* state = _document->GetElementById("toggle-dev-state")) {
                    state->SetInnerRML(_developerButtonEnabled ? "Enabled" : "Disabled");
                }
            } else if (id == "toggle-edit-mode") {
                _editModeEnabled = !_editModeEnabled;
                element->SetClass("enabled", _editModeEnabled);
                if (auto* state = _document->GetElementById("toggle-edit-state")) {
                    state->SetInnerRML(_editModeEnabled ? "Enabled" : "Disabled");
                }
            } else if (id == "welcome-next-1") {
                if (auto* page = _document->GetElementById("welcome-page-1")) {
                    page->SetClass("active", false);
                    page->SetProperty("display", "none");
                }
                if (auto* page = _document->GetElementById("welcome-page-2")) {
                    page->SetClass("active", true);
                    page->SetProperty("display", "block");
                }
                SetStatus("Welcome page 2 - waiting for grab");
            } else if (id == "welcome-grab-instruction") {
                element->SetClass("completed", true);
                element->SetProperty("display", "none");
                if (auto* result = _document->GetElementById("welcome-grab-result")) {
                    result->SetClass("revealed", true);
                    result->SetProperty("display", "flex");
                }
                if (auto* next = _document->GetElementById("welcome-next-2")) {
                    next->SetClass("disabled", false);
                }
                SetStatus("Welcome page 2 - grab completed");
            } else if (id == "welcome-next-2") {
                auto* scale = _document->GetElementById("welcome-scale-instruction");
                if (scale && !scale->IsClassSet("revealed")) {
                    if (auto* result = _document->GetElementById("welcome-grab-result")) {
                        result->SetClass("revealed", false);
                        result->SetProperty("display", "none");
                    }
                    scale->SetClass("revealed", true);
                    scale->SetProperty("display", "flex");
                    SetStatus("Welcome page 2 - scale instruction");
                } else {
                    if (auto* page = _document->GetElementById("welcome-page-2")) {
                        page->SetClass("active", false);
                        page->SetProperty("display", "none");
                    }
                    if (auto* page = _document->GetElementById("welcome-page-3")) {
                        page->SetClass("active", true);
                        page->SetProperty("display", "block");
                    }
                    SetStatus("Welcome page 3 preview");
                }
            } else if (id == "dev-add-command") {
                SetStatus("SteamVR command keyboard opens in game");
            } else if (id == "dev-execute") {
                SetStatus("Execute: " + std::string(kMockCommands[_selectedMockCommand].command));
            } else {
                SetStatus("Clicked " + id);
            }
        }

    private:
        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            return g_app ? g_app->HandleMessage(window, message, wParam, lParam) :
                DefWindowProcW(window, message, wParam, lParam);
        }

        bool CreatePreviewWindow(HINSTANCE instance)
        {
            const wchar_t* className = L"DragonBoardRmlPreviewWindow";
            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = &PreviewApp::WindowProcedure;
            windowClass.hInstance = instance;
            windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
            windowClass.lpszClassName = className;
            if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

            constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            RECT rectangle{ 0, 0, kWindowWidth, kWindowHeight };
            AdjustWindowRect(&rectangle, style, FALSE);
            _window = CreateWindowExW(
                0, className, L"DragonBoard Rml Preview", style,
                CW_USEDEFAULT, CW_USEDEFAULT,
                rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                nullptr, nullptr, instance, nullptr);
            if (!_window) return false;
            CreateApplicationMenu();
            DragAcceptFiles(_window, TRUE);
            ShowWindow(_window, SW_SHOW);
            return true;
        }

        void CreateApplicationMenu()
        {
            const HMENU menuBar = CreateMenu();
            const HMENU fileMenu = CreatePopupMenu();
            _documentsMenu = CreatePopupMenu();

            AppendMenuW(fileMenu, MF_STRING, kCommandOpen, L"&Open RML...\tCtrl+O");
            AppendMenuW(fileMenu, MF_STRING, kCommandReload, L"&Reload\tF5");
            AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(fileMenu, MF_STRING, kCommandExit, L"E&xit\tEsc");

            AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");
            AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(_documentsMenu), L"&Documents");
            AppendMenuW(menuBar, MF_STRING, kCommandEditor, L"&Source Editor\tF10");
            AppendMenuW(menuBar, MF_STRING, kCommandInspector, L"&Inspector\tF9");
            SetMenu(_window, menuBar);
        }

        std::filesystem::path SelectDocumentWithDialog() const
        {
            std::wstring buffer(32768, L'\0');
            const auto initialDirectory = !_documentPath.empty() ?
                _documentPath.parent_path() : _assetsDirectory;
            const auto initialDirectoryString = initialDirectory.wstring();

            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = _window;
            dialog.lpstrFilter = L"RmlUi documents (*.rml)\0*.rml\0All files (*.*)\0*.*\0";
            dialog.lpstrFile = buffer.data();
            dialog.nMaxFile = static_cast<DWORD>(buffer.size());
            dialog.lpstrInitialDir = initialDirectoryString.empty() ? nullptr : initialDirectoryString.c_str();
            dialog.lpstrTitle = L"Open an RmlUi document";
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameW(&dialog)) return {};
            return std::filesystem::path(buffer.c_str());
        }

        void RefreshDocumentMenu()
        {
            if (!_documentsMenu) return;
            while (GetMenuItemCount(_documentsMenu) > 0) {
                DeleteMenu(_documentsMenu, 0, MF_BYPOSITION);
            }

            _documentFiles.clear();
            std::error_code error;
            const auto directory = _documentPath.parent_path();
            for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
                if (error || !entry.is_regular_file() || entry.path().extension() != ".rml") continue;
                _documentFiles.push_back(entry.path());
            }
            std::sort(_documentFiles.begin(), _documentFiles.end());

            if (_documentFiles.empty()) {
                AppendMenuW(_documentsMenu, MF_STRING | MF_DISABLED, 0, L"(No RML documents)");
            } else {
                for (std::size_t index = 0; index < _documentFiles.size(); ++index) {
                    const bool active = std::filesystem::equivalent(
                        _documentFiles[index], _documentPath, error);
                    const UINT flags = MF_STRING | (active ? MF_CHECKED : MF_UNCHECKED);
                    AppendMenuW(
                        _documentsMenu,
                        flags,
                        kCommandDocumentBase + static_cast<UINT>(index),
                        _documentFiles[index].filename().c_str());
                    error.clear();
                }
            }
            DrawMenuBar(_window);
        }

        bool CreateD3D()
        {
            DXGI_SWAP_CHAIN_DESC swapDescription{};
            swapDescription.BufferDesc.Width = kCanvasWidth;
            swapDescription.BufferDesc.Height = kCanvasHeight;
            swapDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapDescription.SampleDesc.Count = 1;
            swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapDescription.BufferCount = 1;
            swapDescription.OutputWindow = _window;
            swapDescription.Windowed = TRUE;
            swapDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            constexpr std::array featureLevels{
                D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1
            };
            D3D_FEATURE_LEVEL selectedFeatureLevel{};
            HRESULT result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                featureLevels.data(), static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                &swapDescription, _swapChain.GetAddressOf(), _device.GetAddressOf(),
                &selectedFeatureLevel, _deviceContext.GetAddressOf());
            if (result == E_INVALIDARG) {
                result = D3D11CreateDeviceAndSwapChain(
                    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                    featureLevels.data() + 1, static_cast<UINT>(featureLevels.size() - 1), D3D11_SDK_VERSION,
                    &swapDescription, _swapChain.GetAddressOf(), _device.GetAddressOf(),
                    &selectedFeatureLevel, _deviceContext.GetAddressOf());
            }
            if (FAILED(result)) return false;

            ComPtr<ID3D11Texture2D> backBuffer;
            if (FAILED(_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) return false;
            return SUCCEEDED(_device->CreateRenderTargetView(
                backBuffer.Get(), nullptr, _renderTarget.GetAddressOf()));
        }

        bool LoadFont()
        {
            const std::array<std::filesystem::path, 4> candidates{
                _assetsDirectory / "assets" / "DragonBoardVR_Font.ttf",
                std::filesystem::path("Data/SKSE/Plugins/DragonBoardVR/ui/assets/DragonBoardVR_Font.ttf"),
                std::filesystem::path("SKSE/Plugins/DragonBoardVR/ui/assets/DragonBoardVR_Font.ttf"),
                std::filesystem::path("Assets/ui/rml/assets/DragonBoardVR_Font.ttf")
            };
            bool primaryLoaded = false;
            for (const auto& path : candidates) {
                std::ifstream stream(path, std::ios::binary | std::ios::ate);
                if (!stream) continue;
                const auto size = stream.tellg();
                if (size <= 0) continue;
                _fontData.resize(static_cast<std::size_t>(size));
                stream.seekg(0);
                if (!stream.read(reinterpret_cast<char*>(_fontData.data()), size)) continue;
                const Rml::Span<const Rml::byte> bytes(_fontData.data(), _fontData.size());
                if (Rml::LoadFontFace(
                        bytes, "DragonBoard", Rml::Style::FontStyle::Normal,
                        Rml::Style::FontWeight::Normal)) {
                    primaryLoaded = true;
                    break;
                }
            }
            if (primaryLoaded) {
                const std::array<std::filesystem::path, 4> fallbackCandidates{
                    _assetsDirectory / "assets" / "NotoSansCJKsc-Regular.otf",
                    std::filesystem::path("Data/SKSE/Plugins/DragonBoardVR/ui/assets/NotoSansCJKsc-Regular.otf"),
                    std::filesystem::path("SKSE/Plugins/DragonBoardVR/ui/assets/NotoSansCJKsc-Regular.otf"),
                    std::filesystem::path("Assets/ui/rml/assets/NotoSansCJKsc-Regular.otf")
                };
                for (const auto& path : fallbackCandidates) {
                    std::ifstream stream(path, std::ios::binary | std::ios::ate);
                    if (!stream) continue;
                    const auto size = stream.tellg();
                    if (size <= 0) continue;
                    _fallbackFontData.resize(static_cast<std::size_t>(size));
                    stream.seekg(0);
                    if (!stream.read(
                            reinterpret_cast<char*>(_fallbackFontData.data()), size)) {
                        _fallbackFontData.clear();
                        continue;
                    }
                    const Rml::Span<const Rml::byte> bytes(
                        _fallbackFontData.data(), _fallbackFontData.size());
                    if (Rml::LoadFontFace(
                            bytes, "DragonBoardCJK", Rml::Style::FontStyle::Normal,
                            Rml::Style::FontWeight::Normal, true)) {
                        break;
                    }
                    _fallbackFontData.clear();
                }
                return true;
            }
            std::cerr << "No usable font was found.\n";
            return false;
        }

        bool LoadDocument(
            const std::filesystem::path& requestedPath,
            bool refreshSourceEditor = true)
        {
            if (!_context) return false;
            std::error_code error;
            auto path = std::filesystem::absolute(requestedPath, error);
            if (error) path = requestedPath;
            if (path.extension() != ".rml" || !std::filesystem::is_regular_file(path)) {
                SetStatus("Select a valid .rml file");
                return false;
            }

            std::ifstream stream(path, std::ios::binary);
            const std::string source{
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>() };
            auto* nextDocument = _context->LoadDocumentFromMemory(
                _localization.TranslateMarkup(source), path.string());
            if (!nextDocument) {
                SetStatus("Failed to load " + path.filename().string());
                return false;
            }

            if (_document) {
                _document->Close();
                _context->Update();
            }
            _document = nextDocument;
            _documentPath = path;
            _previewMetrics = {};
            _previewMetrics.OnVisibilityChanged(true);
            _lastPreviewFrame = std::chrono::steady_clock::now();
            _lastPerformanceReport = _lastPreviewFrame;
            _performanceSummary.clear();
            _poolViolationReported = false;
            BindDocument();
            _document->Show();
            if (refreshSourceEditor) _sourceEditor.SetDocument(_documentPath);
            _inspector.SetDocument(_document, _documentPath);
            RefreshDocumentMenu();
            _lastObservedWrite = LatestAssetWriteTime();
            SetStatus("Loaded");
            return true;
        }

        bool ReloadDocument(bool refreshSourceEditor = true)
        {
            return !_documentPath.empty() && LoadDocument(_documentPath, refreshSourceEditor);
        }

        void PreviewEditedSource(const std::filesystem::path& path, std::string source)
        {
            if (!_context || !_document) return;
            if (path.extension() == ".rml") {
                auto* nextDocument = _context->LoadDocumentFromMemory(
                    _localization.TranslateMarkup(source), path.string());
                if (!nextDocument) {
                    SetStatus("Preview waiting for valid RML - changes are not saved");
                    return;
                }
                _document->Close();
                _context->Update();
                _document = nextDocument;
                _documentPath = path;
                BindDocument();
                _document->Show();
                _inspector.SetDocument(_document, _documentPath);
                SetStatus("Unsaved RML preview");
                return;
            }
            if (path.extension() != ".rcss") return;

            auto overridePath = _documentPath;
            overridePath.replace_extension(".editor-overrides.rcss");
            std::error_code error;
            const bool equivalent = std::filesystem::equivalent(path, overridePath, error);
            const bool isOverride = equivalent ||
                (error && path.lexically_normal() == overridePath.lexically_normal());
            if (isOverride) {
                _inspector.SetOverrideSource(source);
                _document->UpdateDocument();
                SetStatus("Unsaved visual overrides preview");
                return;
            }

            if (!LoadDocument(_documentPath, false)) return;
            auto editedStyle = Rml::Factory::InstanceStyleSheetString(source);
            if (!editedStyle) {
                SetStatus("Preview waiting for valid RCSS - changes are not saved");
                return;
            }
            if (const auto* baseStyle = _document->GetStyleSheetContainer()) {
                editedStyle = baseStyle->CombineStyleSheetContainer(*editedStyle);
            }
            _document->SetStyleSheetContainer(std::move(editedStyle));
            _document->UpdateDocument();
            SetStatus("Unsaved RCSS preview");
        }

        void BindDocument()
        {
            if (_localization.ActiveCode() == "ru") {
                auto* languageRoot = _document->GetElementById("app");
                if (!languageRoot) languageRoot = _document->GetElementById("welcome-app");
                if (!languageRoot) languageRoot = _document->GetElementById("keyboard-app");
                if (!languageRoot) languageRoot = _document;
                languageRoot->SetClass("lang-ru", true);
            }

            // Listen once at the document root so arbitrary buttons and controls,
            // including elements created dynamically, work without C++ registration.
            _document->AddEventListener("click", _listener.get());
            _document->AddEventListener("mousedown", _listener.get());
            _document->AddEventListener("change", _listener.get());

            const auto fileName = _documentPath.filename().string();
            if (fileName == "settings.rml") {
                SelectPage(kSettingsPages, "tab-", "page-", "general");
            } else if (fileName == "dev.rml") {
                PopulateDeveloperDocument();
                SelectPage(kDeveloperPages, "dev-tab-", "dev-page-", "commands");
            } else if (fileName == "journal.rml") {
                PopulateJournalDocument();
                SelectJournalPreviewPage("quests");
            } else if (fileName == "edit.rml") {
                PopulateItemEditDocument();
                SelectPage(kItemEditPages, "edit-tab-", "edit-page-", "position");
            } else if (fileName == "mods.rml") {
                PopulateModsDocument();
            } else if (fileName == "inventory.rml") {
                PopulateInventoryDocument();
            } else if (fileName == "magic.rml") {
                PopulateMagicDocument();
            }
        }

        void PopulateJournalDocument()
        {
            if (auto* page = _document->GetElementById("journal-page-quests")) {
                page->SetProperty("display", "block");
            }
            if (auto* page = _document->GetElementById("journal-page-stats")) {
                page->SetProperty("display", "none");
            }
            SetText("journal-quest-type", "MAIN QUEST");
            SetText("journal-tracking-state", "TRACKED");
            SetText("journal-quest-title", "The Horn of Jurgen Windcaller");
            SetText(
                "journal-quest-summary",
                "Retrieve the Horn of Jurgen Windcaller from Ustengrav and return it to the Greybeards.");
            SetText("journal-active-quest", "Active quest: The Horn of Jurgen Windcaller");

            if (auto* list = _document->GetElementById("journal-quest-list")) {
                list->SetInnerRML(
                    "<div class=\"journal-section-label active-section\"><span>ACTIVE QUESTS</span></div>"
                    "<button id=\"journal-quest-20-1\" class=\"journal-quest-button quest-category-main active\"><i class=\"fade-layer fade-1\"></i>"
                    "<i class=\"fade-layer fade-2\"></i><i class=\"fade-layer fade-3\"></i>"
                    "<i class=\"fade-layer fade-4\"></i><i class=\"fade-layer fade-5\"></i>"
                    "<span class=\"journal-quest-marker\">&gt;</span><span id=\"journal-quest-name-20-1\" class=\"journal-quest-title\">"
                    "<span id=\"journal-quest-name-track-20-1\" class=\"journal-quest-title-track\">The Horn of Jurgen Windcaller</span></span></button>"
                    "<button class=\"journal-quest-button quest-category-side\"><span class=\"journal-quest-marker\"></span>"
                    "<span class=\"journal-quest-title\"><span class=\"journal-quest-title-track\">"
                    "A Blade in the Dark</span></span></button>"
                    "<button class=\"journal-quest-button quest-category-misc\"><span class=\"journal-quest-marker\"></span>"
                    "<span class=\"journal-quest-title\"><span class=\"journal-quest-title-track\">"
                    "Forbidden Legend of the Ancient Nords</span></span></button>"
                    "<div class=\"journal-section-label inactive-section\"><span>INACTIVE QUESTS</span></div>"
                    "<button class=\"journal-quest-button quest-category-side\"><span class=\"journal-quest-marker\"></span>"
                    "<span class=\"journal-quest-title\"><span class=\"journal-quest-title-track\">"
                    "The Golden Claw</span></span></button>");
            }

            if (auto* objectives = _document->GetElementById("journal-objective-list")) {
                objectives->SetInnerRML(
                    "<button class=\"journal-objective\"><span class=\"journal-objective-state\">ACTIVE</span>"
                    "<span class=\"journal-objective-text\">Retrieve the Horn of Jurgen Windcaller</span>"
                    "<span class=\"journal-map-marker\">&gt;</span></button>"
                    "<button class=\"journal-objective completed\"><span class=\"journal-objective-state\">DONE</span>"
                    "<span class=\"journal-objective-text\">Speak to Arngeir</span>"
                    "<span class=\"journal-map-marker\"></span></button>");
            }
        }

        void SetJournalQuestNavigationVisible(bool visible)
        {
            if (!_document) return;
            if (auto* filters = _document->GetElementById("journal-quest-filters")) {
                filters->SetProperty("display", visible ? "flex" : "none");
            }
            if (auto* list = _document->GetElementById("journal-quest-list")) {
                list->SetProperty("display", visible ? "block" : "none");
            }
            if (auto* activeQuest = _document->QuerySelector(".journal-footer-active")) {
                activeQuest->SetProperty("display", visible ? "flex" : "none");
            }
            if (auto* tracking = _document->GetElementById("journal-toggle-tracking")) {
                tracking->SetProperty("display", visible ? "flex" : "none");
            }
        }

        void SelectJournalPreviewPage(std::string_view selected)
        {
            SelectPage(
                kJournalPages,
                "journal-tab-",
                "journal-page-",
                selected);
            SetJournalQuestNavigationVisible(selected == "quests");
        }

        void PopulateItemEditDocument()
        {
            SetText("edit-item-name", "Ebony Sword");
            SetText("edit-category", "Weapons");
            SetText("edit-form-id", "000139B1");
            SetText("edit-model-path", "Weapons/Ebony/EbonySword.nif");
            SetText("edit-label-state", "Visible");
        }

        void PopulateModsDocument()
        {
            if (auto* actions = _document->GetElementById("mods-list")) {
                actions->SetInnerRML(
                    "<div id=\"mods-card-0\" class=\"mod-card\" tabindex=\"0\"><span class=\"mod-card-mark\">&lt;&gt;</span><span class=\"mod-card-label\">Campfire</span></div>"
                    "<div id=\"mods-card-1\" class=\"mod-card\" tabindex=\"0\"><span class=\"mod-card-mark\">&lt;&gt;</span><span class=\"mod-card-label\">Whirlwind Sprint</span></div>"
                    "<div id=\"mods-card-2\" class=\"mod-card\" tabindex=\"0\"><span class=\"mod-card-mark\">&lt;&gt;</span><span class=\"mod-card-label\">Travel Lantern</span></div>");
            }

            if (auto* list = _document->GetElementById("mods-ini-list")) {
                const auto appendCard = [](std::string& markup, std::string_view name,
                                            std::string_view, bool active) {
                    markup += "<div class=\"ini-mod-row\"><button class=\"ini-mod-card";
                    if (active) markup += " active";
                    markup += "\">"
                        "<div class=\"sidebar-fade sidebar-fade-whisper\"></div>"
                        "<div class=\"sidebar-fade sidebar-fade-soft\"></div>"
                        "<div class=\"sidebar-fade sidebar-fade-mid\"></div>"
                        "<div class=\"sidebar-fade sidebar-fade-strong\"></div>"
                        "<div class=\"sidebar-fade sidebar-fade-solid\"></div>"
                        "<span class=\"ini-mod-name\">";
                    markup += name;
                    markup += "</span></button>"
                        "<button class=\"ini-row-visibility\"><span>X</span></button></div>";
                };
                std::string markup;
                appendCard(markup, "Aetherius - A Race Overhaul", "1 INI", false);
                appendCard(markup, "Apothecary - An Alchemy Overhaul", "1 INI", false);
                appendCard(markup, "Auto Parallax", "1 INI", false);
                appendCard(markup, "Azurite Weathers II", "1 INI", false);
                appendCard(markup, "Behavior Data Injector", "1 INI", true);
                appendCard(markup, "HIGGS - Hand Interaction and Gravity Gloves for Skyrim VR", "1 INI", false);
                appendCard(markup, "Blade and Blunt - A Combat Overhaul", "3 INI", false);
                appendCard(markup, "BOS Handcarts 2K", "1 INI", false);
                appendCard(markup, "Dragonborn Voice Over", "2 INI", false);
                appendCard(markup, "Enhanced Volumetric Lighting", "1 INI", false);
                list->SetInnerRML(markup);
            }

            if (auto* detail = _document->GetElementById("mods-ini-detail")) {
                detail->SetInnerRML(
                    "<div class=\"ini-file\">"
                    "<div class=\"ini-section-name\">[Debug]</div>"
                    "<div class=\"ini-setting-row\"><span class=\"ini-setting-key\">EnableDebugLog</span><div class=\"ini-setting-value-column\"><div class=\"ini-setting-description\">Writes diagnostic events to the SKSE log.</div><button class=\"ini-setting-value boolean\"><span>FALSE</span></button></div></div>"
                    "<div class=\"ini-setting-row\"><span class=\"ini-setting-key\">EnableAnimationLog</span><div class=\"ini-setting-value-column\"><button class=\"ini-setting-value boolean enabled\"><span>TRUE</span></button></div></div>"
                    "<div class=\"ini-section-name\">[General]</div>"
                    "<div class=\"ini-setting-row\"><span class=\"ini-setting-key\">AnimationEventCacheSize</span><div class=\"ini-setting-value-column\"><div class=\"ini-setting-description\">Maximum number of animation events retained in memory.</div><button class=\"ini-setting-value\"><span>2048</span></button></div></div>"
                    "<div class=\"ini-setting-row\"><span class=\"ini-setting-key\">ExcludedBehaviorProjectPath</span><div class=\"ini-setting-value-column\"><button class=\"ini-setting-value\"><span>meshes/actors/character/behaviors/0_master.hkx</span></button></div></div>"
                    "<div class=\"ini-setting-row\"><span class=\"ini-setting-key\">VeryLongSettingNameForVRReadability</span><div class=\"ini-setting-value-column\"><div class=\"ini-setting-description\">Selecting this card opens the SteamVR keyboard.</div><button class=\"ini-setting-value\"><span>A long value opened with the VR keyboard</span></button></div></div>"
                    "<div class=\"ini-setting-row\"><span class=\"ini-setting-key\">ReloadBehaviorGraph</span><div class=\"ini-setting-value-column\"><button class=\"ini-setting-value boolean\"><span>FALSE</span></button></div></div>"
                    "</div>");
            }

            if (auto* head = _document->GetElementById("mods-ini-editor-head")) {
                head->SetProperty("display", "flex");
            }
            SetText("mods-ini-mod-title", "Behavior Data Injector");
            if (auto* tabs = _document->GetElementById("mods-ini-file-tabs")) {
                tabs->SetInnerRML(
                    "<button class=\"ini-file-tab active\"><span>BehaviorDataInjector.ini</span></button>"
                    "<button class=\"ini-file-tab\"><span>AnimationEvents.ini</span></button>"
                    "<button class=\"ini-file-tab conflict\"><span>Compatibility.ini</span></button>"
                    "<button class=\"ini-file-tab\"><span>AdvancedOverrides.ini</span></button>"
                    "<button class=\"ini-file-tab\"><span>RuntimePatches.ini</span></button>"
                    "<button class=\"ini-file-tab\"><span>DebugProfiles.ini</span></button>");
            }

            SetText("mods-ini-status", "Profile: FUS RO DAH | Mods: 422 | INIs: 239");
            SetText("mods-ini-search-text", "SEARCH");
            if (auto* hidden =
                    _document->GetElementById("mods-ini-show-hidden")) {
                hidden->SetInnerRML("<span>HIDDEN (12)</span>");
            }
            if (auto* clear =
                    _document->GetElementById("mods-ini-search-clear")) {
                clear->SetProperty("display", "none");
            }
            SelectModsPreviewPage(true);
        }

        void SelectModsPreviewPage(bool ini)
        {
            if (auto* tab = _document->GetElementById("mods-tab-actions")) {
                tab->SetClass("active", !ini);
            }
            if (auto* tab = _document->GetElementById("mods-tab-ini")) {
                tab->SetClass("active", ini);
            }
            if (auto* view = _document->GetElementById("mods-actions-view")) {
                view->SetProperty("display", ini ? "none" : "block");
            }
            if (auto* view = _document->GetElementById("mods-ini-view")) {
                view->SetProperty("display", ini ? "block" : "none");
            }
            if (auto* list = _document->GetElementById("mods-ini-list")) {
                list->SetProperty("display", ini ? "block" : "none");
            }
            if (auto* tools =
                    _document->GetElementById("mods-ini-sidebar-tools")) {
                tools->SetProperty("display", ini ? "block" : "none");
            }
            if (auto* hidden =
                    _document->GetElementById("mods-ini-show-hidden")) {
                hidden->SetProperty("display", ini ? "flex" : "none");
            }
            if (auto* footer = _document->GetElementById("mods-actions-footer")) {
                footer->SetProperty("display", ini ? "none" : "flex");
            }
            if (auto* footer = _document->GetElementById("mods-ini-footer")) {
                footer->SetProperty("display", ini ? "block" : "none");
            }
            if (auto* add = _document->GetElementById("mods-add")) {
                add->SetProperty("display", ini ? "none" : "flex");
            }
            if (auto* discard = _document->GetElementById("mods-ini-discard")) {
                discard->SetProperty("display", ini ? "flex" : "none");
            }
            if (auto* save = _document->GetElementById("mods-ini-save")) {
                save->SetProperty("display", ini ? "flex" : "none");
            }
        }

        void PopulateDeveloperDocument()
        {
            auto* list = _document->GetElementById("dev-command-list");
            if (!list) return;
            std::string markup;
            for (std::size_t index = 0; index < kMockCommands.size(); ++index) {
                markup += "<button id=\"dev-command-" + std::to_string(index) +
                    "\" class=\"command-item\"><span>" +
                    kMockCommands[index].label + "</span></button><br />";
            }
            list->SetInnerRML(markup);
            SelectMockCommand(0);

            SetText("dev-fps", "90.0");
            SetText("dev-frame-time", "11.11 ms");
            SetText("dev-update-timing", "0.20 / 0.28 ms");
            SetText("dev-render-timing", "1.71 / 2.10 ms");
            SetText("dev-total-timing", "2.50 / 3.22 ms");
            SetText("dev-draw-calls", "24");
            SetText("dev-dom-elements", "930");
            SetText("dev-renders-per-second", "90.0");
            SetText("dev-cached-frames", "0");
            SetText("dev-texture-size", "1920 x 1080");
            SetText("dev-active-document", "Developer");
            SetText("dev-dirty-reason", "Pointer");
            SetText("dev-helper", "Connected");
            SetText("dev-version", "0.8 preview");
        }

        static std::string SyntheticName(bool inventory, std::size_t index)
        {
            if (index % 9 == 0) {
                return inventory ?
                    "Nordic Greatsword of the Unrelenting Ancient Dragonborn Champion " +
                        std::to_string(index + 1) :
                    "Conjure Ancient Dragon Priest Guardian of the Forgotten Realm " +
                        std::to_string(index + 1);
            }
            static constexpr std::array<const char*, 5> inventoryNames{
                "Ebony Sword", "Dragonscale Armor", "Potion of Ultimate Healing",
                "Black Soul Gem", "Elven Bow"
            };
            static constexpr std::array<const char*, 5> magicNames{
                "Flames", "Fast Healing", "Oakflesh", "Clairvoyance", "Fireball"
            };
            const auto& names = inventory ? inventoryNames : magicNames;
            return std::string(names[index % names.size()]) + " " +
                std::to_string(index + 1);
        }

        static float Milliseconds(
            std::chrono::steady_clock::time_point start,
            std::chrono::steady_clock::time_point end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }

        static std::size_t CountDomElements(const Rml::Element* element)
        {
            if (!element) return 0;
            std::size_t count = 1;
            const int childCount = element->GetNumChildren(false);
            for (int index = 0; index < childCount; ++index) {
                count += CountDomElements(element->GetChild(index));
            }
            return count;
        }

        static std::size_t CountElementsWithIdPrefix(
            const Rml::Element* element,
            std::string_view prefix)
        {
            if (!element) return 0;
            std::size_t count =
                element->GetTagName() == "button" &&
                element->GetId().starts_with(prefix) ? 1 : 0;
            const int childCount = element->GetNumChildren(false);
            for (int index = 0; index < childCount; ++index) {
                count += CountElementsWithIdPrefix(element->GetChild(index), prefix);
            }
            return count;
        }

        void RecordPreviewPerformance(
            std::chrono::steady_clock::time_point frameStart,
            std::chrono::steady_clock::time_point updateStart,
            std::chrono::steady_clock::time_point updateEnd,
            std::chrono::steady_clock::time_point beginStart,
            std::chrono::steady_clock::time_point beginEnd,
            std::chrono::steady_clock::time_point renderStart,
            std::chrono::steady_clock::time_point renderEnd,
            std::chrono::steady_clock::time_point endStart,
            std::chrono::steady_clock::time_point endEnd)
        {
            const auto now = std::chrono::steady_clock::now();
            const float frameSeconds = std::chrono::duration<float>(
                now - _lastPreviewFrame).count();
            _lastPreviewFrame = now;
            _previewMetrics.AdvanceRateWindow(frameSeconds);

            RmlPerformanceMetrics::RenderTiming timing;
            timing.updateMs = Milliseconds(updateStart, updateEnd);
            timing.beginFrameMs = Milliseconds(beginStart, beginEnd);
            timing.renderMs = Milliseconds(renderStart, renderEnd);
            timing.endFrameMs = Milliseconds(endStart, endEnd);
            timing.totalMs = Milliseconds(frameStart, endEnd);
            timing.domElements = CountDomElements(_document);
            timing.width = kCanvasWidth;
            timing.height = kCanvasHeight;
            timing.activeDocument = _documentPath.filename().string();
            _previewMetrics.RecordRenderedFrame(
                frameSeconds,
                _renderer.GetDrawCallCount(),
                timing,
                "Preview");

            if (now - _lastPerformanceReport < std::chrono::seconds(1)) return;
            _lastPerformanceReport = now;
            const auto snapshot = _previewMetrics.GetSnapshot();
            const auto fileName = _documentPath.filename().string();
            const std::string_view rowPrefix = fileName == "inventory.rml" ?
                "inventory-item-" : fileName == "magic.rml" ?
                "magic-spell-" : "";
            const auto materializedRows = rowPrefix.empty() ? 0 :
                CountElementsWithIdPrefix(_document, rowPrefix);
            if (!rowPrefix.empty() && materializedRows > 10 && !_poolViolationReported) {
                logger::error(
                    "Synthetic virtual-list DOM limit exceeded: {} rows.",
                    materializedRows);
                _poolViolationReported = true;
            }
            _performanceSummary = std::format(
                "DOM {}  Rows {}  Update avg/p95 {:.2f}/{:.2f} ms  "
                "Render avg/p95 {:.2f}/{:.2f} ms  Draws {}{}",
                snapshot.domElements,
                materializedRows,
                snapshot.update.averageMs,
                snapshot.update.p95Ms,
                snapshot.render.averageMs,
                snapshot.render.p95Ms,
                snapshot.panelDrawCalls,
                _poolViolationReported ? "  POOL VIOLATION" : "");
            RefreshWindowTitle();
        }

        void UpdateSyntheticVirtualRows(bool force)
        {
            if (!_document) return;
            const auto fileName = _documentPath.filename().string();
            const bool inventory = fileName == "inventory.rml";
            const bool magic = fileName == "magic.rml";
            if (!inventory && !magic) return;

            const char* listId = inventory ? "inventory-item-list" : "magic-spell-list";
            auto* listElement = _document->GetElementById(listId);
            if (!listElement) return;
            auto* scrollElement = inventory ?
                _document->GetElementById("inventory-scroll-proxy") :
                _document->GetElementById("magic-scroll-proxy");

            if (_syntheticEmptySearch) {
                if (!force) return;
                (inventory ? _inventoryPreviewList : _magicPreviewList).Reset();
                if (scrollElement) scrollElement->SetScrollTop(0.0f);
                if (inventory) {
                    _inventoryPreviewSyncedScrollTop = 0.0f;
                } else {
                    _magicPreviewSyncedScrollTop = 0.0f;
                }
                if (auto* thumb = _document->GetElementById(
                        inventory ? "inventory-scroll-thumb" : "magic-scroll-thumb")) {
                    thumb->SetProperty("top", "0px");
                    thumb->SetProperty("height", "600px");
                }
                listElement->SetInnerRML(inventory ?
                    "<div class=\"inventory-empty\">No matching items</div>" :
                    "<div class=\"magic-empty\">No matching spells</div>");
                SetText(inventory ? "inventory-item-count" : "magic-spell-count", "0");
                return;
            }

            const auto itemCount = kSyntheticDatasetSizes[_syntheticDatasetIndex];
            auto& virtualList = inventory ? _inventoryPreviewList : _magicPreviewList;
            virtualList.SetItemCount(itemCount);
            float scrollTop = inventory ?
                _inventoryPreviewSyncedScrollTop :
                _magicPreviewSyncedScrollTop;
            {
                const float totalHeight =
                    static_cast<float>(itemCount) * 120.0f;
                const float maximumScroll =
                    std::max(0.0f, totalHeight - 600.0f);
                scrollTop = std::clamp(scrollTop, 0.0f, maximumScroll);
                listElement->SetScrollTop(0.0f);
                if (inventory) {
                    _inventoryPreviewSyncedScrollTop = scrollTop;
                } else {
                    _magicPreviewSyncedScrollTop = scrollTop;
                }
                if (auto* thumb = _document->GetElementById(
                        inventory ? "inventory-scroll-thumb" : "magic-scroll-thumb")) {
                    const float thumbHeight = totalHeight > 0.0f ?
                        std::clamp(600.0f * 600.0f / totalHeight, 104.0f, 600.0f) :
                        600.0f;
                    const float thumbTravel = 600.0f - thumbHeight;
                    const float thumbTop = maximumScroll > 0.0f ?
                        thumbTravel * scrollTop / maximumScroll : 0.0f;
                    thumb->SetProperty(
                        "height", Rml::CreateString("%.0fpx", thumbHeight));
                    thumb->SetProperty(
                        "top", Rml::CreateString("%.0fpx", thumbTop));
                }
            }
            const bool windowChanged = virtualList.Update(
                scrollTop + 60.0f);
            if (!force && !windowChanged) {
                if (auto* content = _document->GetElementById(
                        inventory ?
                            "inventory-virtual-content" :
                            "magic-virtual-content")) {
                    content->RemoveProperty("top");
                }
                return;
            }

            const auto& window = virtualList.GetWindow();
            const char* contentClass = inventory ?
                "inventory-virtual-content" : "magic-virtual-content";
            std::string markup = "<div class=\"" + std::string(contentClass) +
                "\" style=\"height: 600px;\">";
            for (std::size_t slot = 0; slot < window.rowCount; ++slot) {
                const auto itemIndex = window.firstIndex + slot;
                std::string marker;
                if (itemIndex % 8 == 0) marker = "[L/R]";
                else if (itemIndex % 8 == 1) marker = "[L]";
                else if (itemIndex % 8 == 2) marker = "[R]";

                std::string classes = inventory ?
                    "inventory-list-item" : "magic-list-item";
                if (itemIndex == window.firstIndex + window.rowCount / 2) {
                    classes += " active";
                }
                if (!marker.empty()) classes += " equipped";
                if (itemIndex % 5 == 0) classes += " favorited";

                const auto indexText = std::to_string(itemIndex);
                markup += "<button id=\"" +
                    std::string(inventory ? "inventory-item-" : "magic-spell-") +
                    indexText + "\" class=\"" + classes + "\" style=\"top: " +
                    std::to_string(static_cast<int>(
                        static_cast<float>(slot) * 120.0f)) +
                    "px;\">";
                markup += "<span class=\"row-fade row-fade-solid\"></span>"
                    "<span class=\"row-fade row-fade-strong\"></span>"
                    "<span class=\"row-fade row-fade-mid\"></span>"
                    "<span class=\"row-fade row-fade-soft\"></span>"
                    "<span class=\"row-fade row-fade-whisper\"></span>";
                markup += "<span class=\"" +
                    std::string(inventory ? "item-state-mark" : "spell-state-mark") +
                    "\">" + marker + "</span>";
                markup += "<span class=\"" +
                    std::string(inventory ? "item-name" : "spell-name") +
                    "\"><span class=\"" +
                    std::string(inventory ? "item-name-track" : "spell-name-track") +
                    "\">" + SyntheticName(inventory, itemIndex) + "</span></span>";
                if (inventory) {
                    markup += "<span class=\"item-stack\">";
                    if (itemIndex % 6 == 0) markup += "x" + std::to_string(itemIndex % 4 + 2);
                    markup += "</span>";
                }
                markup += "</button>";
            }
            markup += "</div>";
            listElement->SetInnerRML(markup);
            listElement->SetScrollTop(0.0f);
            SetText(
                inventory ? "inventory-item-count" : "magic-spell-count",
                std::to_string(itemCount));
        }

        void CycleSyntheticDataset()
        {
            _syntheticDatasetIndex =
                (_syntheticDatasetIndex + 1) % kSyntheticDatasetSizes.size();
            _syntheticEmptySearch = false;
            _inventoryPreviewList.Reset();
            _magicPreviewList.Reset();
            _inventoryPreviewSyncedScrollTop = 0.0f;
            _magicPreviewSyncedScrollTop = 0.0f;
            if (_document) {
                if (auto* scroll =
                        _document->GetElementById("inventory-scroll-proxy")) {
                    scroll->SetScrollTop(0.0f);
                }
                if (auto* list = _document->GetElementById("magic-spell-list")) {
                    list->SetScrollTop(0.0f);
                }
            }
            UpdateSyntheticVirtualRows(true);
            SetStatus(
                "Synthetic dataset: " +
                std::to_string(kSyntheticDatasetSizes[_syntheticDatasetIndex]) +
                " entries, max 10 DOM rows");
        }

        void ShowPinTutorialPreview()
        {
            if (!_document) return;
            for (std::uint8_t candidate = 1; candidate <= 3; ++candidate) {
                const auto id = "welcome-page-" + std::to_string(candidate);
                if (auto* page = _document->GetElementById(id)) {
                    page->SetClass("active", false);
                    page->SetProperty("display", "none");
                }
            }
            if (auto* pin = _document->GetElementById("pin-tutorial-page")) {
                pin->SetClass("active", true);
                pin->SetProperty("display", "block");
                SetStatus("Pin tutorial preview");
            }
        }

        void ToggleSyntheticEmptySearch()
        {
            const auto fileName = _documentPath.filename().string();
            if (fileName != "inventory.rml" && fileName != "magic.rml") {
                SetStatus("F7 empty-search scenario requires Inventory or Magic");
                return;
            }
            _syntheticEmptySearch = !_syntheticEmptySearch;
            if (!_syntheticEmptySearch) {
                _inventoryPreviewList.Reset();
                _magicPreviewList.Reset();
            }
            UpdateSyntheticVirtualRows(true);
            SetStatus(_syntheticEmptySearch ?
                "Synthetic search: no results" :
                "Synthetic search cleared");
        }

        void PopulateInventoryDocument()
        {
            _syntheticEmptySearch = false;
            _inventoryPreviewList.Reset();
            _inventoryPreviewSyncedScrollTop = 0.0f;
            if (auto* scroll =
                    _document->GetElementById("inventory-scroll-proxy")) {
                scroll->SetScrollTop(0.0f);
            }
            UpdateSyntheticVirtualRows(true);
            if (auto* scroll =
                    _document->GetElementById("inventory-scroll-proxy")) {
                scroll->SetScrollTop(0.0f);
            }
            if (auto* filter = _document->GetElementById("inventory-filter-weapons")) {
                filter->SetClass("active", true);
            }
            SetText("inventory-player-name", "Arthas");
            SetText("inventory-player-level", "42");
            SetText("inventory-gold", "12.840");
            SetText("inventory-carry-weight", "218.5 / 420");
            SetText("inventory-health-text", "276 / 320");
            SetText("inventory-stamina-text", "188 / 240");
            SetText("inventory-magicka-text", "92 / 180");
            if (auto* fill = _document->GetElementById("inventory-health-fill")) {
                fill->SetProperty("width", "455px");
            }
            if (auto* fill = _document->GetElementById("inventory-stamina-fill")) {
                fill->SetProperty("width", "414px");
            }
            if (auto* fill = _document->GetElementById("inventory-magicka-fill")) {
                fill->SetProperty("width", "270px");
            }
            SetText(
                "inventory-item-count",
                std::to_string(kSyntheticDatasetSizes[_syntheticDatasetIndex]));
            SetText("inventory-selected-category", "WEAPON");
            SetText(
                "inventory-selected-name",
                "Nordic Bow of the Ancient Dragonborn Champion");
            if (auto* left = _document->GetElementById("inventory-left-hand-state")) {
                left->SetClass("active", true);
            }
            if (auto* right = _document->GetElementById("inventory-right-hand-state")) {
                right->SetClass("active", true);
            }
            SetText("inventory-attack", "18");
            SetText("inventory-defense", "--");
            SetText("inventory-weight", "12.0");
            SetText("inventory-value", "580");
            SetText("inventory-count", "1");
            SetText("inventory-description", "A finely crafted Nordic bow with strong draw weight and excellent range.");
            SetText("inventory-equip-label", "UNEQUIP");
        }

        void PopulateMagicDocument()
        {
            _syntheticEmptySearch = false;
            _magicPreviewList.Reset();
            _magicPreviewSyncedScrollTop = 0.0f;
            if (auto* scroll =
                    _document->GetElementById("magic-scroll-proxy")) {
                scroll->SetScrollTop(0.0f);
            }
            UpdateSyntheticVirtualRows(true);
            if (auto* filter = _document->GetElementById("magic-filter-conjuration")) {
                filter->SetClass("active", true);
            }
            SetText("magic-player-name", "Arthas");
            SetText("magic-player-level", "42");
            SetText(
                "magic-spell-count",
                std::to_string(kSyntheticDatasetSizes[_syntheticDatasetIndex]));
            SetText("magic-selected-category", "CONJURATION");
            SetText("magic-selected-name", "Conjure Ancient Dragon Priest Guardian");
            SetText("magic-cost", "176");
            SetText("magic-skill-level", "EXPERT");
            SetText("magic-cast-type", "FIRE AND FORGET");
            SetText("magic-target", "AIMED");
            SetText("magic-duration", "60 SEC");
            SetText("magic-range", "1000");
            SetText(
                "magic-description",
                "Summons an ancient Dragon Priest guardian for 60 seconds wherever the caster is aiming.");
            SetText("magic-equip-label", "UNEQUIP");
            SetText("magic-magicka", "248 / 310");
            if (auto* fill = _document->GetElementById("magic-magicka-fill")) {
                fill->SetProperty("width", "422px");
            }
            if (auto* left = _document->GetElementById("magic-left-hand-state")) {
                left->SetClass("active", false);
            }
            if (auto* right = _document->GetElementById("magic-right-hand-state")) {
                right->SetClass("active", true);
            }
            if (auto* icon = _document->GetElementById("magic-preview-icon")) {
                icon->SetAttribute("src", "assets/conjurationicon.png");
                icon->SetProperty("display", "block");
            }
            if (auto* edit = _document->GetElementById("magic-edit")) {
                edit->SetClass("enabled", true);
            }
        }

        template <std::size_t Size>
        void SelectPage(
            const std::array<const char*, Size>& pages,
            std::string_view tabPrefix,
            std::string_view pagePrefix,
            std::string_view selected)
        {
            for (const auto* page : pages) {
                const bool active = selected == page;
                if (auto* tab = _document->GetElementById(std::string(tabPrefix) + page)) {
                    tab->SetClass("active", active);
                }
                if (auto* content = _document->GetElementById(std::string(pagePrefix) + page)) {
                    content->SetProperty("display", active ? "block" : "none");
                }
            }
        }

        void SelectMockCommand(std::size_t index)
        {
            _selectedMockCommand = std::min(index, kMockCommands.size() - 1);
            for (std::size_t commandIndex = 0; commandIndex < kMockCommands.size(); ++commandIndex) {
                if (auto* element = _document->GetElementById(
                        "dev-command-" + std::to_string(commandIndex))) {
                    element->SetClass("active", commandIndex == _selectedMockCommand);
                }
            }
            const auto& command = kMockCommands[_selectedMockCommand];
            SetText("dev-command-title", command.label);
            SetText("dev-command-description", command.description);
            SetText("dev-command-input", command.command);
            if (auto* warning = _document->GetElementById("dev-command-warning")) {
                warning->SetProperty("display", command.dangerous ? "block" : "none");
            }
        }

        void SetText(const char* id, std::string_view value)
        {
            if (auto* element = _document->GetElementById(id)) {
                element->SetInnerRML(_localization.Translate(value));
            }
        }

        void SwitchDocument(PreviewDocument document)
        {
            const char* fileName = "edit.rml";
            if (document == PreviewDocument::Settings) fileName = "settings.rml";
            else if (document == PreviewDocument::Developer) fileName = "dev.rml";
            std::array<std::filesystem::path, 2> candidates{
                _documentPath.parent_path() / fileName,
                _assetsDirectory / fileName
            };
            for (const auto& candidate : candidates) {
                if (std::filesystem::is_regular_file(candidate)) {
                    LoadDocument(candidate);
                    return;
                }
            }
            SetStatus(std::string("Could not find ") + fileName);
        }

        void CheckHotReload()
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - _lastReloadCheck < std::chrono::milliseconds(250)) return;
            _lastReloadCheck = now;
            const auto latest = LatestAssetWriteTime();
            if (latest > _lastObservedWrite) ReloadDocument();
        }

        std::filesystem::file_time_type LatestAssetWriteTime() const
        {
            std::filesystem::file_time_type latest{};
            std::error_code error;
            const auto directory = _documentPath.parent_path();
            if (directory.empty()) return latest;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
                if (error || !entry.is_regular_file()) continue;
                const auto extension = entry.path().extension().string();
                if (extension != ".rml" && extension != ".rcss" &&
                    extension != ".ttf" && extension != ".otf" &&
                    extension != ".png" && extension != ".jpeg") continue;
                latest = std::max(latest, entry.last_write_time(error));
            }
            return latest;
        }

        static std::filesystem::path FindAssetsDirectory()
        {
            std::array<std::filesystem::path, 2> starts{
                std::filesystem::current_path(),
                std::filesystem::path(GetExecutablePath()).parent_path()
            };
            for (auto start : starts) {
                for (int depth = 0; depth < 8 && !start.empty(); ++depth) {
                    const auto candidate = start / "Assets" / "ui" / "rml";
                    if (std::filesystem::is_directory(candidate)) return candidate;
                    start = start.parent_path();
                }
            }
            return {};
        }

        static std::wstring GetExecutablePath()
        {
            std::wstring path(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            path.resize(length);
            return path;
        }

        std::pair<int, int> ScaleMouse(int clientX, int clientY) const
        {
            RECT client{};
            GetClientRect(_window, &client);
            const int width = std::max(1L, client.right - client.left);
            const int height = std::max(1L, client.bottom - client.top);
            return {
                std::clamp(clientX * kCanvasWidth / width, 0, kCanvasWidth - 1),
                std::clamp(clientY * kCanvasHeight / height, 0, kCanvasHeight - 1)
            };
        }

        static int GetModifiers()
        {
            int modifiers = 0;
            if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= Rml::Input::KM_SHIFT;
            if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= Rml::Input::KM_CTRL;
            if (GetKeyState(VK_MENU) & 0x8000) modifiers |= Rml::Input::KM_ALT;
            return modifiers;
        }

        void SetStatus(std::string status)
        {
            _status = std::move(status);
            RefreshWindowTitle();
        }

        void RefreshWindowTitle()
        {
            const std::string document = _documentPath.empty() ?
                "No document" : _documentPath.filename().string();
            const std::string title = "DragonBoard Rml Preview - " + document + " - " + _status +
                (_performanceSummary.empty() ? "" : " | " + _performanceSummary) +
                " | Ctrl+O Open  F5 Reload  F6 Dataset  F7 Empty Search  F9 Inspector  F10 Editor";
            SetWindowTextA(_window, title.c_str());
        }

        HWND _window = nullptr;
        HMENU _documentsMenu = nullptr;
        ComPtr<ID3D11Device> _device;
        ComPtr<ID3D11DeviceContext> _deviceContext;
        ComPtr<IDXGISwapChain> _swapChain;
        ComPtr<ID3D11RenderTargetView> _renderTarget;
        DragonBoardRmlRenderer _renderer;
        RmlSourceEditor _sourceEditor;
        RmlVisualInspector _inspector;
        PreviewSystemInterface _systemInterface;
        Rml::Context* _context = nullptr;
        Rml::ElementDocument* _document = nullptr;
        std::unique_ptr<PreviewEventListener> _listener;
        std::vector<unsigned char> _fontData;
        std::vector<unsigned char> _fallbackFontData;
        LocalizationManager _localization;
        std::filesystem::path _assetsDirectory;
        std::filesystem::path _documentPath;
        std::vector<std::filesystem::path> _documentFiles;
        std::filesystem::file_time_type _lastObservedWrite{};
        std::chrono::steady_clock::time_point _lastReloadCheck{};
        std::size_t _selectedMockCommand = 0;
        std::size_t _syntheticDatasetIndex = 0;
        RmlVirtualList _inventoryPreviewList{ 600.0f, 120.0f, 0 };
        float _inventoryPreviewSyncedScrollTop = 0.0f;
        RmlVirtualList _magicPreviewList{ 600.0f, 120.0f, 0 };
        float _magicPreviewSyncedScrollTop = 0.0f;
        RmlPerformanceMetrics _previewMetrics;
        std::string _status;
        std::string _performanceSummary;
        std::chrono::steady_clock::time_point _lastPreviewFrame{};
        std::chrono::steady_clock::time_point _lastPerformanceReport{};
        bool _syntheticEmptySearch = false;
        bool _poolViolationReported = false;
        bool _developerButtonEnabled = true;
        bool _editModeEnabled = true;
        bool _rmlInitialized = false;
        bool _debuggerInitialized = false;
        bool _exitRequested = false;
        Rml::Element* _dragCandidate = nullptr;
        int _dragStartX = 0;
        int _dragStartY = 0;
        bool _directDragging = false;
        bool _suppressNextClick = false;
        int _dragAxis = 0;  // 0 = free, 1 = horizontal, 2 = vertical.
    };

    void PreviewEventListener::ProcessEvent(Rml::Event& event)
    {
        _app.HandleUiEvent(event);
    }
}

int wmain(int argumentCount, wchar_t* arguments[])
{
    SetProcessDPIAware();
    std::optional<std::filesystem::path> requestedDocument;
    std::string requestedLanguage = "en";
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument == L"--language" && index + 1 < argumentCount) {
            const std::wstring value(arguments[++index]);
            requestedLanguage.assign(value.begin(), value.end());
        } else if (argument.starts_with(L"--language=")) {
            const auto value = argument.substr(11);
            requestedLanguage.assign(value.begin(), value.end());
        } else if (!requestedDocument) {
            requestedDocument = std::filesystem::path(argument);
        }
    }

    PreviewApp app;
    g_app = &app;
    if (!app.Initialize(
            GetModuleHandleW(nullptr), requestedDocument, requestedLanguage)) {
        std::cerr << "DragonBoard Rml Preview initialization failed.\n";
        MessageBoxW(
            nullptr,
            L"DragonBoard RML Editor could not start.\n\n"
            L"Verify that the project Assets/ui/rml folder is still available.",
            L"DragonBoard RML Editor",
            MB_OK | MB_ICONERROR);
        g_app = nullptr;
        return 1;
    }
    const int result = app.Run();
    g_app = nullptr;
    return result;
}
