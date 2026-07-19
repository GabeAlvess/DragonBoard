#include "RmlSourceEditor.h"

#include <RmlUi/Core/Element.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <regex>
#include <richedit.h>

namespace dragonboard::tools
{
    bool RmlSourceEditor::Create(
        HINSTANCE instance,
        HWND owner,
        SavedCallback savedCallback,
        PreviewCallback previewCallback,
        UndoCallback undoCallback,
        StatusCallback statusCallback)
    {
        _owner = owner;
        _savedCallback = std::move(savedCallback);
        _previewCallback = std::move(previewCallback);
        _undoCallback = std::move(undoCallback);
        _statusCallback = std::move(statusCallback);
        _richEditLibrary = LoadLibraryW(L"Msftedit.dll");
        if (!_richEditLibrary) return false;
        _backgroundBrush = CreateSolidBrush(RGB(24, 26, 31));
        _controlBrush = CreateSolidBrush(RGB(31, 34, 41));

        const wchar_t* className = L"DragonBoardRmlSourceEditor";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &RmlSourceEditor::WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = _backgroundBrush;
        windowClass.lpszClassName = className;
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        _window = CreateWindowExW(
            WS_EX_TOOLWINDOW, className, L"DragonBoard RML Editor",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            30, 30, 900, 1000,
            owner, nullptr, instance, this);
        if (!_window) return false;

        CreateControls();
        LayoutControls();
        SetTimer(_window, kPreviewTimerId, 100, nullptr);
        return true;
    }

    void RmlSourceEditor::Destroy()
    {
        _savedCallback = {};
        if (_window) {
            DestroyWindow(_window);
            _window = nullptr;
        }
        if (_editorFont) {
            DeleteObject(_editorFont);
            _editorFont = nullptr;
        }
        if (_controlBrush) {
            DeleteObject(_controlBrush);
            _controlBrush = nullptr;
        }
        if (_backgroundBrush) {
            DeleteObject(_backgroundBrush);
            _backgroundBrush = nullptr;
        }
        if (_richEditLibrary) {
            FreeLibrary(_richEditLibrary);
            _richEditLibrary = nullptr;
        }
    }

    void RmlSourceEditor::Show(bool visible)
    {
        if (_window) ShowWindow(_window, visible ? SW_SHOW : SW_HIDE);
    }

    void RmlSourceEditor::Toggle()
    {
        Show(!IsVisible());
    }

    bool RmlSourceEditor::IsVisible() const
    {
        return _window && IsWindowVisible(_window);
    }

    bool RmlSourceEditor::HandleShortcut(const MSG& message)
    {
        if (message.message != WM_KEYDOWN || message.wParam != 'Z' ||
            (GetKeyState(VK_CONTROL) & 0x8000) == 0) {
            return false;
        }

        const auto root = GetAncestor(message.hwnd, GA_ROOT);
        if (root != _window && root != _owner) return false;

        if (message.hwnd == _editor && SendMessageW(_editor, EM_CANUNDO, 0, 0)) {
            SendMessageW(_editor, EM_UNDO, 0, 0);
            MarkDirty();
        } else if (_undoCallback) {
            _undoCallback();
        }
        return true;
    }

    void RmlSourceEditor::SetDocument(const std::filesystem::path& documentPath)
    {
        if (documentPath.empty()) return;
        if (_dirty && !SamePath(_activePath, documentPath)) return;

        const auto previousActive = _activePath;
        _documentPath = documentPath;
        RefreshFiles();

        auto desired = documentPath;
        if (!previousActive.empty() &&
            SamePath(previousActive.parent_path(), documentPath.parent_path()) &&
            std::filesystem::is_regular_file(previousActive)) {
            desired = previousActive;
        }
        SelectPath(desired);
        LoadSelectedFile();
    }

