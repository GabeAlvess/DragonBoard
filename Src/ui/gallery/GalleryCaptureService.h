#pragma once

#include "ui/gallery/GalleryCatalog.h"

#include <filesystem>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace dragonboard::ui::gallery
{
    class GalleryCaptureService
    {
    public:
        static GalleryCaptureService& GetSingleton();
        bool RequestCapture();
        bool RequestDelete(std::string photoId);
        bool RequestRename(std::string photoId, std::string name);
        bool RequestSetMapPinned(
            std::string photoId,
            bool pinned,
            std::optional<GalleryMapLocation> mapLocation = std::nullopt);
        [[nodiscard]] static std::optional<GalleryMapLocation>
        ResolveMapLocationGameThread(const GalleryPhoto& photo);
        bool RequestSetPanelPinned(std::string photoId, bool pinned);
        bool RequestSetFavorite(std::string photoId, bool favorite);
        [[nodiscard]] static std::string ResolveLocationNameGameThread(
            RE::PlayerCharacter& player);
        void UpdateGameThread(float deltaTime);
        [[nodiscard]] bool IsBusy() const { return _state != State::kIdle; }
        [[nodiscard]] bool IsCaptureBusy() const
        {
            return _captureOperation.load(std::memory_order_acquire);
        }
        [[nodiscard]] bool ConsumeCatalogChanged();

    private:
        enum class State { kIdle, kWaitingForEngine, kProcessing };

        [[nodiscard]] static GalleryPhoto CaptureMetadata();
        [[nodiscard]] static std::filesystem::path GameDirectory();
        [[nodiscard]] static bool IsScreenshotCandidate(const std::filesystem::path& path);
        [[nodiscard]] static std::optional<std::filesystem::path> WaitForProducedFile(
            std::filesystem::file_time_type requestedAt,
            std::stop_token stopToken);
        [[nodiscard]] static bool ImportProducedFile(
            GalleryPhoto photo,
            const std::filesystem::path& source,
            std::uint32_t thumbnailWidth);
        [[nodiscard]] static bool DeletePhotoFiles(std::string_view photoId);
        bool StartWorker(std::function<bool()> operation);
        void StartCaptureWorker();
        void FinishWorker();
        void Reset();

        State _state = State::kIdle;
        GalleryPhoto _pendingPhoto;
        std::filesystem::file_time_type _captureRequestedAt{};
        std::jthread _worker;
        std::atomic<bool> _workerFinished{ false };
        std::atomic<bool> _workerSucceeded{ false };
        std::atomic<bool> _catalogChanged{ false };
        std::atomic<bool> _captureOperation{ false };
    };
}
