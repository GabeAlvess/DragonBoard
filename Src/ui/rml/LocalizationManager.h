#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dragonboard::ui::rml
{
    class LocalizationManager
    {
    public:
        struct Language
        {
            std::string code;
            std::string nativeName;
        };

        bool Load(std::string_view requestedCode);

        [[nodiscard]] std::string Translate(std::string_view english) const;
        [[nodiscard]] std::string TranslateMarkup(std::string_view markup) const;
        [[nodiscard]] const std::string& ActiveCode() const { return _activeCode; }

        [[nodiscard]] static std::vector<Language> SupportedLanguages();
        [[nodiscard]] static std::string NormalizeCode(std::string_view code);
        [[nodiscard]] static std::string NativeName(std::string_view code);
        [[nodiscard]] static std::string PreviousCode(std::string_view code);
        [[nodiscard]] static std::string NextCode(std::string_view code);

    private:
        std::string _activeCode{ "en" };
        std::unordered_map<std::string, std::string> _translations;
    };
}
