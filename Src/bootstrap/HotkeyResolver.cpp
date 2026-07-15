#include "bootstrap/HotkeyResolver.h"

#include <RE/C/ControlMap.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace dragonboard::bootstrap
{
    namespace
    {
        std::vector<std::uint32_t> ParseKeyTokenList(const std::string& tokenList)
        {
            std::vector<std::uint32_t> keys;
            std::istringstream stream(tokenList);
            std::string token;

            while (std::getline(stream, token, ',')) {
                token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char character) {
                    return std::isspace(character) != 0;
                }), token.end());
                if (token.empty()) {
                    continue;
                }

                if (token.starts_with("!0") || token == "0xff" || token == "0xFF" || token == "255") {
                    continue;
                }

                if (const auto plusPosition = token.find('+'); plusPosition != std::string::npos) {
                    token = token.substr(0, plusPosition);
                }

                char* end = nullptr;
                const auto parsed = std::strtoul(token.c_str(), &end, 0);
                if (!end || end == token.c_str() || *end != '\0') {
                    continue;
                }

                const auto key = static_cast<std::uint32_t>(parsed);
                if (key != RE::ControlMap::kInvalid) {
                    keys.push_back(key);
                }
            }

            return keys;
        }

        std::vector<std::uint32_t> LoadHotkey8FromControlMapFile()
        {
            const std::array<std::filesystem::path, 4> candidates{
                "Data/Interface/controls/pc/controlmapvr.txt",
                "Data/Interface/controls/pc/controlmap.txt",
                "controlmapvr.txt",
                "controlmap.txt"
            };

            for (const auto& path : candidates) {
                if (!std::filesystem::exists(path)) {
                    continue;
                }

                std::ifstream file(path);
                if (!file.is_open()) {
                    continue;
                }

                std::string line;
                while (std::getline(file, line)) {
                    const auto first = line.find_first_not_of(" \t");
                    if (first == std::string::npos) {
                        continue;
                    }

                    const auto trimmed = line.substr(first);
                    if (trimmed.starts_with("//") || trimmed.starts_with(";")) {
                        continue;
                    }
                    if (!trimmed.starts_with("Hotkey8")) {
                        continue;
                    }

                    std::istringstream lineStream(trimmed);
                    std::string eventName;
                    std::string keyboardField;
                    lineStream >> eventName >> keyboardField;

                    if (eventName == "Hotkey8" && !keyboardField.empty()) {
                        return ParseKeyTokenList(keyboardField);
                    }
                }
            }

            return {};
        }
    }

    std::vector<std::uint32_t> ResolveHotkey8KeyboardKeys()
    {
        std::vector<std::uint32_t> keys;

        if (auto* controlMap = RE::ControlMap::GetSingleton()) {
            const auto primary = controlMap->GetMappedKey("Hotkey8", RE::INPUT_DEVICE::kKeyboard);
            if (primary != RE::ControlMap::kInvalid) {
                keys.push_back(primary);
            }
        }

        auto fromFile = LoadHotkey8FromControlMapFile();
        keys.insert(keys.end(), fromFile.begin(), fromFile.end());

        std::unordered_set<std::uint32_t> seen;
        std::vector<std::uint32_t> deduplicated;
        deduplicated.reserve(keys.size());
        for (const auto key : keys) {
            if (seen.insert(key).second) {
                deduplicated.push_back(key);
            }
        }

        return deduplicated;
    }
}
