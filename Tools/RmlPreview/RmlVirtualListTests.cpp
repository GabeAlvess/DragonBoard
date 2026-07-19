#include "ui/rml/RmlVirtualList.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using dragonboard::ui::rml::RmlVirtualList;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void ValidateDataset(std::size_t itemCount)
    {
        RmlVirtualList list(548.0f, 108.0f, 4);
        list.SetItemCount(itemCount);
        (void)list.Update(0.0f);

        const auto expectedPool = std::min<std::size_t>(itemCount, 10);
        Require(list.GetPoolSize() == expectedPool, "unexpected pool size");
        Require(list.GetWindow().firstIndex == 0, "initial window did not start at zero");
        Require(list.GetWindow().rowCount == expectedPool, "unexpected materialized row count");
        Require(
            std::abs(
                list.GetWindow().totalHeight - static_cast<float>(itemCount) * 108.0f) < 0.01f,
            "virtual height did not match the complete dataset");

        if (itemCount > 0) {
            const auto step = std::max<std::size_t>(itemCount / 17, 1);
            for (std::size_t itemIndex = 0; itemIndex < itemCount; itemIndex += step) {
                (void)list.Update(static_cast<float>(itemIndex) * 108.0f);
                const auto& window = list.GetWindow();
                Require(window.rowCount <= 10, "materialized pool exceeded ten rows");
                Require(window.firstIndex <= itemIndex, "window skipped its requested item");
                Require(
                    window.firstIndex + window.rowCount > itemIndex,
                    "window did not contain its requested item");
            }
        }

        if (itemCount > expectedPool) {
            (void)list.Update(static_cast<float>(itemCount) * 108.0f);
            Require(
                list.GetWindow().firstIndex + list.GetWindow().rowCount == itemCount,
                "final window did not include the last item");
        }
    }

    void ValidateNoUnnecessaryRebuilds()
    {
        RmlVirtualList list(548.0f, 108.0f, 4);
        list.SetItemCount(1000);
        Require(list.Update(0.0f), "initial window was not materialized");
        Require(!list.Update(0.0f), "identical window requested a rebuild");
        Require(
            !list.Update(107.0f),
            "sub-row scroll requested a rebuild before the visible row changed");
        Require(
            !list.Update(216.0f),
            "overscan requested a rebuild while the pooled window was unchanged");
        Require(
            list.Update(324.0f),
            "advancing beyond the leading overscan did not update the virtual window");
        Require(
            !list.Update(324.0f),
            "stable scrolled window requested a second rebuild");
    }

    void ValidateSelectionAcrossFullScroll()
    {
        constexpr std::size_t itemCount = 1000;
        RmlVirtualList list(548.0f, 108.0f, 4);
        list.SetItemCount(itemCount);

        for (std::size_t selectedIndex = 0;
             selectedIndex < itemCount;
             selectedIndex += 13) {
            (void)list.Update(list.GetRowOffset(selectedIndex));
            const auto& window = list.GetWindow();
            Require(
                selectedIndex >= window.firstIndex,
                "selected item preceded the materialized window");
            Require(
                selectedIndex < window.firstIndex + window.rowCount,
                "selected item followed the materialized window");

            const auto poolIndex = selectedIndex - window.firstIndex;
            const auto mappedIndex = window.firstIndex + poolIndex;
            Require(
                mappedIndex == selectedIndex,
                "virtual pool index did not map back to the selected real index");
        }

        (void)list.Update(list.GetRowOffset(itemCount - 1));
        const auto& finalWindow = list.GetWindow();
        Require(
            finalWindow.firstIndex + finalWindow.rowCount == itemCount,
            "full scroll did not end on the final real item");
    }

    void ValidateEmptySearchResultTransition()
    {
        RmlVirtualList list(548.0f, 108.0f, 4);
        list.SetItemCount(250);
        Require(list.Update(1080.0f), "populated search result was not materialized");

        list.SetItemCount(0);
        Require(list.Update(1080.0f), "empty search result did not invalidate the window");
        Require(list.GetPoolSize() == 0, "empty search retained pooled rows");
        Require(list.GetWindow().rowCount == 0, "empty search retained materialized rows");
        Require(list.GetWindow().firstIndex == 0, "empty search retained a scroll index");
        Require(list.GetWindow().totalHeight == 0.0f, "empty search retained virtual height");

        list.SetItemCount(25);
        Require(list.Update(0.0f), "cleared search did not restore the virtual window");
        Require(list.GetWindow().firstIndex == 0, "cleared search did not reset to the start");
        Require(list.GetPoolSize() == 10, "cleared search restored an invalid pool size");
    }
}

int main()
{
    ValidateDataset(0);
    ValidateDataset(25);
    ValidateDataset(75);
    ValidateDataset(250);
    ValidateDataset(500);
    ValidateDataset(1000);
    ValidateNoUnnecessaryRebuilds();
    ValidateSelectionAcrossFullScroll();
    ValidateEmptySearchResultTransition();

    RmlVirtualList list(548.0f, 108.0f, 4);
    list.SetItemCount(1000);
    (void)list.Update(500.0f * 108.0f);
    const auto window = list.GetWindow();
    Require(window.firstIndex <= 500, "middle window started after the requested item");
    Require(
        window.firstIndex + window.rowCount > 500,
        "middle window did not include the requested item");

    std::cout <<
        "RmlVirtualList Inventory/Magic tests passed "
        "(0/25/75/250/500/1000 entries, max pool 10, "
        "stable windows, full-scroll mapping, empty search).\n";
    return 0;
}
