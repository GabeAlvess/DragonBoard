#include "ui/rml/LocalizationManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr std::array<const char*, 3> kTranslationRoots{
            "Data/SKSE/Plugins/DragonBoardVR/translations",
            "SKSE/Plugins/DragonBoardVR/translations",
            "Assets/ui/translations"
        };

        struct DiscoveredLanguage
        {
            LocalizationManager::Language language;
            std::filesystem::path catalogPath;
            std::vector<std::string> aliases;
        };

        std::string LowerAscii(std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return result;
        }

        std::string Trim(std::string_view value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return std::string(value.substr(first, last - first + 1));
        }

        std::vector<DiscoveredLanguage> DiscoverLanguages()
        {
            std::vector<DiscoveredLanguage> discovered;
            std::unordered_set<std::string> knownCodes;

            for (const auto* root : kTranslationRoots) {
                std::error_code error;
                const std::filesystem::path directory(root);
                if (!std::filesystem::is_directory(directory, error)) continue;

                std::vector<std::filesystem::path> catalogPaths;
                for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
                    if (error) break;
                    if (entry.is_regular_file() && LowerAscii(entry.path().extension().string()) == ".json") {
                        catalogPaths.push_back(entry.path());
                    }
                }
                std::ranges::sort(catalogPaths);

                for (const auto& path : catalogPaths) {
                    try {
                        std::ifstream stream(path, std::ios::binary);
                        const auto document = nlohmann::json::parse(stream);
                        const auto code = Trim(document.value("language", path.stem().string()));
                        if (code.empty()) {
                            logger::warn(
                                "DragonBoardVR: translation catalog '{}' has no language code.",
                                path.string());
                            continue;
                        }

                        auto nativeName = Trim(document.value("name", code));
                        if (nativeName.empty()) nativeName = code;

                        DiscoveredLanguage language{
                            .language = { code, nativeName },
                            .catalogPath = path
                        };
                        if (const auto aliases = document.find("aliases");
                            aliases != document.end() && aliases->is_array()) {
                            for (const auto& alias : *aliases) {
                                if (!alias.is_string()) continue;
                                auto value = Trim(alias.get_ref<const std::string&>());
                                if (!value.empty()) language.aliases.push_back(std::move(value));
                            }
                        }
                        const auto normalizedCode = LowerAscii(code);
                        if (!knownCodes.emplace(normalizedCode).second) continue;
                        discovered.push_back(std::move(language));
                    } catch (const std::exception& error) {
                        logger::error(
                            "DragonBoardVR: could not inspect translation catalog '{}': {}.",
                            path.string(),
                            error.what());
                    }
                }
            }

            if (!knownCodes.contains("en")) {
                discovered.push_back({ .language = { "en", "English" } });
            }
            std::ranges::sort(discovered, [](const auto& left, const auto& right) {
                const auto leftCode = LowerAscii(left.language.code);
                const auto rightCode = LowerAscii(right.language.code);
                if (leftCode == "en") return rightCode != "en";
                if (rightCode == "en") return false;
                return leftCode < rightCode;
            });
            return discovered;
        }

        std::string ResolveCode(
            std::string_view requestedCode,
            const std::vector<DiscoveredLanguage>& languages)
        {
            const auto requested = LowerAscii(Trim(requestedCode));
            if (requested.empty() || requested == "auto") return "en";

            for (const auto& language : languages) {
                if (LowerAscii(language.language.code) == requested ||
                    LowerAscii(language.language.nativeName) == requested ||
                    std::ranges::any_of(language.aliases, [&](const auto& alias) {
                        return LowerAscii(alias) == requested;
                    })) {
                    return language.language.code;
                }
            }

            const auto separator = requested.find('-');
            const auto base = requested.substr(0, separator);
            for (const auto& language : languages) {
                const auto available = LowerAscii(language.language.code);
                if (available == base || available.starts_with(base + "-") ||
                    requested.starts_with(available + "-")) {
                    return language.language.code;
                }
            }
            return "en";
        }

        std::string EscapeRmlText(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value) {
                switch (character) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                default: escaped.push_back(character); break;
                }
            }
            return escaped;
        }
    }

    bool LocalizationManager::Load(std::string_view requestedCode)
    {
        _translations.clear();
        const auto languages = DiscoverLanguages();
        _activeCode = ResolveCode(requestedCode, languages);
        if (LowerAscii(_activeCode) == "en") {
            _activeCode = "en";
            logger::info("DragonBoardVR: interface language set to English.");
            return true;
        }

        const auto language = std::ranges::find_if(languages, [&](const auto& candidate) {
            return LowerAscii(candidate.language.code) == LowerAscii(_activeCode);
        });
        if (language != languages.end() && !language->catalogPath.empty()) {
            const auto& path = language->catalogPath;

            try {
                std::ifstream stream(path, std::ios::binary);
                const auto document = nlohmann::json::parse(stream);
                const auto& translations = document.contains("translations") ?
                    document.at("translations") : document;
                if (!translations.is_object()) {
                    throw std::runtime_error("translations must be a JSON object");
                }
                for (const auto& [english, translated] : translations.items()) {
                    if (translated.is_string() && !translated.get_ref<const std::string&>().empty()) {
                        _translations.emplace(english, translated.get<std::string>());
                    }
                }
                logger::info(
                    "DragonBoardVR: loaded {} '{}' interface translations from '{}'.",
                    _translations.size(),
                    _activeCode,
                    path.string());
                return true;
            } catch (const std::exception& error) {
                logger::error(
                    "DragonBoardVR: could not load interface translations from '{}': {}.",
                    path.string(),
                    error.what());
            }
        }

        logger::warn(
            "DragonBoardVR: translation '{}' was not found; falling back to English.",
            _activeCode);
        _activeCode = "en";
        return false;
    }

    std::string LocalizationManager::Translate(std::string_view english) const
    {
        if (const auto it = _translations.find(std::string(english));
            it != _translations.end()) {
            return it->second;
        }
        return std::string(english);
    }

    std::string LocalizationManager::TranslateMarkup(std::string_view markup) const
    {
        if (_translations.empty()) return std::string(markup);

        std::string result;
        result.reserve(markup.size() + markup.size() / 8);
        std::size_t cursor = 0;
        while (cursor < markup.size()) {
            auto textStart = markup.find('>', cursor);
            if (textStart == std::string_view::npos) {
                result.append(markup.substr(cursor));
                break;
            }
            ++textStart;
            result.append(markup.substr(cursor, textStart - cursor));

            const auto textEnd = markup.find('<', textStart);
            if (textEnd == std::string_view::npos) {
                result.append(markup.substr(textStart));
                break;
            }

            const auto segment = markup.substr(textStart, textEnd - textStart);
            const auto first = segment.find_first_not_of(" \t\r\n");
            const auto last = segment.find_last_not_of(" \t\r\n");
            if (first == std::string_view::npos) {
                result.append(segment);
            } else {
                const auto text = segment.substr(first, last - first + 1);
                result.append(segment.substr(0, first));
                if (const auto it = _translations.find(std::string(text));
                    it != _translations.end()) {
                    result.append(EscapeRmlText(it->second));
                } else {
                    result.append(text);
                }
                result.append(segment.substr(last + 1));
            }
            cursor = textEnd;
        }
        return result;
    }

    std::vector<LocalizationManager::Language>
    LocalizationManager::SupportedLanguages()
    {
        std::vector<Language> languages;
        for (auto& discovered : DiscoverLanguages()) {
            languages.push_back(std::move(discovered.language));
        }
        return languages;
    }

    std::string LocalizationManager::NormalizeCode(std::string_view code)
    {
        const auto languages = DiscoverLanguages();
        return ResolveCode(code, languages);
    }

    std::string LocalizationManager::NativeName(std::string_view code)
    {
        const auto languages = DiscoverLanguages();
        const auto normalized = ResolveCode(code, languages);
        for (const auto& language : languages) {
            if (language.language.code == normalized) return language.language.nativeName;
        }
        return "English";
    }

    std::string LocalizationManager::PreviousCode(std::string_view code)
    {
        const auto languages = DiscoverLanguages();
        if (languages.empty()) return "en";
        const auto normalized = ResolveCode(code, languages);
        for (std::size_t index = 0; index < languages.size(); ++index) {
            if (languages[index].language.code == normalized) {
                return languages[
                    (index + languages.size() - 1) % languages.size()].language.code;
            }
        }
        return languages.front().language.code;
    }

    std::string LocalizationManager::NextCode(std::string_view code)
    {
        const auto languages = DiscoverLanguages();
        if (languages.empty()) return "en";
        const auto normalized = ResolveCode(code, languages);
        for (std::size_t index = 0; index < languages.size(); ++index) {
            if (languages[index].language.code == normalized) {
                return languages[(index + 1) % languages.size()].language.code;
            }
        }
        return languages.front().language.code;
    }
}
