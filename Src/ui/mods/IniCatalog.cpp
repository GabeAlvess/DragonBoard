#include "ui/mods/IniCatalog.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace dragonboard::ui::mods
{
    namespace
    {
        IniValueType ParseValueType(std::string_view value)
        {
            if (value == "boolean") return IniValueType::kBoolean;
            if (value == "integer") return IniValueType::kInteger;
            if (value == "float") return IniValueType::kFloat;
            return IniValueType::kString;
        }
    }

    bool IniCatalog::Load(const std::filesystem::path& path, std::string& error)
    {
        try {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) {
                error = "INI catalog was not found: " + path.string();
                return false;
            }

            nlohmann::json root;
            stream >> root;
            if (root.value("schemaVersion", 0) != 1) {
                error = "Unsupported INI catalog schema version.";
                return false;
            }

            IniCatalog loaded;
            const auto& mo2 = root.at("mo2");
            loaded._mo2Root = mo2.at("root").get<std::string>();
            loaded._modsDirectory = mo2.at("modsDirectory").get<std::string>();
            loaded._overwriteDirectory = mo2.at("overwriteDirectory").get<std::string>();
            loaded._profile = mo2.at("profile").get<std::string>();
            const auto& summary = root.at("summary");
            loaded._activeModCount = summary.value("activeMods", std::size_t{ 0 });
            loaded._iniFileCount = summary.value("iniFiles", std::size_t{ 0 });

            for (const auto& modJson : root.at("mods")) {
                IniMod mod;
                mod.id = modJson.at("id").get<std::string>();
                mod.name = modJson.at("name").get<std::string>();
                mod.folder = modJson.value("folder", std::string{});
                mod.priority = modJson.value("priority", 0);
                mod.overwrite = modJson.value("overwrite", false);

                for (const auto& fileJson : modJson.at("files")) {
                    IniFile file;
                    file.id = fileJson.at("id").get<std::string>();
                    file.name = fileJson.at("name").get<std::string>();
                    file.relativePath = fileJson.at("relativePath").get<std::string>();
                    file.encoding = fileJson.value("encoding", std::string{ "utf-8" });
                    file.editable = fileJson.value("editable", false);
                    file.effectiveProvider = fileJson.value("effectiveProvider", false);
                    file.hasConflict = fileJson.value("hasConflict", false);
                    file.providers = fileJson.value(
                        "providers", std::vector<std::string>{});
                    if (!file.providers.empty()) {
                        file.effectiveProviderName = file.providers.front();
                    }
                    if (const auto fingerprint = fileJson.find("fingerprint");
                        fingerprint != fileJson.end()) {
                        file.size = fingerprint->value("size", std::uintmax_t{ 0 });
                        file.modifiedNs = fingerprint->value("modifiedNs", std::int64_t{ 0 });
                        file.sha256 = fingerprint->value("sha256", std::string{});
                    }

                    for (const auto& sectionJson : fileJson.at("sections")) {
                        const auto sectionName = sectionJson.value("name", std::string{});
                        for (const auto& settingJson : sectionJson.at("settings")) {
                            IniSetting setting;
                            setting.section = sectionName;
                            setting.key = settingJson.at("key").get<std::string>();
                            setting.value = settingJson.value("value", std::string{});
                            setting.description =
                                settingJson.value("description", std::string{});
                            setting.type = ParseValueType(
                                settingJson.value("valueType", std::string{ "string" }));
                            setting.line = settingJson.value("line", std::size_t{ 0 });
                            setting.occurrence =
                                settingJson.value("occurrence", std::size_t{ 0 });
                            setting.sensitive = settingJson.value("sensitive", false);
                            file.settings.push_back(std::move(setting));
                        }
                    }
                    mod.files.push_back(std::move(file));
                }
                loaded._mods.push_back(std::move(mod));
            }

            *this = std::move(loaded);
            error.clear();
            return true;
        } catch (const std::exception& exception) {
            error = std::string("Invalid INI catalog: ") + exception.what();
            return false;
        }
    }

    void IniCatalog::Clear()
    {
        *this = IniCatalog{};
    }
}
