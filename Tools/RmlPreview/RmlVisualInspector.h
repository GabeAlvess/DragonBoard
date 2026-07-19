#pragma once

#include <RmlUi/Core/ElementDocument.h>

#include <Windows.h>
#include <CommCtrl.h>

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace dragonboard::tools
{
    class RmlVisualInspector
    {
    public:
        using StatusCallback = std::function<void(std::string)>;

        bool Create(HINSTANCE instance, HWND owner, StatusCallback statusCallback);
        void Destroy();
        void Show(bool visible);
        void Toggle();
        void Update();
        [[nodiscard]] bool IsVisible() const;

        void SetDocument(Rml::ElementDocument* document, const std::filesystem::path& path);
        void SelectElement(Rml::Element* element);
        bool BeginMove(Rml::Element* element);
        void UpdateMove(float deltaX, float deltaY);
        [[nodiscard]] std::string EndMove();
        [[nodiscard]] std::string SerializeOverrides() const;
        void SetOverrideSource(std::string_view source);
        bool Undo();

    private:
        using Properties = std::map<std::string, std::string>;
        using Overrides = std::map<std::string, Properties>;

        static constexpr UINT kTreeId = 4100;
        static constexpr UINT kSelectorId = 4101;
        static constexpr UINT kPropertyBaseId = 4200;
        static constexpr UINT kApplyId = 4300;
        static constexpr UINT kSaveId = 4301;
        static constexpr UINT kUndoId = 4302;
        static constexpr UINT kRedoId = 4303;

        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

        void CreateControls();
        void LayoutControls();
        void RebuildTree();
        void SyncTreeSelection();
        void AddTreeElement(Rml::Element* element, HTREEITEM parent);
        void PopulatePropertyFields();
        void ApplyFields();
        void ApplyOverrides(const Overrides& previous);
        void SaveOverrides();
        void LoadOverrides();
        void ParseOverrides(std::istream& stream);
        void Redo();
        void UpdateButtons();

        [[nodiscard]] std::string BuildSelector(Rml::Element* element) const;
        [[nodiscard]] Rml::Element* FindFirst(const std::string& selector) const;
        [[nodiscard]] std::filesystem::path OverridePath() const;
        [[nodiscard]] static std::wstring ToWide(std::string_view value);
        [[nodiscard]] static std::string ToUtf8(std::wstring_view value);
        [[nodiscard]] static std::wstring GetWindowString(HWND window);

        HWND _window = nullptr;
        HWND _owner = nullptr;
        HWND _tree = nullptr;
        HWND _selectorLabel = nullptr;
        HWND _selector = nullptr;
        std::vector<HWND> _propertyLabels;
        std::vector<HWND> _propertyFields;
        HWND _applyButton = nullptr;
        HWND _saveButton = nullptr;
        HWND _undoButton = nullptr;
        HWND _redoButton = nullptr;
        Rml::ElementDocument* _document = nullptr;
        Rml::Element* _selected = nullptr;
        std::filesystem::path _documentPath;
        std::unordered_map<Rml::Element*, HTREEITEM> _treeItems;
        HTREEITEM _lastTreeSelection = nullptr;
        Overrides _overrides;
        std::vector<Overrides> _undo;
        std::vector<Overrides> _redo;
        std::set<std::size_t> _dirtyProperties;
        StatusCallback _statusCallback;
        bool _populatingFields = false;
        Rml::Element* _movingElement = nullptr;
        std::string _movingSelector;
        Overrides _movePrevious;
        float _moveInitialLeft = 0.0f;
        float _moveInitialTop = 0.0f;
    };
}
