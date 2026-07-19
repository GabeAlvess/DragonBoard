#include "ui/rml/DragonBoardRmlRenderer.h"
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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;
    using dragonboard::ui::rml::DragonBoardRmlRenderer;
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

    constexpr std::array<const char*, 5> kSettingsPages{
        "general", "position", "visuals", "items", "labels"
    };
    constexpr std::array<const char*, 2> kDeveloperPages{
        "commands", "info"
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
        bool Initialize(HINSTANCE instance, const std::optional<std::filesystem::path>& requestedDocument)
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

                _inspector.Update();
                CheckHotReload();
                if (!_context->Update()) {
                    std::cerr << "RmlUi context update failed.\n";
                    break;
                }
                if (!_renderer.BeginFrame(_renderTarget.Get(), kCanvasWidth, kCanvasHeight)) break;
                const bool rendered = _context->Render();
                _renderer.EndFrame();
                if (!rendered) break;
                _swapChain->Present(1, 0);
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
                    _context->ProcessMouseWheel(Rml::Vector2f(0.0f, -steps), GetModifiers());
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
                    return true;
                }
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

            auto* nextDocument = _context->LoadDocument(path.string());
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
                auto* nextDocument = _context->LoadDocumentFromMemory(source, path.string());
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
            } else if (fileName == "edit.rml") {
                PopulateItemEditDocument();
                SelectPage(kItemEditPages, "edit-tab-", "edit-page-", "position");
            } else if (fileName == "mods.rml") {
                if (auto* list = _document->GetElementById("mods-list")) {
                    list->SetInnerRML(
                        "<div id=\"mods-card-0\" class=\"mod-card\" tabindex=\"0\"><span class=\"mod-card-mark\">&lt;&gt;</span><span class=\"mod-card-label\">Campfire</span></div>"
                        "<div id=\"mods-card-1\" class=\"mod-card\" tabindex=\"0\"><span class=\"mod-card-mark\">&lt;&gt;</span><span class=\"mod-card-label\">Whirlwind Sprint</span></div>"
                        "<div id=\"mods-card-2\" class=\"mod-card\" tabindex=\"0\"><span class=\"mod-card-mark\">&lt;&gt;</span><span class=\"mod-card-label\">Travel Lantern</span></div>");
                }
            } else if (fileName == "inventory.rml") {
                PopulateInventoryDocument();
            } else if (fileName == "magic.rml") {
                PopulateMagicDocument();
            }
        }

        void PopulateItemEditDocument()
        {
            SetText("edit-item-name", "Ebony Sword");
            SetText("edit-category", "Weapons");
            SetText("edit-form-id", "000139B1");
            SetText("edit-model-path", "Weapons/Ebony/EbonySword.nif");
            SetText("edit-label-state", "Visible");
        }

        void PopulateDeveloperDocument()
        {
            auto* list = _document->GetElementById("dev-command-list");
            if (!list) return;
            std::string markup;
            for (std::size_t index = 0; index < kMockCommands.size(); ++index) {
                markup += "<button id=\"dev-command-" + std::to_string(index) +
                    "\" class=\"command-item\">" + kMockCommands[index].label + "</button><br />";
            }
            list->SetInnerRML(markup);
            SelectMockCommand(0);

            SetText("dev-fps", "90.0");
            SetText("dev-frame-time", "11.11 ms");
            SetText("dev-present-timing", "11.11 / 11.12 / 11.30 / 11.52 ms");
            SetText("dev-update-timing", "0.18 / 0.20 / 0.28 / 0.35 ms");
            SetText("dev-begin-timing", "0.31 / 0.34 / 0.48 / 0.57 ms");
            SetText("dev-render-timing", "1.62 / 1.71 / 2.10 / 2.44 ms");
            SetText("dev-end-timing", "0.22 / 0.25 / 0.36 / 0.43 ms");
            SetText("dev-dx11-timing", "0.53 / 0.59 / 0.84 / 1.00 ms");
            SetText("dev-total-timing", "2.33 / 2.50 / 3.22 / 3.79 ms");
            SetText("dev-draw-calls", "24");
            SetText("dev-dom-elements", "930");
            SetText("dev-renders-per-second", "90.0");
            SetText("dev-cached-frames", "0");
            SetText("dev-texture-size", "1920 x 1080");
            SetText("dev-active-document", "Developer");
            SetText("dev-dirty-reason", "Pointer");
            SetText("dev-helper", "Connected");
            SetText("dev-version", "0.8 preview");
            SetText("dev-feature-level", "0xB000");
            SetText("dev-player-position", "X 1240.5   Y -832.0   Z 96.2");
            SetText("dev-cell", "Whiterun");
            SetText("dev-cell-form", "00018A56");
            SetText("dev-worldspace", "Tamriel");
            SetText("dev-worldspace-form", "0000003C");
        }

        void PopulateInventoryDocument()
        {
            if (auto* list = _document->GetElementById("inventory-item-list")) {
                list->SetInnerRML(
                    "<button id=\"inventory-item-0\" class=\"inventory-list-item equipped\"><span class=\"item-state-mark\">[L]</span><span id=\"inventory-item-name-0\" class=\"item-name\"><span id=\"inventory-item-name-track-0\" class=\"item-name-track\">Ebony Sword</span></span><span class=\"item-stack\"></span></button>"
                    "<button id=\"inventory-item-1\" class=\"inventory-list-item active equipped favorited\"><span class=\"item-state-mark\">[R]</span><span id=\"inventory-item-name-1\" class=\"item-name\"><span id=\"inventory-item-name-track-1\" class=\"item-name-track\">Nordic Bow of the Ancient Dragonborn Champion</span></span><span class=\"item-stack\"></span></button>"
                    "<button id=\"inventory-item-2\" class=\"inventory-list-item favorited\"><span class=\"item-state-mark\"></span><span id=\"inventory-item-name-2\" class=\"item-name\"><span id=\"inventory-item-name-track-2\" class=\"item-name-track\">Potion of Ultimate Healing</span></span><span class=\"item-stack\">x4</span></button>"
                    "<button id=\"inventory-item-3\" class=\"inventory-list-item\"><span class=\"item-state-mark\"></span><span id=\"inventory-item-name-3\" class=\"item-name\"><span id=\"inventory-item-name-track-3\" class=\"item-name-track\">Dragonscale Armor</span></span><span class=\"item-stack\"></span></button>"
                    "<button id=\"inventory-item-4\" class=\"inventory-list-item\"><span class=\"item-state-mark\"></span><span id=\"inventory-item-name-4\" class=\"item-name\"><span id=\"inventory-item-name-track-4\" class=\"item-name-track\">Black Soul Gem</span></span><span class=\"item-stack\">x2</span></button>");
            }
            if (auto* filter = _document->GetElementById("inventory-filter-weapons")) {
                filter->SetClass("active", true);
            }
            SetText("inventory-player-name", "Arthas");
            SetText("inventory-player-level", "42");
            SetText("inventory-gold", "12.840");
            SetText("inventory-carry-weight", "218.5 / 420");
            SetText("inventory-item-count", "47");
            SetText("inventory-selected-category", "WEAPON");
            SetText(
                "inventory-selected-name",
                "Nordic Bow of the Ancient Dragonborn Champion");
            if (auto* left = _document->GetElementById("inventory-left-hand-state")) {
                left->SetClass("active", false);
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
            if (auto* list = _document->GetElementById("magic-spell-list")) {
                list->SetInnerRML(
                    "<button id=\"magic-spell-0\" class=\"magic-list-item equipped\"><span class=\"spell-state-mark\">[L]</span><span id=\"magic-spell-name-0\" class=\"spell-name\"><span id=\"magic-spell-name-track-0\" class=\"spell-name-track\">Flames</span></span></button>"
                    "<button id=\"magic-spell-1\" class=\"magic-list-item active equipped favorited\"><span class=\"spell-state-mark\">[R]</span><span id=\"magic-spell-name-1\" class=\"spell-name\"><span id=\"magic-spell-name-track-1\" class=\"spell-name-track\">Conjure Ancient Dragon Priest Guardian</span></span></button>"
                    "<button id=\"magic-spell-2\" class=\"magic-list-item favorited\"><span class=\"spell-state-mark\"></span><span id=\"magic-spell-name-2\" class=\"spell-name\"><span id=\"magic-spell-name-track-2\" class=\"spell-name-track\">Fast Healing</span></span></button>"
                    "<button id=\"magic-spell-3\" class=\"magic-list-item\"><span class=\"spell-state-mark\"></span><span id=\"magic-spell-name-3\" class=\"spell-name\"><span id=\"magic-spell-name-track-3\" class=\"spell-name-track\">Oakflesh</span></span></button>"
                    "<button id=\"magic-spell-4\" class=\"magic-list-item\"><span class=\"spell-state-mark\"></span><span id=\"magic-spell-name-4\" class=\"spell-name\"><span id=\"magic-spell-name-track-4\" class=\"spell-name-track\">Clairvoyance</span></span></button>");
            }
            if (auto* filter = _document->GetElementById("magic-filter-conjuration")) {
                filter->SetClass("active", true);
            }
            SetText("magic-player-name", "Arthas");
            SetText("magic-player-level", "42");
            SetText("magic-spell-count", "18");
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
                element->SetInnerRML(std::string(value));
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
            const std::string document = _documentPath.empty() ?
                "No document" : _documentPath.filename().string();
            const std::string title = "DragonBoard Rml Preview - " + document + " - " + _status +
                " | Ctrl+O Open  F5 Reload  F9 Inspector  F10 Editor";
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
        std::filesystem::path _assetsDirectory;
        std::filesystem::path _documentPath;
        std::vector<std::filesystem::path> _documentFiles;
        std::filesystem::file_time_type _lastObservedWrite{};
        std::chrono::steady_clock::time_point _lastReloadCheck{};
        std::size_t _selectedMockCommand = 0;
        std::string _status;
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
    if (argumentCount > 1) requestedDocument = std::filesystem::path(arguments[1]);

    PreviewApp app;
    g_app = &app;
    if (!app.Initialize(GetModuleHandleW(nullptr), requestedDocument)) {
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
