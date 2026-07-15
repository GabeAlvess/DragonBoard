#include "RmlVisualInspector.h"

#include <RmlUi/Core/Element.h>

#include <CommCtrl.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <sstream>

namespace dragonboard::tools
{
    namespace
    {
        struct PropertyField
        {
            const char* name;
            const wchar_t* label;
        };

        constexpr std::array<PropertyField, 12> kProperties{
            PropertyField{ "color", L"Text color" },
            PropertyField{ "background-color", L"Background" },
            PropertyField{ "font-size", L"Font size" },
            PropertyField{ "width", L"Width" },
            PropertyField{ "height", L"Height" },
            PropertyField{ "margin", L"Margin" },
            PropertyField{ "padding", L"Padding" },
            PropertyField{ "opacity", L"Opacity" },
            PropertyField{ "border-color", L"Border color" },
            PropertyField{ "border-width", L"Border width" },
            PropertyField{ "display", L"Display" },
            PropertyField{ "text-align", L"Text align" }
        };

        std::string Trim(std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }
    }

    bool RmlVisualInspector::Create(HINSTANCE instance, HWND owner, StatusCallback statusCallback)
    {
        _owner = owner;
        _statusCallback = std::move(statusCallback);

        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_TREEVIEW_CLASSES };
        InitCommonControlsEx(&controls);

