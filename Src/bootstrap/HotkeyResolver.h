#pragma once

#include <cstdint>
#include <vector>

namespace dragonboard::bootstrap
{
    [[nodiscard]] std::vector<std::uint32_t> ResolveHotkey8KeyboardKeys();
}
