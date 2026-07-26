#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dragonboard::ui::mods
{
    enum class IniValueType : std::uint8_t
    {
        kBoolean,
        kInteger,
        kFloat,
        kString
    };

    struct IniSetting
    {
        std::string section;
        std::string key;
        std::string value;
        std::string description;
        IniValueType type = IniValueType::kString;
        std::size_t line = 0;
        std::size_t occurrence = 0;
        bool sensitive = false;
    };

    struct IniFile
    {
        std::string id;
        std::string name;
        std::string relativePath;
        std::string encoding;
        std::string sha256;
        std::string effectiveProviderName;
        std::vector<std::string> providers;
        std::vector<IniSetting> settings;
        std::uintmax_t size = 0;
        std::int64_t modifiedNs = 0;
        bool editable = false;
        bool effectiveProvider = false;
        bool hasConflict = false;
    };

    struct IniMod
    {
        std::string id;
        std::string name;
        std::string folder;
        int priority = 0;
        bool overwrite = false;
        std::vector<IniFile> files;
    };

    class IniCatalog
    {
    public:
        bool Load(const std::filesystem::path& path, std::string& error);
        void Clear();

        [[nodiscard]] const std::filesystem::path& Mo2Root() const { return _mo2Root; }
        [[nodiscard]] const std::filesystem::path& ModsDirectory() const { return _modsDirectory; }
        [[nodiscard]] const std::filesystem::path& OverwriteDirectory() const { return _overwriteDirectory; }
        [[nodiscard]] const std::string& Profile() const { return _profile; }
        [[nodiscard]] const std::vector<IniMod>& Mods() const { return _mods; }
        [[nodiscard]] std::size_t ActiveModCount() const { return _activeModCount; }
        [[nodiscard]] std::size_t IniFileCount() const { return _iniFileCount; }

    private:
        std::filesystem::path _mo2Root;
        std::filesystem::path _modsDirectory;
        std::filesystem::path _overwriteDirectory;
        std::string _profile;
        std::vector<IniMod> _mods;
        std::size_t _activeModCount = 0;
        std::size_t _iniFileCount = 0;
    };
}
