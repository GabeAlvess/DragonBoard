#include "ui/gallery/GalleryCatalog.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace dragonboard::ui::gallery
{
    namespace
    {
        std::filesystem::path GameDirectory()
        {
            std::array<wchar_t, 32768> executablePath{};
            const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
            if (length > 0 && length < executablePath.size()) {
                return std::filesystem::path(std::wstring(executablePath.data(), length)).parent_path();
            }
            std::error_code error;
            return std::filesystem::current_path(error);
        }

        std::filesystem::path NormalizeFinalPath(std::wstring path)
        {
            constexpr std::wstring_view uncPrefix = LR"(\\?\UNC\)";
            constexpr std::wstring_view prefix = LR"(\\?\)";
            if (path.starts_with(uncPrefix)) {
                path = LR"(\\)" + path.substr(uncPrefix.size());
            } else if (path.starts_with(prefix)) {
                path.erase(0, prefix.size());
            }
            return std::filesystem::path(std::move(path));
        }

        bool IsInsideGameData(const std::filesystem::path& path)
        {
            auto normalize = [](const std::filesystem::path& value) {
                auto text = value.lexically_normal().generic_wstring();
                std::transform(text.begin(), text.end(), text.begin(), ::towlower);
                return text;
            };
            const auto candidate = normalize(path);
            auto data = normalize(GameDirectory() / "Data");
            if (!data.ends_with(L'/')) data.push_back(L'/');
            return candidate.starts_with(data);
        }

        bool FilesEqual(
            const std::filesystem::path& leftPath,
            const std::filesystem::path& rightPath)
        {
            std::error_code error;
            const auto leftSize = std::filesystem::file_size(leftPath, error);
            if (error) return false;
            const auto rightSize = std::filesystem::file_size(rightPath, error);
            if (error || leftSize != rightSize) return false;

            std::ifstream left(leftPath, std::ios::binary);
            std::ifstream right(rightPath, std::ios::binary);
            if (!left || !right) return false;
            std::array<char, 64 * 1024> leftBuffer{};
            std::array<char, 64 * 1024> rightBuffer{};
            for (;;) {
                left.read(leftBuffer.data(), leftBuffer.size());
                right.read(rightBuffer.data(), rightBuffer.size());
                const auto leftCount = left.gcount();
                const auto rightCount = right.gcount();
                if (leftCount != rightCount) return false;
                if (leftCount == 0) return true;
                if (!std::equal(
                        leftBuffer.begin(), leftBuffer.begin() + leftCount,
                        rightBuffer.begin())) {
                    return false;
                }
            }
        }

        std::filesystem::path FindMo2PluginPath(
            const std::filesystem::path& loadedPluginPath)
        {
            std::vector<std::filesystem::path> matches;
            auto directory = GameDirectory();
            for (int depth = 0; depth < 5 && !directory.empty(); ++depth) {
                const auto mods = directory / "mods";
                std::error_code error;
                if (std::filesystem::is_directory(mods, error)) {
                    for (const auto& entry :
                         std::filesystem::directory_iterator(mods, error)) {
                        if (error) break;
                        if (!entry.is_directory(error)) {
                            error.clear();
                            continue;
                        }
                        const auto candidate = entry.path() / "SKSE" / "Plugins" /
                            loadedPluginPath.filename();
                        if (std::filesystem::is_regular_file(candidate, error) &&
                            FilesEqual(loadedPluginPath, candidate)) {
                            matches.push_back(candidate);
                        }
                        error.clear();
                    }
                }
                const auto parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
            if (matches.size() == 1) return matches.front();
            if (matches.size() > 1) {
                logger::warn(
                    "DragonBoardVR: multiple MO2 mods contain the loaded gallery DLL; "
                    "using the mapped module path.");
            }
            return {};
        }

        std::filesystem::path PluginFilePath()
        {
            static const int moduleAnchor = 0;
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&moduleAnchor),
                    &module)) {
                return {};
            }

            std::array<wchar_t, 32768> modulePathBuffer{};
            const DWORD modulePathLength = GetModuleFileNameW(
                module,
                modulePathBuffer.data(),
                static_cast<DWORD>(modulePathBuffer.size()));
            if (modulePathLength == 0 || modulePathLength >= modulePathBuffer.size()) {
                return {};
            }
            const std::filesystem::path modulePath(
                std::wstring(modulePathBuffer.data(), modulePathLength));

            std::filesystem::path mappedPath;
            const HANDLE file = CreateFileW(
                modulePath.c_str(),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                const DWORD required = GetFinalPathNameByHandleW(
                    file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                if (required > 0) {
                    std::vector<wchar_t> finalPath(required + 1);
                    const DWORD written = GetFinalPathNameByHandleW(
                        file,
                        finalPath.data(),
                        static_cast<DWORD>(finalPath.size()),
                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                    if (written > 0 && written < finalPath.size()) {
                        mappedPath = NormalizeFinalPath(
                            std::wstring(finalPath.data(), written));
                    }
                }
                CloseHandle(file);
            }

            const auto comparisonPath = mappedPath.empty() ? modulePath : mappedPath;
            if (!IsInsideGameData(comparisonPath)) return comparisonPath;
            if (const auto mo2Path = FindMo2PluginPath(modulePath); !mo2Path.empty()) {
                return mo2Path;
            }
            return comparisonPath;
        }

        nlohmann::json ToJson(const GalleryPhoto& photo)
        {
            const auto serializePath = [](const std::string& value) {
                if (value.empty()) return value;
                const std::filesystem::path path(value);
                std::error_code error;
                const auto relative = std::filesystem::relative(
                    path, GalleryCatalog::RootPath(), error);
                if (!error && !relative.empty() &&
                    *relative.begin() != "..") {
                    return relative.generic_string();
                }
                return path.generic_string();
            };
            return {
                { "id", photo.id }, { "name", photo.name },
                { "image", serializePath(photo.imagePath) },
                { "thumbnail", serializePath(photo.thumbnailPath) },
                { "original", serializePath(photo.originalPath) },
                { "capturedAt", photo.capturedAt }, { "skyrimDate", photo.skyrimDate },
                { "location", photo.location }, { "cellFormId", photo.cellFormId },
                { "worldspaceFormId", photo.worldspaceFormId },
                { "position", { { "x", photo.position.x }, { "y", photo.position.y }, { "z", photo.position.z } } },
                { "angle", { { "x", photo.angle.x }, { "y", photo.angle.y }, { "z", photo.angle.z } } },
                { "mapPinned", photo.mapPinned },
                { "panelPinned", photo.panelPinned },
                { "favorite", photo.favorite }
            };
        }

        GalleryPhoto FromJson(const nlohmann::json& value)
        {
            GalleryPhoto photo;
            photo.id = value.value("id", "");
            photo.name = value.value("name", "");
            const auto deserializePath = [](std::string pathValue) {
                if (pathValue.empty()) return pathValue;
                std::filesystem::path path(std::move(pathValue));
                if (path.is_relative()) path = GalleryCatalog::RootPath() / path;
                return path.lexically_normal().string();
            };
            photo.imagePath = deserializePath(value.value("image", ""));
            photo.thumbnailPath = deserializePath(
                value.value("thumbnail", photo.imagePath));
            photo.originalPath = deserializePath(value.value("original", ""));
            photo.capturedAt = value.value("capturedAt", "");
            photo.skyrimDate = value.value("skyrimDate", "");
            photo.location = value.value("location", "");
            photo.cellFormId = value.value("cellFormId", 0u);
            photo.worldspaceFormId = value.value("worldspaceFormId", 0u);
            if (const auto it = value.find("position"); it != value.end()) {
                photo.position = { it->value("x", 0.0f), it->value("y", 0.0f), it->value("z", 0.0f) };
            }
            if (const auto it = value.find("angle"); it != value.end()) {
                photo.angle = { it->value("x", 0.0f), it->value("y", 0.0f), it->value("z", 0.0f) };
            }
            photo.mapPinned = value.value("mapPinned", false);
            photo.panelPinned = value.value("panelPinned", false);
            photo.favorite = value.value("favorite", false);
            return photo;
        }
    }

    GalleryCatalog& GalleryCatalog::GetSingleton()
    {
        static GalleryCatalog singleton;
        return singleton;
    }

    std::filesystem::path GalleryCatalog::RootPath()
    {
        static const auto root = [] {
            auto pluginPath = PluginFilePath();
            if (pluginPath.empty()) {
                pluginPath = GameDirectory() / "Data" / "SKSE" / "Plugins" /
                    "DragonBoardVR.dll";
            }
            auto resolved = pluginPath.parent_path() / "DragonBoardVR" / "gallery";
            logger::info(
                "DragonBoardVR: gallery storage resolved to '{}'.",
                resolved.string());
            return resolved;
        }();
        return root;
    }

    std::filesystem::path GalleryCatalog::PhotosPath() { return RootPath() / "photos"; }
    std::filesystem::path GalleryCatalog::CatalogPath() { return RootPath() / "gallery.json"; }

    bool GalleryCatalog::Load()
    {
        std::scoped_lock lock(_mutex);
        _photos.clear();
        std::error_code error;
        std::filesystem::create_directories(PhotosPath(), error);
        if (!std::filesystem::exists(CatalogPath(), error)) return true;
        try {
            std::ifstream stream(CatalogPath(), std::ios::binary);
            const auto document = nlohmann::json::parse(stream);
            const auto& photos = document.contains("photos") ? document["photos"] : document;
            if (!photos.is_array()) return false;
            for (const auto& entry : photos) {
                auto photo = FromJson(entry);
                if (photo.id.empty() || photo.imagePath.empty()) continue;
                if (!std::filesystem::exists(photo.imagePath, error)) {
                    logger::warn(
                        "DragonBoardVR: gallery image '{}' is missing; catalog entry '{}' was skipped.",
                        photo.imagePath,
                        photo.id);
                    error.clear();
                    continue;
                }
                if (!photo.thumbnailPath.empty() &&
                    !std::filesystem::exists(photo.thumbnailPath, error)) {
                    photo.thumbnailPath = photo.imagePath;
                    error.clear();
                }
                _photos.push_back(std::move(photo));
            }
            return true;
        } catch (const std::exception& exception) {
            logger::error("DragonBoardVR: gallery catalog load failed: {}", exception.what());
            return false;
        }
    }

    bool GalleryCatalog::Save() const
    {
        std::vector<GalleryPhoto> photos;
        {
            std::scoped_lock lock(_mutex);
            photos = _photos;
        }
        return SaveSnapshot(photos);
    }

    bool GalleryCatalog::SaveSnapshot(const std::vector<GalleryPhoto>& photosSnapshot)
    {
        std::error_code error;
        std::filesystem::create_directories(PhotosPath(), error);
        const auto path = CatalogPath();
        const auto temporary = path.wstring() + L".tmp";
        try {
            nlohmann::json photos = nlohmann::json::array();
            for (const auto& photo : photosSnapshot) photos.push_back(ToJson(photo));
            const nlohmann::json document{ { "version", 2 }, { "photos", std::move(photos) } };
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                stream << document.dump(2);
                stream.flush();
                if (!stream) return false;
            }
            if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::filesystem::remove(temporary, error);
                return false;
            }
            return true;
        } catch (const std::exception& exception) {
            std::filesystem::remove(temporary, error);
            logger::error("DragonBoardVR: gallery catalog save failed: {}", exception.what());
            return false;
        }
    }

    bool GalleryCatalog::Add(GalleryPhoto photo)
    {
        std::scoped_lock lock(_mutex);
        if (photo.id.empty() || photo.imagePath.empty() ||
            std::any_of(_photos.begin(), _photos.end(), [&](const auto& existing) { return existing.id == photo.id; })) return false;
        auto updated = _photos;
        updated.insert(updated.begin(), std::move(photo));
        if (!SaveSnapshot(updated)) return false;
        _photos = std::move(updated);
        return true;
    }

    bool GalleryCatalog::Rename(std::string_view id, std::string name)
    {
        std::scoped_lock lock(_mutex);
        auto updated = _photos;
        const auto it = std::find_if(updated.begin(), updated.end(), [&](const auto& photo) { return photo.id == id; });
        if (it == updated.end()) return false;
        it->name = std::move(name);
        if (!SaveSnapshot(updated)) return false;
        _photos = std::move(updated);
        return true;
    }

    bool GalleryCatalog::SetMapPinned(
        std::string_view id,
        bool pinned,
        std::optional<GalleryMapLocation> mapLocation)
    {
        std::scoped_lock lock(_mutex);
        auto updated = _photos;
        const auto it = std::find_if(updated.begin(), updated.end(), [&](const auto& photo) { return photo.id == id; });
        if (it == updated.end()) return false;
        it->mapPinned = pinned;
        if (mapLocation) {
            it->worldspaceFormId = mapLocation->worldspaceFormId;
            it->position = mapLocation->position;
        }
        if (!SaveSnapshot(updated)) return false;
        _photos = std::move(updated);
        return true;
    }

    bool GalleryCatalog::SetPanelPinned(std::string_view id, bool pinned)
    {
        std::scoped_lock lock(_mutex);
        auto updated = _photos;
        const auto it = std::find_if(updated.begin(), updated.end(), [&](const auto& photo) { return photo.id == id; });
        if (it == updated.end()) return false;
        it->panelPinned = pinned;
        if (!SaveSnapshot(updated)) return false;
        _photos = std::move(updated);
        return true;
    }

    bool GalleryCatalog::SetFavorite(std::string_view id, bool favorite)
    {
        std::scoped_lock lock(_mutex);
        auto updated = _photos;
        const auto it = std::find_if(updated.begin(), updated.end(), [&](const auto& photo) { return photo.id == id; });
        if (it == updated.end()) return false;
        it->favorite = favorite;
        if (!SaveSnapshot(updated)) return false;
        _photos = std::move(updated);
        return true;
    }

    std::optional<GalleryPhoto> GalleryCatalog::Delete(std::string_view id)
    {
        std::scoped_lock lock(_mutex);
        auto updated = _photos;
        const auto it = std::find_if(updated.begin(), updated.end(), [&](const auto& photo) { return photo.id == id; });
        if (it == updated.end()) return std::nullopt;
        GalleryPhoto deleted = *it;
        updated.erase(it);
        if (!SaveSnapshot(updated)) return std::nullopt;
        _photos = std::move(updated);
        return deleted;
    }

    std::optional<GalleryPhoto> GalleryCatalog::Find(std::string_view id) const
    {
        std::scoped_lock lock(_mutex);
        const auto it = std::find_if(_photos.begin(), _photos.end(), [&](const auto& photo) { return photo.id == id; });
        return it == _photos.end() ? std::nullopt : std::optional<GalleryPhoto>(*it);
    }

    std::vector<GalleryPhoto> GalleryCatalog::Snapshot() const
    {
        std::scoped_lock lock(_mutex);
        return _photos;
    }
}