    void RmlSourceEditor::SelectElement(const Rml::Element* element)
    {
        if (!element || _documentPath.empty()) return;
        if (_dirty && !SamePath(_activePath, _documentPath)) return;
        if (!SamePath(_activePath, _documentPath)) {
            SelectPath(_documentPath);
            if (!LoadFile(_documentPath)) return;
        }

        const auto source = GetWindowString(_editor);
        std::wsmatch match;
        std::wstring description;
        std::wregex expression;
        bool hasExpression = false;

        if (!element->GetId().empty()) {
            const auto id = ToWide(element->GetId());
            expression = std::wregex(
                L"id\\s*=\\s*([\\\"'])" + EscapeRegex(id) + L"\\1",
                std::regex_constants::icase);
            description = L"#" + id;
            hasExpression = true;
        } else if (!element->GetClassNames().empty()) {
            auto className = element->GetClassNames();
            if (const auto separator = className.find(' '); separator != std::string::npos) {
                className.resize(separator);
            }
            const auto wideClass = ToWide(className);
            expression = std::wregex(
                L"class\\s*=\\s*([\\\"'])[^\\\"']*\\b" + EscapeRegex(wideClass) +
                    L"\\b[^\\\"']*\\1",
                std::regex_constants::icase);
            description = L"." + wideClass;
            hasExpression = true;
        } else if (!element->GetTagName().empty() && !element->GetTagName().starts_with("#")) {
            const auto tag = ToWide(element->GetTagName());
            expression = std::wregex(L"<\\s*" + EscapeRegex(tag) + L"\\b", std::regex_constants::icase);
            description = L"<" + tag + L">";
            hasExpression = true;
        }

        if (!hasExpression || !std::regex_search(source, match, expression)) {
            if (_statusCallback) _statusCallback("Source location was not found for clicked element");
            return;
        }
        GoToSourcePosition(static_cast<std::size_t>(match.position()), ToUtf8(description));
    }

    void RmlSourceEditor::OpenFileAtText(
        const std::filesystem::path& path,
        std::string_view text,
        std::string_view description)
    {
        if (_dirty && !SamePath(_activePath, path)) {
            if (_statusCallback) _statusCallback("Save or Reset the current file before switching");
            return;
        }
        RefreshFiles();
        SelectPath(path);
        if (!LoadFile(path)) return;
        const auto source = GetWindowString(_editor);
        const auto position = source.find(ToWide(text));
        if (position != std::wstring::npos) GoToSourcePosition(position, description);
    }

    void RmlSourceEditor::SetGeneratedContent(
        const std::filesystem::path& path,
        std::string content,
        std::string_view selector)
    {
        if (_dirty && !SamePath(_activePath, path)) {
            if (_statusCallback) _statusCallback("Save or Reset the current file before moving another element");
            return;
        }
        RefreshFiles();
        if (std::none_of(_files.begin(), _files.end(), [&](const auto& item) { return SamePath(item, path); })) {
            _files.push_back(path);
            SendMessageW(_fileList, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(path.filename().wstring().c_str()));
        }
        _activePath = path;
        SelectPath(path);
        _loading = true;
        const auto wide = ToWide(content);
        SetWindowTextW(_editor, wide.c_str());
        ApplySyntaxHighlighting();
        _loading = false;
        _dirty = true;
        UpdateTitle();
        const auto position = wide.find(ToWide(selector));
        if (position != std::wstring::npos) GoToSourcePosition(position, selector);
        EmitPreview();
    }

    bool RmlSourceEditor::IsMoveModeEnabled() const
    {
        return _moveModeEnabled;
    }

