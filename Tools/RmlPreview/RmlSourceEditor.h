#pragma once

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Rml
{
    class Element;
}

namespace dragonboard::tools
{
    class RmlSourceEditor
    {
    public:
        using SavedCallback = std::function<void(const std::filesystem::path&)>;
        using PreviewCallback = std::function<void(const std::filesystem::path&, std::string)>;
        using UndoCallback = std::function<bool()>;
        using StatusCallback = std::function<void(std::string)>;

        bool Create(
            HINSTANCE instance,
            HWND owner,
            SavedCallback savedCallback,
            PreviewCallback previewCallback,
            UndoCallback undoCallback,
            StatusCallback statusCallback);
        void Destroy();
        void Show(bool visible);
        void Toggle();
        [[nodiscard]] bool IsVisible() const;
        bool HandleShortcut(const MSG& message);

        void SetDocument(const std::filesystem::path& documentPath);
        void SelectElement(const Rml::Element* element);
        void OpenFileAtText(
            const std::filesystem::path& path,
            std::string_view text,
            std::string_view description);
        void SetGeneratedContent(
            const std::filesystem::path& path,
            std::string content,
            std::string_view selector);
        [[nodiscard]] bool IsMoveModeEnabled() const;

    private:
        static constexpr UINT kFileListId = 5100;
        static constexpr UINT kEditorId = 5101;
        static constexpr UINT kSaveId = 5102;
        static constexpr UINT kResetId = 5103;
        static constexpr UINT kMoveModeId = 5104;
        static constexpr UINT_PTR kPreviewTimerId = 1;

        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

        void CreateControls();
        void LayoutControls();
        void RefreshFiles();
        bool LoadSelectedFile();
        bool LoadFile(const std::filesystem::path& path);
        bool SaveNow();
        void ResetFromDisk();
        void EmitPreview();
        void ApplySyntaxHighlighting();
        void GoToSourcePosition(std::size_t position, std::string_view description);
        void SelectPath(const std::filesystem::path& path);
        void MarkDirty();
        void UpdateTitle();

        [[nodiscard]] static std::wstring ToWide(std::string_view value);
        [[nodiscard]] static std::string ToUtf8(std::wstring_view value);
        [[nodiscard]] static std::wstring GetWindowString(HWND window);
        [[nodiscard]] static bool SamePath(
            const std::filesystem::path& left,
            const std::filesystem::path& right);
        [[nodiscard]] static std::wstring EscapeRegex(std::wstring_view value);

        HWND _window = nullptr;
        HWND _owner = nullptr;
        HWND _fileList = nullptr;
        HWND _editor = nullptr;
        HWND _saveButton = nullptr;
        HWND _reloadButton = nullptr;
        HWND _moveModeButton = nullptr;
        HWND _hint = nullptr;
        HFONT _editorFont = nullptr;
        HBRUSH _backgroundBrush = nullptr;
        HBRUSH _controlBrush = nullptr;
        HMODULE _richEditLibrary = nullptr;
        std::filesystem::path _documentPath;
        std::filesystem::path _activePath;
        std::vector<std::filesystem::path> _files;
        SavedCallback _savedCallback;
        PreviewCallback _previewCallback;
        UndoCallback _undoCallback;
        StatusCallback _statusCallback;
        std::chrono::steady_clock::time_point _lastEdit{};
        bool _dirty = false;
        bool _loading = false;
        bool _formatting = false;
        bool _useCrLf = false;
        bool _hadUtf8Bom = false;
        bool _moveModeEnabled = true;
    };
}
