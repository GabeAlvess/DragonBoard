#include "ui/mods/IniCatalog.h"
#include "ui/mods/IniFileWriter.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

namespace
{
    void WriteBytes(const std::filesystem::path& path, std::string_view bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    std::string ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>() };
    }

    bool Expect(bool condition, std::string_view message)
    {
        if (condition) return true;
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("DragonBoardIniEditorTests-" + std::to_string(GetCurrentProcessId()));
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);

    const auto modsDirectory = root / "mods";
    const auto overwriteDirectory = root / "overwrite";
    const auto iniPath =
        modsDirectory / "Test Mod" / "SKSE" / "Plugins" / "Test.ini";
    constexpr std::string_view original =
        "[General]\r\nEnabled=true\r\nCount = 12\r\n";
    WriteBytes(iniPath, original);

    nlohmann::json catalogJson{
        { "schemaVersion", 1 },
        { "mo2", {
            { "root", root.string() },
            { "profile", "Test" },
            { "modsDirectory", modsDirectory.string() },
            { "profilesDirectory", (root / "profiles").string() },
            { "overwriteDirectory", overwriteDirectory.string() } } },
        { "summary", {
            { "activeMods", 1 },
            { "modsWithIni", 1 },
            { "iniFiles", 1 } } },
        { "mods", nlohmann::json::array({
            {
                { "id", "test-mod" },
                { "name", "Test Mod" },
                { "folder", "Test Mod" },
                { "priority", 0 },
                { "active", true },
                { "files", nlohmann::json::array({
                    {
                        { "id", "test-file" },
                        { "name", "Test.ini" },
                        { "relativePath", "SKSE/Plugins/Test.ini" },
                        { "encoding", "utf-8" },
                        { "editable", true },
                        { "effectiveProvider", true },
                        { "hasConflict", false },
                        { "providers", nlohmann::json::array({ "Test Mod" }) },
                        { "fingerprint", {
                            { "size", original.size() },
                            { "modifiedNs", 0 },
                            { "sha256", "9b9649662ecab6fabe58a13df3550961c6b9cd9e0e6743b951eacb414e412125" } } },
                        { "sections", nlohmann::json::array({
                            {
                                { "name", "General" },
                                { "settings", nlohmann::json::array({
                                    {
                                        { "key", "Enabled" },
                                        { "value", "true" },
                                        { "valueType", "boolean" },
                                        { "sensitive", false },
                                        { "line", 2 },
                                        { "occurrence", 0 } },
                                    {
                                        { "key", "Count" },
                                        { "value", "12" },
                                        { "valueType", "integer" },
                                        { "sensitive", false },
                                        { "line", 3 },
                                        { "occurrence", 0 } }
                                }) }
                            }
                        }) }
                    }
                }) }
            }
        }) }
    };
    const auto catalogPath = root / "IniCatalog.json";
    WriteBytes(catalogPath, catalogJson.dump(2));

    dragonboard::ui::mods::IniCatalog catalog;
    std::string loadError;
    bool passed = Expect(catalog.Load(catalogPath, loadError), loadError);
    const std::unordered_map<std::string, std::string> drafts{
        { "0:0:0", "false" },
        { "0:0:1", "42" }
    };
    const auto save = dragonboard::ui::mods::SaveIniDrafts(catalog, drafts);
    passed &= Expect(save.success, save.message);
    passed &= Expect(save.filesWritten == 1, "Expected exactly one written file.");
    passed &= Expect(
        ReadBytes(iniPath) == "[General]\r\nEnabled=false\r\nCount = 42\r\n",
        "The writer did not preserve CRLF and unrelated formatting.");
    passed &= Expect(
        !std::filesystem::exists(root / "backups"),
        "The writer created a persistent backup directory.");

    const auto staleSave = dragonboard::ui::mods::SaveIniDrafts(catalog, drafts);
    passed &= Expect(!staleSave.success, "A stale catalog fingerprint was accepted.");

    std::filesystem::remove_all(root, cleanupError);
    if (!passed) return 1;
    std::cout << "IniEditorTests passed\n";
    return 0;
}