    LRESULT CALLBACK RmlSourceEditor::WindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<RmlSourceEditor*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<RmlSourceEditor*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->HandleMessage(window, message, wParam, lParam) :
            DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT RmlSourceEditor::HandleMessage(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message) {
        case WM_CLOSE:
            Show(false);
            return 0;
        case WM_SIZE:
            LayoutControls();
            return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(220, 224, 232));
            SetBkColor(dc, RGB(31, 34, 41));
            return reinterpret_cast<LRESULT>(_controlBrush);
        }
        case WM_TIMER:
            if (wParam == kPreviewTimerId && _dirty &&
                std::chrono::steady_clock::now() - _lastEdit >= std::chrono::milliseconds(350)) {
                ApplySyntaxHighlighting();
                EmitPreview();
                _lastEdit = std::chrono::steady_clock::now() + std::chrono::hours(24);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kEditorId && HIWORD(wParam) == EN_CHANGE && !_loading && !_formatting) {
                MarkDirty();
                return 0;
            }
            if (LOWORD(wParam) == kFileListId && HIWORD(wParam) == LBN_SELCHANGE) {
                if (_dirty) {
                    SelectPath(_activePath);
                    if (_statusCallback) _statusCallback("Save or Reset before switching files");
                } else {
                    LoadSelectedFile();
                }
                return 0;
            }
            if (LOWORD(wParam) == kSaveId) {
                SaveNow();
                return 0;
            }
            if (LOWORD(wParam) == kResetId) {
                ResetFromDisk();
                return 0;
            }
            if (LOWORD(wParam) == kMoveModeId) {
                _moveModeEnabled = !_moveModeEnabled;
                SetWindowTextW(_moveModeButton, _moveModeEnabled ? L"Move mode: ON" : L"Move mode: OFF");
                if (_statusCallback) {
                    _statusCallback(_moveModeEnabled ?
                        "Move mode enabled - drag elements in the preview" :
                        "Move mode disabled - preview controls are interactive");
                }
                return 0;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void RmlSourceEditor::CreateControls()
    {
        const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(_window, GWLP_HINSTANCE));
        _fileList = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFileListId)), instance, nullptr);
        _editor = CreateWindowExW(
            WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL |
                ES_SAVESEL,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kEditorId)), instance, nullptr);
        SendMessageW(_editor, EM_SETLIMITTEXT, 16 * 1024 * 1024, 0);
        SendMessageW(_editor, EM_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(std::array<int, 1>{ 16 }.data()));
        SendMessageW(_editor, EM_SETBKGNDCOLOR, 0, RGB(31, 34, 41));
        SendMessageW(_editor, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(12, 12));
        SendMessageW(_editor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);

        _editorFont = CreateFontW(
            -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Consolas");
        if (_editorFont) SendMessageW(_editor, WM_SETFONT, reinterpret_cast<WPARAM>(_editorFont), TRUE);

        _saveButton = CreateWindowExW(
            0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kSaveId)), instance, nullptr);
        _reloadButton = CreateWindowExW(
            0, L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kResetId)), instance, nullptr);
        _moveModeButton = CreateWindowExW(
            0, L"BUTTON", L"Move mode: ON", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window,
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kMoveModeId)), instance, nullptr);
        _hint = CreateWindowExW(
            0, L"STATIC", L"Live preview - disk changes only on Save", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, _window, nullptr, instance, nullptr);
    }

    void RmlSourceEditor::LayoutControls()
    {
        if (!_window || !_editor) return;
        RECT client{};
        GetClientRect(_window, &client);
        const int width = std::max(1L, client.right - client.left);
        const int height = std::max(1L, client.bottom - client.top);
        constexpr int margin = 10;
        constexpr int fileWidth = 180;
        constexpr int bottomHeight = 34;
        MoveWindow(_fileList, margin, margin, fileWidth, height - 3 * margin - bottomHeight, TRUE);
        MoveWindow(
            _editor, 2 * margin + fileWidth, margin,
            width - 3 * margin - fileWidth, height - 3 * margin - bottomHeight, TRUE);
        const int bottom = height - margin - bottomHeight;
        MoveWindow(_saveButton, margin, bottom, 150, bottomHeight, TRUE);
        MoveWindow(_reloadButton, margin + 160, bottom, 150, bottomHeight, TRUE);
        MoveWindow(_moveModeButton, margin + 320, bottom, 145, bottomHeight, TRUE);
        MoveWindow(_hint, margin + 480, bottom + 8, width - margin - 480, 22, TRUE);
    }

    void RmlSourceEditor::RefreshFiles()
    {
        _files.clear();
        SendMessageW(_fileList, LB_RESETCONTENT, 0, 0);
        if (_documentPath.empty()) return;

        _files.push_back(_documentPath);
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(_documentPath.parent_path(), error)) {
            if (error || !entry.is_regular_file() || entry.path().extension() != ".rcss") continue;
            _files.push_back(entry.path());
        }
        std::sort(_files.begin() + 1, _files.end());
        for (const auto& path : _files) {
            const auto name = path.filename().wstring();
            SendMessageW(_fileList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
    }

    bool RmlSourceEditor::LoadSelectedFile()
    {
        const auto selection = SendMessageW(_fileList, LB_GETCURSEL, 0, 0);
        if (selection == LB_ERR || static_cast<std::size_t>(selection) >= _files.size()) return false;
        return LoadFile(_files[static_cast<std::size_t>(selection)]);
    }

    bool RmlSourceEditor::LoadFile(const std::filesystem::path& path)
    {
        if (path.empty()) return false;
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            if (_statusCallback) _statusCallback("Editor could not open " + path.filename().string());
            return false;
        }
        std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        _hadUtf8Bom = contents.starts_with("\xEF\xBB\xBF");
        if (_hadUtf8Bom) contents.erase(0, 3);
        _useCrLf = contents.find("\r\n") != std::string::npos;

        _loading = true;
        _activePath = path;
        const auto wide = ToWide(contents);
        SetWindowTextW(_editor, wide.c_str());
        ApplySyntaxHighlighting();
        _loading = false;
        _dirty = false;
        SelectPath(path);
        UpdateTitle();
        return true;
    }

    bool RmlSourceEditor::SaveNow()
    {
        if (!_dirty || _activePath.empty()) return true;
        std::string contents = ToUtf8(GetWindowString(_editor));
        if (!_useCrLf) {
            std::string normalized;
            normalized.reserve(contents.size());
            for (std::size_t index = 0; index < contents.size(); ++index) {
                if (contents[index] == '\r' && index + 1 < contents.size() && contents[index + 1] == '\n') continue;
                normalized.push_back(contents[index]);
            }
            contents = std::move(normalized);
        }

        std::ofstream stream(_activePath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            if (_statusCallback) _statusCallback("Editor could not save " + _activePath.filename().string());
            return false;
        }
        if (_hadUtf8Bom) stream.write("\xEF\xBB\xBF", 3);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.close();
        if (!stream) {
            if (_statusCallback) _statusCallback("Editor write failed for " + _activePath.filename().string());
            return false;
        }

        _dirty = false;
        UpdateTitle();
        if (_statusCallback) _statusCallback("Saved " + _activePath.filename().string() + " - preview updated");
        if (_savedCallback) _savedCallback(_activePath);
        return true;
    }

    void RmlSourceEditor::ResetFromDisk()
    {
        if (_activePath.empty()) return;
        if (std::filesystem::is_regular_file(_activePath)) {
            LoadFile(_activePath);
        } else {
            _loading = true;
            SetWindowTextW(_editor, L"");
            _loading = false;
            _dirty = false;
            UpdateTitle();
        }
        EmitPreview();
        if (_statusCallback) _statusCallback("Reset to the last saved version");
    }

    void RmlSourceEditor::EmitPreview()
    {
        if (_previewCallback && !_activePath.empty()) {
            _previewCallback(_activePath, ToUtf8(GetWindowString(_editor)));
        }
    }

    void RmlSourceEditor::ApplySyntaxHighlighting()
    {
        if (!_editor) return;
        const auto source = GetWindowString(_editor);
        CHARRANGE savedSelection{};
        SendMessageW(_editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&savedSelection));
        _formatting = true;
        SendMessageW(_editor, WM_SETREDRAW, FALSE, 0);

        const auto formatRange = [this](std::size_t start, std::size_t length, COLORREF color, bool bold = false) {
            CHARRANGE range{
                static_cast<LONG>(start),
                static_cast<LONG>(start + length)
            };
            SendMessageW(_editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
            CHARFORMAT2W format{};
            format.cbSize = sizeof(format);
            format.dwMask = CFM_COLOR | CFM_BOLD;
            format.crTextColor = color;
            format.dwEffects = bold ? CFE_BOLD : 0;
            SendMessageW(_editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        };

        formatRange(0, source.size(), RGB(220, 224, 232));
        const auto applyMatches = [&source, &formatRange](
            const std::wregex& expression, COLORREF color, bool bold = false, std::size_t group = 0) {
            for (auto iterator = std::wsregex_iterator(source.begin(), source.end(), expression);
                 iterator != std::wsregex_iterator(); ++iterator) {
                const auto& match = *iterator;
                if (group >= match.size() || !match[group].matched) continue;
                formatRange(
                    static_cast<std::size_t>(match.position(group)),
                    static_cast<std::size_t>(match.length(group)), color, bold);
            }
        };

        if (_activePath.extension() == ".rml") {
            applyMatches(std::wregex(L"</?\\s*([A-Za-z][A-Za-z0-9:_-]*)"), RGB(86, 182, 255), true, 1);
            applyMatches(std::wregex(L"\\s([A-Za-z_:][A-Za-z0-9:_.-]*)\\s*="), RGB(198, 146, 255), false, 1);
        } else {
            applyMatches(std::wregex(L"([;{]\\s*)([-A-Za-z][A-Za-z0-9_-]*)\\s*:"),
                RGB(86, 182, 255), false, 2);
            applyMatches(std::wregex(L"([^@{}][^{}]*)(?=\\{)"), RGB(198, 146, 255), true, 1);
        }
        applyMatches(std::wregex(L"\"[^\"\\r\\n]*\"|'[^'\\r\\n]*'"), RGB(255, 184, 108));
        applyMatches(std::wregex(L"<!--[\\s\\S]*?-->|/\\*[\\s\\S]*?\\*/"), RGB(105, 170, 110));

        SendMessageW(_editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&savedSelection));
        SendMessageW(_editor, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(_editor, nullptr, TRUE);
        _formatting = false;
    }

    void RmlSourceEditor::GoToSourcePosition(std::size_t position, std::string_view description)
    {
        const LONG character = static_cast<LONG>(position);
        const LONG line = static_cast<LONG>(SendMessageW(_editor, EM_LINEFROMCHAR, character, 0));
        const LONG lineStart = static_cast<LONG>(SendMessageW(_editor, EM_LINEINDEX, line, 0));
        const LONG lineLength = static_cast<LONG>(SendMessageW(_editor, EM_LINELENGTH, lineStart, 0));
        CHARRANGE range{ lineStart, lineStart + std::max(1L, lineLength) };
        SendMessageW(_editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        const LONG firstVisible = static_cast<LONG>(SendMessageW(_editor, EM_GETFIRSTVISIBLELINE, 0, 0));
        SendMessageW(_editor, EM_LINESCROLL, 0, line - firstVisible - 8);
        SendMessageW(_editor, EM_SCROLLCARET, 0, 0);
        SetFocus(_editor);
        Show(true);
        if (_statusCallback) {
            _statusCallback("Source " + std::string(description) + " at line " + std::to_string(line + 1));
        }
    }

    void RmlSourceEditor::SelectPath(const std::filesystem::path& path)
    {
        for (std::size_t index = 0; index < _files.size(); ++index) {
            if (SamePath(_files[index], path)) {
                SendMessageW(_fileList, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                return;
            }
        }
    }

    void RmlSourceEditor::MarkDirty()
    {
        _dirty = true;
        _lastEdit = std::chrono::steady_clock::now();
        UpdateTitle();
    }

    void RmlSourceEditor::UpdateTitle()
    {
        std::wstring title = L"DragonBoard RML Editor - ";
        title += _activePath.empty() ? L"No file" : _activePath.filename().wstring();
        if (_dirty) title += L" *";
        SetWindowTextW(_window, title.c_str());
        EnableWindow(_saveButton, _dirty);
        EnableWindow(_reloadButton, !_activePath.empty());
    }

    std::wstring RmlSourceEditor::ToWide(std::string_view value)
    {
        if (value.empty()) return {};
        int size = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        const UINT codePage = size > 0 ? CP_UTF8 : CP_ACP;
        const DWORD flags = size > 0 ? MB_ERR_INVALID_CHARS : 0;
        if (size <= 0) {
            size = MultiByteToWideChar(
                codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
        }
        std::wstring result(static_cast<std::size_t>(std::max(0, size)), L'\0');
        if (size > 0) {
            MultiByteToWideChar(
                codePage, flags, value.data(), static_cast<int>(value.size()), result.data(), size);
        }
        return result;
    }

    std::string RmlSourceEditor::ToUtf8(std::wstring_view value)
    {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring RmlSourceEditor::GetWindowString(HWND window)
    {
        const int size = GetWindowTextLengthW(window);
        std::wstring value(static_cast<std::size_t>(size) + 1, L'\0');
        GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
        value.resize(static_cast<std::size_t>(size));
        return value;
    }

    bool RmlSourceEditor::SamePath(
        const std::filesystem::path& left,
        const std::filesystem::path& right)
    {
        std::error_code error;
        const bool equivalent = std::filesystem::equivalent(left, right, error);
        return !error ? equivalent : left.lexically_normal() == right.lexically_normal();
    }

    std::wstring RmlSourceEditor::EscapeRegex(std::wstring_view value)
    {
        static const std::wregex special(L"([.^$|()\\[\\]{}*+?\\\\])");
        return std::regex_replace(std::wstring(value), special, L"\\$1");
    }
}
