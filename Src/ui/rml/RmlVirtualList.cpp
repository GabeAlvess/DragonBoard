#include "ui/rml/RmlVirtualList.h"

#include <algorithm>
#include <cmath>

namespace dragonboard::ui::rml
{
    RmlVirtualList::RmlVirtualList(
        float viewportHeight,
        float rowHeight,
        std::size_t overscanRows) :
        _viewportHeight(std::max(viewportHeight, 1.0f)),
        _rowHeight(std::max(rowHeight, 1.0f)),
        _overscanRows(overscanRows)
    {}

    void RmlVirtualList::SetItemCount(std::size_t itemCount)
    {
        _itemCount = itemCount;
    }

    bool RmlVirtualList::Update(float scrollOffset)
    {
        const auto next = CalculateWindow(scrollOffset);
        if (next == _window) return false;
        _window = next;
        return true;
    }

    void RmlVirtualList::Reset()
    {
        _itemCount = 0;
        _window = {};
    }

    const RmlVirtualList::Window& RmlVirtualList::GetWindow() const
    {
        return _window;
    }

    std::size_t RmlVirtualList::GetPoolSize() const
    {
        const auto visibleRows = static_cast<std::size_t>(
            std::ceil(_viewportHeight / _rowHeight));
        return std::min(_itemCount, visibleRows + _overscanRows);
    }

    float RmlVirtualList::GetRowOffset(std::size_t itemIndex) const
    {
        return static_cast<float>(itemIndex) * _rowHeight;
    }

    RmlVirtualList::Window RmlVirtualList::CalculateWindow(float scrollOffset) const
    {
        Window result{};
        result.totalHeight = static_cast<float>(_itemCount) * _rowHeight;
        result.rowCount = GetPoolSize();
        if (result.rowCount == 0) return result;

        const auto visibleFirst = std::min(
            static_cast<std::size_t>(
                std::floor(std::max(scrollOffset, 0.0f) / _rowHeight)),
            _itemCount - 1);
        const auto rowsBefore = _overscanRows / 2;
        result.firstIndex = visibleFirst > rowsBefore ?
            visibleFirst - rowsBefore : 0;
        const auto maximumFirst = _itemCount - result.rowCount;
        result.firstIndex = std::min(result.firstIndex, maximumFirst);
        return result;
    }
}
