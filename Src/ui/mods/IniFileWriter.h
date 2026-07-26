#pragma once

#include "ui/mods/IniCatalog.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace dragonboard::ui::mods
{
    struct IniSaveResult
    {
        bool success = false;
        std::size_t filesWritten = 0;
        std::string message;
    };

    IniSaveResult SaveIniDrafts(
        const IniCatalog& catalog,
        const std::unordered_map<std::string, std::string>& drafts);
}