        const wchar_t* className = L"DragonBoardRmlVisualInspector";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &RmlVisualInspector::WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = className;
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        _window = CreateWindowExW(
            WS_EX_TOOLWINDOW, className, L"RmlUi Visual Inspector",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, 460, 900,
            owner, nullptr, instance, this);
        if (!_window) return false;
        CreateControls();
        LayoutControls();
        SetTimer(_window, 1, 100, nullptr);
        return true;
    }

    void RmlVisualInspector::Destroy()
    {
        _document = nullptr;
        _selected = nullptr;
        _treeItems.clear();
        if (_window) {
            DestroyWindow(_window);
            _window = nullptr;
        }
    }

    void RmlVisualInspector::Show(bool visible)
    {
        if (_window) ShowWindow(_window, visible ? SW_SHOW : SW_HIDE);
    }

    void RmlVisualInspector::Toggle()
    {
        Show(!IsVisible());
    }

    void RmlVisualInspector::Update()
    {
        SyncTreeSelection();
    }

    bool RmlVisualInspector::IsVisible() const
    {
        return _window && IsWindowVisible(_window);
    }

    void RmlVisualInspector::SetDocument(
        Rml::ElementDocument* document, const std::filesystem::path& path)
    {
        const bool changed = _documentPath != path;
        _document = document;
        _documentPath = path;
        _selected = nullptr;
        if (changed) {
            _overrides.clear();
            _undo.clear();
            _redo.clear();
            LoadOverrides();
        }
        ApplyOverrides({});
        RebuildTree();
        PopulatePropertyFields();
        UpdateButtons();
    }

    void RmlVisualInspector::SelectElement(Rml::Element* element)
    {
        if (!element || element == _document) return;
        _selected = element;
        if (const auto found = _treeItems.find(element); found != _treeItems.end()) {
            TreeView_SelectItem(_tree, found->second);
        }
        PopulatePropertyFields();
    }

    LRESULT CALLBACK RmlVisualInspector::WindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<RmlVisualInspector*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<RmlVisualInspector*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->HandleMessage(window, message, wParam, lParam) :
            DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT RmlVisualInspector::HandleMessage(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message) {
        case WM_CLOSE:
            Show(false);
            return 0;
        case WM_SIZE:
            LayoutControls();
            return 0;
        case WM_TIMER:
            if (wParam == 1) SyncTreeSelection();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) >= kPropertyBaseId &&
                LOWORD(wParam) < kPropertyBaseId + kProperties.size() &&
                HIWORD(wParam) == EN_CHANGE && !_populatingFields) {
                _dirtyProperties.insert(LOWORD(wParam) - kPropertyBaseId);
                UpdateButtons();
                return 0;
            }
            switch (LOWORD(wParam)) {
            case kApplyId: ApplyFields(); return 0;
            case kSaveId: SaveOverrides(); return 0;
            case kUndoId: Undo(); return 0;
            case kRedoId: Redo(); return 0;
            default: break;
            }
            break;
        case WM_NOTIFY: {
            const auto* notification = reinterpret_cast<NMHDR*>(lParam);
            if (notification->idFrom == kTreeId &&
                (notification->code == TVN_SELCHANGEDW || notification->code == TVN_SELCHANGEDA)) {
                SyncTreeSelection();
                return 0;
            }
            break;
        }
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void RmlVisualInspector::CreateControls()
    {
        const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(_window, GWLP_HINSTANCE));
        _tree = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
            WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTreeId)), instance, nullptr);
        TreeView_SetUnicodeFormat(_tree, TRUE);

        _selectorLabel = CreateWindowExW(0, L"STATIC", L"Selector", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window, nullptr, instance, nullptr);
        _selector = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"No element selected",
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSelectorId)), instance, nullptr);

        for (std::size_t index = 0; index < kProperties.size(); ++index) {
            _propertyLabels.push_back(CreateWindowExW(
                0, L"STATIC", kProperties[index].label, WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, _window, nullptr, instance, nullptr));
            _propertyFields.push_back(CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                0, 0, 0, 0, _window,
                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kPropertyBaseId + index)), instance, nullptr));
        }

        _applyButton = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kApplyId)), instance, nullptr);
        _saveButton = CreateWindowExW(0, L"BUTTON", L"Save RCSS", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSaveId)), instance, nullptr);
        _undoButton = CreateWindowExW(0, L"BUTTON", L"Undo", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kUndoId)), instance, nullptr);
        _redoButton = CreateWindowExW(0, L"BUTTON", L"Redo", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kRedoId)), instance, nullptr);
    }

    void RmlVisualInspector::LayoutControls()
    {
        if (!_window || !_tree) return;
        RECT client{};
        GetClientRect(_window, &client);
        const int width = client.right - client.left;
        const int margin = 10;
        int y = margin;
        MoveWindow(_tree, margin, y, width - 2 * margin, 260, TRUE);
        y += 270;

        MoveWindow(_selectorLabel, margin, y + 4, 105, 22, TRUE);
        MoveWindow(_selector, 120, y, width - 130, 25, TRUE);
        y += 34;

        for (std::size_t index = 0; index < kProperties.size(); ++index) {
            MoveWindow(_propertyLabels[index], margin, y + 4, 105, 22, TRUE);
            MoveWindow(_propertyFields[index], 120, y, width - 130, 25, TRUE);
            y += 31;
        }

        const int buttonWidth = (width - 5 * margin) / 4;
        MoveWindow(_applyButton, margin, y, buttonWidth, 30, TRUE);
        MoveWindow(_saveButton, 2 * margin + buttonWidth, y, buttonWidth, 30, TRUE);
        MoveWindow(_undoButton, 3 * margin + 2 * buttonWidth, y, buttonWidth, 30, TRUE);
        MoveWindow(_redoButton, 4 * margin + 3 * buttonWidth, y, buttonWidth, 30, TRUE);
    }

    void RmlVisualInspector::RebuildTree()
    {
        TreeView_DeleteAllItems(_tree);
        _treeItems.clear();
        _lastTreeSelection = nullptr;
        if (_document) AddTreeElement(_document, TVI_ROOT);
    }

    void RmlVisualInspector::SyncTreeSelection()
    {
        const auto item = TreeView_GetSelection(_tree);
        if (!item) return;
        if (item == _lastTreeSelection && _selected) return;
        _lastTreeSelection = item;
        for (const auto& [element, treeItem] : _treeItems) {
            if (treeItem == item) {
                _selected = element;
                PopulatePropertyFields();
                if (_statusCallback) {
                    _statusCallback("Selected " + BuildSelector(element));
                }
                break;
            }
        }
    }

    void RmlVisualInspector::AddTreeElement(Rml::Element* element, HTREEITEM parent)
    {
        std::string name = element->GetTagName();
        if (!element->GetId().empty()) name += "#" + element->GetId();
        if (!element->GetClassNames().empty()) name += "." + element->GetClassNames();
        auto wideName = ToWide(name);

        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.itemex.mask = TVIF_TEXT | TVIF_PARAM;
        insert.itemex.pszText = wideName.data();
        insert.itemex.lParam = reinterpret_cast<LPARAM>(element);
        const auto item = reinterpret_cast<HTREEITEM>(
            SendMessageW(_tree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
        _treeItems[element] = item;

        for (int index = 0; index < element->GetNumChildren(); ++index) {
            AddTreeElement(element->GetChild(index), item);
        }
        if (parent == TVI_ROOT) TreeView_Expand(_tree, item, TVE_EXPAND);
    }

    void RmlVisualInspector::PopulatePropertyFields()
    {
        _populatingFields = true;
        if (!_selected) {
            SetWindowTextW(_selector, L"No element selected");
            for (auto field : _propertyFields) SetWindowTextW(field, L"");
            _dirtyProperties.clear();
            _populatingFields = false;
            UpdateButtons();
            return;
        }

        const auto selector = BuildSelector(_selected);
        SetWindowTextW(_selector, ToWide(selector).c_str());
        for (std::size_t index = 0; index < kProperties.size(); ++index) {
            std::string value;
            if (const auto* property = _selected->GetProperty(kProperties[index].name)) {
                value = property->ToString();
            }
            SetWindowTextW(_propertyFields[index], ToWide(value).c_str());
        }
        _dirtyProperties.clear();
        _populatingFields = false;
        UpdateButtons();
    }

    void RmlVisualInspector::ApplyFields()
    {
        if (!_selected) return;
        const auto selector = BuildSelector(_selected);
        if (selector.empty()) {
            if (_statusCallback) _statusCallback("Element needs an id, class or tag selector");
            return;
        }

        const Overrides previous = _overrides;
        auto& properties = _overrides[selector];
        if (_dirtyProperties.empty()) return;
        for (const std::size_t index : _dirtyProperties) {
            const auto value = Trim(ToUtf8(GetWindowString(_propertyFields[index])));
            if (value.empty()) {
                properties.erase(kProperties[index].name);
                _selected->RemoveProperty(kProperties[index].name);
            } else {
                properties[kProperties[index].name] = value;
                _selected->SetProperty(kProperties[index].name, value);
            }
        }
        if (properties.empty()) _overrides.erase(selector);
        _undo.push_back(previous);
        _redo.clear();
        _dirtyProperties.clear();
        UpdateButtons();
        if (_statusCallback) _statusCallback("Applied visual properties to " + selector);
    }

    void RmlVisualInspector::ApplyOverrides(const Overrides& previous)
    {
        if (!_document) return;
        std::set<std::pair<std::string, std::string>> oldProperties;
        for (const auto& [selector, properties] : previous) {
            for (const auto& [name, value] : properties) oldProperties.emplace(selector, name);
        }
        for (const auto& [selector, properties] : _overrides) {
            for (const auto& [name, value] : properties) oldProperties.erase({ selector, name });
        }
        for (const auto& [selector, name] : oldProperties) {
            Rml::ElementList elements;
            _document->QuerySelectorAll(elements, selector);
            for (auto* element : elements) element->RemoveProperty(name);
        }
        for (const auto& [selector, properties] : _overrides) {
            Rml::ElementList elements;
            _document->QuerySelectorAll(elements, selector);
            for (auto* element : elements) {
                for (const auto& [name, value] : properties) element->SetProperty(name, value);
            }
        }
    }

    void RmlVisualInspector::SaveOverrides()
    {
        if (_documentPath.empty()) return;
        std::ofstream stream(OverridePath(), std::ios::binary | std::ios::trunc);
        if (!stream) {
            if (_statusCallback) _statusCallback("Could not save RCSS overrides");
            return;
        }
        stream << "/* Generated by DragonBoard RmlUi Visual Inspector. */\n\n";
        for (const auto& [selector, properties] : _overrides) {
            stream << selector << " {\n";
            for (const auto& [name, value] : properties) {
                stream << "    " << name << ": " << value << ";\n";
            }
            stream << "}\n\n";
        }
        if (_statusCallback) _statusCallback("Saved " + OverridePath().filename().string());
    }

    void RmlVisualInspector::LoadOverrides()
    {
        std::ifstream stream(OverridePath());
        if (!stream) return;
        std::string selector;
        std::string line;
        while (std::getline(stream, line)) {
            line = Trim(line);
            if (line.empty() || line.starts_with("/*") || line.starts_with("*")) continue;
            if (line.ends_with("{")) {
                selector = Trim(line.substr(0, line.size() - 1));
            } else if (line == "}") {
                selector.clear();
            } else if (!selector.empty()) {
                const auto colon = line.find(':');
                if (colon == std::string::npos) continue;
                auto name = Trim(line.substr(0, colon));
                auto value = Trim(line.substr(colon + 1));
                if (value.ends_with(";")) value.pop_back();
                _overrides[selector][name] = Trim(value);
            }
        }
    }

    void RmlVisualInspector::Undo()
    {
        if (_undo.empty()) return;
        const Overrides previous = _overrides;
        _redo.push_back(_overrides);
        _overrides = std::move(_undo.back());
        _undo.pop_back();
        ApplyOverrides(previous);
        PopulatePropertyFields();
        UpdateButtons();
        if (_statusCallback) _statusCallback("Undo");
    }

    void RmlVisualInspector::Redo()
    {
        if (_redo.empty()) return;
        const Overrides previous = _overrides;
        _undo.push_back(_overrides);
        _overrides = std::move(_redo.back());
        _redo.pop_back();
        ApplyOverrides(previous);
        PopulatePropertyFields();
        UpdateButtons();
        if (_statusCallback) _statusCallback("Redo");
    }

    void RmlVisualInspector::UpdateButtons()
    {
        EnableWindow(_applyButton, _selected != nullptr && !_dirtyProperties.empty());
        EnableWindow(_saveButton, !_documentPath.empty());
        EnableWindow(_undoButton, !_undo.empty());
        EnableWindow(_redoButton, !_redo.empty());
    }

    std::string RmlVisualInspector::BuildSelector(Rml::Element* element) const
    {
        if (!element) return {};
        if (!element->GetId().empty()) return "#" + element->GetId();
        const auto classes = element->GetClassNames();
        if (!classes.empty()) {
            const auto separator = classes.find(' ');
            return "." + classes.substr(0, separator);
        }
        return element->GetTagName();
    }

    Rml::Element* RmlVisualInspector::FindFirst(const std::string& selector) const
    {
        return _document && !selector.empty() ? _document->QuerySelector(selector) : nullptr;
    }

    std::filesystem::path RmlVisualInspector::OverridePath() const
    {
        auto path = _documentPath;
        path.replace_extension(".editor-overrides.rcss");
        return path;
    }

    std::wstring RmlVisualInspector::ToWide(std::string_view value)
    {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    std::string RmlVisualInspector::ToUtf8(std::wstring_view value)
    {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring RmlVisualInspector::GetWindowString(HWND window)
    {
        const int size = GetWindowTextLengthW(window);
        std::wstring value(size + 1, L'\0');
        GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
        value.resize(size);
        return value;
    }
}
