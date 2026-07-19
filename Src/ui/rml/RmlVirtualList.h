#pragma once

#include <cstddef>

namespace dragonboard::ui::rml
{
    class RmlVirtualList
    {
    public:
        struct Window
        {
            std::size_t firstIndex = 0;
            std::size_t rowCount = 0;
            float totalHeight = 0.0f;

            [[nodiscard]] bool operator==(const Window&) const = default;
        };

        RmlVirtualList(float viewportHeight, float rowHeight, std::size_t overscanRows);

        void SetItemCount(std::size_t itemCount);
        [[nodiscard]] bool Update(float scrollOffset);
        void Reset();

        [[nodiscard]] const Window& GetWindow() const;
        [[nodiscard]] std::size_t GetPoolSize() const;
        [[nodiscard]] float GetRowOffset(std::size_t itemIndex) const;

    private:
        [[nodiscard]] Window CalculateWindow(float scrollOffset) const;

        float _viewportHeight = 0.0f;
        float _rowHeight = 0.0f;
        std::size_t _overscanRows = 0;
        std::size_t _itemCount = 0;
        Window _window{};
    };
}
