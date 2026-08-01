#pragma once

#include <string_view>

namespace dragonboard::ui::menu
{
    enum class MenuActionMode
    {
        Open,
        Toggle
    };

    class MenuActionRouter
    {
    public:
        [[nodiscard]] static bool Execute(
            std::string_view action,
            MenuActionMode mode = MenuActionMode::Toggle);
    };
}
