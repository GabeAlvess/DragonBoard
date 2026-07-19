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
}

int main()
{
    ValidateDataset(0);
    ValidateDataset(25);
    ValidateDataset(75);
    ValidateDataset(250);
    ValidateDataset(500);
    ValidateDataset(1000);

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
        "(0/25/75/250/500/1000 entries, max pool 10).\n";
    return 0;
}
