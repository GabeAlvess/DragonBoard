#pragma once

#include <RE/Skyrim.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dragonboard::ui::gallery
{
    struct GalleryPhoto
    {
        std::string id;
        std::string name;
        std::string imagePath;
        std::string thumbnailPath;
        std::string originalPath;
        std::string capturedAt;
        std::string skyrimDate;
        std::string location;
        std::uint32_t cellFormId = 0;
        std::uint32_t worldspaceFormId = 0;
        RE::NiPoint3 position{};
        RE::NiPoint3 angle{};
        bool mapPinned = false;
        bool panelPinned = false;
        bool favorite = false;
    };

    struct GalleryMapLocation
    {
        RE::FormID worldspaceFormId = 0;
        RE::NiPoint3 position{};
    };

    class GalleryCatalog
    {
    public:
        static GalleryCatalog& GetSingleton();
        bool Load();
        bool Save() const;
        bool Add(GalleryPhoto photo);
        bool Rename(std::string_view id, std::string name);
        bool SetMapPinned(std::string_view id, bool pinned, std::optional<GalleryMapLocation> mapLocation = std::nullopt);
        bool SetPanelPinned(std::string_view id, bool pinned);
        bool SetFavorite(std::string_view id, bool favorite);
        [[nodiscard]] std::optional<GalleryPhoto> Delete(std::string_view id);
        [[nodiscard]] std::optional<GalleryPhoto> Find(std::string_view id) const;
        [[nodiscard]] std::vector<GalleryPhoto> Snapshot() const;
        [[nodiscard]] static std::filesystem::path RootPath();
        [[nodiscard]] static std::filesystem::path PhotosPath();
        [[nodiscard]] static std::filesystem::path CatalogPath();

    private:
        [[nodiscard]] static bool SaveSnapshot(const std::vector<GalleryPhoto>& photos);
        mutable std::mutex _mutex;
        std::vector<GalleryPhoto> _photos;
    };
}
