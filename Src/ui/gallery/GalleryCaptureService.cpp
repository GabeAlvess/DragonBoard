#include "ui/gallery/GalleryCaptureService.h"
#include "vrui/VRUISettings.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cwctype>
#include <format>
#include <limits>
#include <utility>
#include <vector>

namespace dragonboard::ui::gallery
{
    namespace
    {
        std::string FormatLocalTime(const std::tm& value)
        {
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
                value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                value.tm_hour, value.tm_min, value.tm_sec);
        }

        std::string MakeId(const std::tm& value)
        {
            static std::uint32_t serial = 0;
            return std::format("{:04}{:02}{:02}_{:02}{:02}{:02}_{:04}",
                value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
                value.tm_hour, value.tm_min, value.tm_sec, ++serial % 10000);
        }

        bool CreatePngPhoto(
            const std::filesystem::path& source,
            const std::filesystem::path& destination)
        {
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool releaseCom = SUCCEEDED(comResult);
            if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return false;

            using Microsoft::WRL::ComPtr;
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> converter;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapEncoder> encoder;
            ComPtr<IWICBitmapFrameEncode> encodedFrame;
            ComPtr<IPropertyBag2> properties;

            HRESULT result = CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf()));
            if (SUCCEEDED(result)) {
                result = factory->CreateDecoderFromFilename(
                    source.c_str(), nullptr, GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
            }
            if (SUCCEEDED(result)) result = decoder->GetFrame(0, frame.GetAddressOf());

            UINT width = 0;
            UINT height = 0;
            if (SUCCEEDED(result)) result = frame->GetSize(&width, &height);
            if (SUCCEEDED(result) && (width == 0 || height == 0 ||
                width > (std::numeric_limits<UINT>::max)() / 4 ||
                height > (std::numeric_limits<UINT>::max)() / (width * 4))) {
                result = E_INVALIDARG;
            }
            if (SUCCEEDED(result)) {
                result = factory->CreateFormatConverter(converter.GetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = converter->Initialize(
                    frame.Get(), GUID_WICPixelFormat32bppBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0,
                    WICBitmapPaletteTypeCustom);
            }

            const UINT stride = width * 4;
            const UINT bufferSize = stride * height;
            std::vector<BYTE> pixels;
            if (SUCCEEDED(result)) {
                pixels.resize(bufferSize);
                result = converter->CopyPixels(
                    nullptr, stride, bufferSize, pixels.data());
            }

            if (SUCCEEDED(result)) result = factory->CreateStream(stream.GetAddressOf());
            if (SUCCEEDED(result)) {
                result = stream->InitializeFromFilename(destination.c_str(), GENERIC_WRITE);
            }
            if (SUCCEEDED(result)) {
                result = factory->CreateEncoder(
                    GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            }
            if (SUCCEEDED(result)) {
                result = encoder->CreateNewFrame(
                    encodedFrame.GetAddressOf(), properties.GetAddressOf());
            }
            if (SUCCEEDED(result)) result = encodedFrame->Initialize(properties.Get());
            if (SUCCEEDED(result)) result = encodedFrame->SetSize(width, height);
            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(result)) result = encodedFrame->SetPixelFormat(&pixelFormat);
            if (SUCCEEDED(result)) {
                result = encodedFrame->WritePixels(
                    height, stride, bufferSize, pixels.data());
            }
            if (SUCCEEDED(result)) result = encodedFrame->Commit();
            if (SUCCEEDED(result)) result = encoder->Commit();

            if (releaseCom) CoUninitialize();
            if (FAILED(result)) {
                std::error_code error;
                std::filesystem::remove(destination, error);
                logger::warn(
                    "DragonBoardVR: gallery PNG conversion failed for '{}': 0x{:08X}.",
                    source.string(), static_cast<std::uint32_t>(result));
                return false;
            }
            return true;
        }

        bool CreateThumbnail(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            std::uint32_t maximumWidth)
        {
            if (maximumWidth == 0) return false;
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool releaseCom = SUCCEEDED(comResult);
            if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return false;

            using Microsoft::WRL::ComPtr;
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICBitmapScaler> scaler;
            ComPtr<IWICFormatConverter> converter;
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapEncoder> encoder;
            ComPtr<IWICBitmapFrameEncode> encodedFrame;
            ComPtr<IPropertyBag2> properties;

            HRESULT result = CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf()));
            if (SUCCEEDED(result)) {
                result = factory->CreateDecoderFromFilename(
                    source.c_str(),
                    nullptr,
                    GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad,
                    decoder.GetAddressOf());
            }
            if (SUCCEEDED(result)) result = decoder->GetFrame(0, frame.GetAddressOf());

            UINT sourceWidth = 0;
            UINT sourceHeight = 0;
            if (SUCCEEDED(result)) result = frame->GetSize(&sourceWidth, &sourceHeight);
            const UINT targetWidth = std::min(sourceWidth, maximumWidth);
            const UINT targetHeight = sourceWidth > 0 ?
                std::max<UINT>(1, static_cast<UINT>(
                    std::lround(static_cast<double>(sourceHeight) *
                        static_cast<double>(targetWidth) /
                        static_cast<double>(sourceWidth)))) : 0;
            if (SUCCEEDED(result) && (targetWidth == 0 || targetHeight == 0)) {
                result = E_INVALIDARG;
            }

            IWICBitmapSource* bitmapSource = frame.Get();
            if (SUCCEEDED(result) &&
                (targetWidth != sourceWidth || targetHeight != sourceHeight)) {
                result = factory->CreateBitmapScaler(scaler.GetAddressOf());
                if (SUCCEEDED(result)) {
                    result = scaler->Initialize(
                        frame.Get(),
                        targetWidth,
                        targetHeight,
                        WICBitmapInterpolationModeFant);
                }
                bitmapSource = scaler.Get();
            }
            if (SUCCEEDED(result)) {
                result = factory->CreateFormatConverter(converter.GetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = converter->Initialize(
                    bitmapSource,
                    GUID_WICPixelFormat32bppBGRA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0.0,
                    WICBitmapPaletteTypeCustom);
            }
            if (SUCCEEDED(result)) result = factory->CreateStream(stream.GetAddressOf());
            if (SUCCEEDED(result)) {
                result = stream->InitializeFromFilename(
                    destination.c_str(), GENERIC_WRITE);
            }
            if (SUCCEEDED(result)) {
                result = factory->CreateEncoder(
                    GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            }
            if (SUCCEEDED(result)) {
                result = encoder->CreateNewFrame(
                    encodedFrame.GetAddressOf(), properties.GetAddressOf());
            }
            if (SUCCEEDED(result)) result = encodedFrame->Initialize(properties.Get());
            if (SUCCEEDED(result)) result = encodedFrame->SetSize(targetWidth, targetHeight);
            WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(result)) result = encodedFrame->SetPixelFormat(&pixelFormat);
            if (SUCCEEDED(result)) result = encodedFrame->WriteSource(converter.Get(), nullptr);
            if (SUCCEEDED(result)) result = encodedFrame->Commit();
            if (SUCCEEDED(result)) result = encoder->Commit();

            if (releaseCom) CoUninitialize();
            if (FAILED(result)) {
                std::error_code error;
                std::filesystem::remove(destination, error);
                logger::warn(
                    "DragonBoardVR: gallery thumbnail generation failed for '{}': 0x{:08X}.",
                    source.string(),
                    static_cast<std::uint32_t>(result));
                return false;
            }
            return true;
        }
    }

    GalleryCaptureService& GalleryCaptureService::GetSingleton()
    {
        static GalleryCaptureService singleton;
        return singleton;
    }

    std::filesystem::path GalleryCaptureService::GameDirectory()
    {
        std::array<wchar_t, 32768> executablePath{};
        const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (length > 0 && length < executablePath.size()) {
            return std::filesystem::path(std::wstring(executablePath.data(), length)).parent_path();
        }
        std::error_code error;
        return std::filesystem::current_path(error);
    }

    bool GalleryCaptureService::IsScreenshotCandidate(const std::filesystem::path& path)
    {
        auto extension = path.extension().wstring();
        auto stem = path.stem().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        std::transform(stem.begin(), stem.end(), stem.begin(), ::towlower);
        const bool image = extension == L".bmp" || extension == L".png" || extension == L".jpg" || extension == L".jpeg";
        return image && (stem.starts_with(L"screenshot") || stem.starts_with(L"screen shot"));
    }

    std::optional<GalleryMapLocation>
    GalleryCaptureService::ResolveMapLocationGameThread(const GalleryPhoto& photo)
    {
        if (photo.worldspaceFormId != 0) {
            return GalleryMapLocation{ photo.worldspaceFormId, photo.position };
        }
        auto* interiorCell =
            RE::TESForm::LookupByID<RE::TESObjectCELL>(photo.cellFormId);
        if (!interiorCell || !interiorCell->IsInteriorCell()) return std::nullopt;

        std::optional<GalleryMapLocation> resolved;
        interiorCell->ForEachReference([&](RE::TESObjectREFR* reference) {
            if (!reference) return RE::BSContainer::ForEachResult::kContinue;
            const auto linkedDoor =
                reference->extraList.GetTeleportLinkedDoor().get();
            auto* exteriorCell = linkedDoor ? linkedDoor->GetParentCell() : nullptr;
            if (!exteriorCell || !exteriorCell->IsExteriorCell()) {
                return RE::BSContainer::ForEachResult::kContinue;
            }
            auto* worldspace = exteriorCell->GetRuntimeData().worldSpace;
            if (!worldspace) return RE::BSContainer::ForEachResult::kContinue;

            resolved = GalleryMapLocation{
                worldspace->GetFormID(), linkedDoor->GetPosition() };
            logger::info(
                "DragonBoardVR: gallery interior cell {:08X} resolved through door {:08X} to exterior cell {:08X} in worldspace {:08X}.",
                interiorCell->GetFormID(),
                reference->GetFormID(),
                exteriorCell->GetFormID(),
                worldspace->GetFormID());
            return RE::BSContainer::ForEachResult::kStop;
        });
        return resolved;
    }

    std::string GalleryCaptureService::ResolveLocationNameGameThread(
        RE::PlayerCharacter& player)
    {
        std::string locationName;
        if (auto* location = player.GetCurrentLocation()) {
            if (const char* name = location->GetFullName(); name && *name) {
                locationName = name;
            }
        }

        std::string cellName;
        std::string worldspaceName;
        RE::FormID worldspaceFormId = 0;
        if (auto* cell = player.GetParentCell()) {
            if (const char* name = cell->GetName(); name && *name) cellName = name;
            if (auto* worldspace = cell->GetRuntimeData().worldSpace) {
                worldspaceFormId = worldspace->GetFormID();
                if (const char* name = worldspace->GetName(); name && *name) {
                    worldspaceName = name;
                }
            }
        }

        constexpr RE::FormID kTamrielWorldspace = 0x0000003C;
        const bool genericTamrielLocation =
            worldspaceFormId == kTamrielWorldspace &&
            (locationName.empty() || locationName == worldspaceName ||
                locationName == "SKYRIM" || locationName == "Skyrim" ||
                locationName == "Скайрим");
        if (!cellName.empty() && (locationName.empty() || genericTamrielLocation)) {
            return cellName;
        }
        if (genericTamrielLocation) return "SKYRIM";
        if (!locationName.empty()) return locationName;
        if (!cellName.empty()) return cellName;
        if (!worldspaceName.empty()) return worldspaceName;
        return "Unknown location";
    }

    GalleryPhoto GalleryCaptureService::CaptureMetadata()
    {
        GalleryPhoto photo;
        const auto now = std::chrono::system_clock::now();
        const std::time_t rawTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &rawTime);
        photo.id = MakeId(localTime);
        photo.capturedAt = FormatLocalTime(localTime);
        photo.name = std::format("Photo {}", photo.capturedAt);

        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            photo.position = player->GetPosition();
            photo.angle = player->GetAngle();
            photo.location = ResolveLocationNameGameThread(*player);
            if (auto* cell = player->GetParentCell()) {
                photo.cellFormId = cell->GetFormID();
                if (auto* worldspace = cell->GetRuntimeData().worldSpace) {
                    photo.worldspaceFormId = worldspace->GetFormID();
                }
            }
        }
        if (auto mapLocation = ResolveMapLocationGameThread(photo)) {
            photo.worldspaceFormId = mapLocation->worldspaceFormId;
            photo.position = mapLocation->position;
        }
        if (photo.location.empty()) photo.location = "Unknown location";
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            const float hour = calendar->GetHour();
            photo.skyrimDate = std::format("{} {}, 4E {} - {:02}:{:02}",
                calendar->GetMonthName(), static_cast<int>(calendar->GetDay()), calendar->GetYear(),
                static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f));
        }
        return photo;
    }

    bool GalleryCaptureService::RequestCapture()
    {
        if (_state != State::kIdle) return false;
        auto* controls = RE::MenuControls::GetSingleton();
        if (!controls) return false;
        _pendingPhoto = CaptureMetadata();
        _captureRequestedAt = std::filesystem::file_time_type::clock::now();
        _captureOperation.store(true, std::memory_order_release);
        if (!controls->QueueScreenshot()) {
            logger::warn("DragonBoardVR: native screenshot request was rejected.");
            Reset();
            return false;
        }
        _state = State::kWaitingForEngine;
        logger::info("DragonBoardVR: native gallery screenshot queued as '{}'.", _pendingPhoto.id);
        return true;
    }

    bool GalleryCaptureService::RequestDelete(std::string photoId)
    {
        if (photoId.empty()) return false;
        return StartWorker([photoId = std::move(photoId)]() {
            return DeletePhotoFiles(photoId);
        });
    }

    bool GalleryCaptureService::RequestRename(
        std::string photoId, std::string name)
    {
        if (photoId.empty() || name.empty()) return false;
        return StartWorker(
            [photoId = std::move(photoId), name = std::move(name)]() mutable {
                return GalleryCatalog::GetSingleton().Rename(
                    photoId, std::move(name));
            });
    }

    bool GalleryCaptureService::RequestSetMapPinned(
        std::string photoId,
        bool pinned,
        std::optional<GalleryMapLocation> mapLocation)
    {
        if (photoId.empty()) return false;
        return StartWorker([
            photoId = std::move(photoId),
            pinned,
            mapLocation = std::move(mapLocation)]() {
            return GalleryCatalog::GetSingleton().SetMapPinned(
                photoId, pinned, std::move(mapLocation));
        });
    }

    bool GalleryCaptureService::RequestSetPanelPinned(
        std::string photoId, bool pinned)
    {
        if (photoId.empty()) return false;
        return StartWorker([photoId = std::move(photoId), pinned]() {
            return GalleryCatalog::GetSingleton().SetPanelPinned(photoId, pinned);
        });
    }

    bool GalleryCaptureService::RequestSetFavorite(
        std::string photoId, bool favorite)
    {
        if (photoId.empty()) return false;
        return StartWorker([photoId = std::move(photoId), favorite]() {
            return GalleryCatalog::GetSingleton().SetFavorite(photoId, favorite);
        });
    }

    bool GalleryCaptureService::StartWorker(std::function<bool()> operation)
    {
        if (_state != State::kIdle || !operation) return false;
        _workerFinished.store(false, std::memory_order_release);
        _workerSucceeded.store(false, std::memory_order_release);
        _captureOperation.store(false, std::memory_order_release);
        _state = State::kProcessing;
        _worker = std::jthread([this, operation = std::move(operation)]() mutable {
            bool succeeded = false;
            try {
                succeeded = operation();
            } catch (const std::exception& exception) {
                logger::error(
                    "DragonBoardVR: gallery background operation failed: {}",
                    exception.what());
            }
            _workerSucceeded.store(succeeded, std::memory_order_release);
            _workerFinished.store(true, std::memory_order_release);
        });
        return true;
    }

    std::optional<std::filesystem::path> GalleryCaptureService::WaitForProducedFile(
        std::filesystem::file_time_type requestedAt,
        std::stop_token stopToken)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(10);
        std::filesystem::path candidate;
        std::uintmax_t candidateSize = 0;
        int stablePolls = 0;
        while (!stopToken.stop_requested() &&
               std::chrono::steady_clock::now() < deadline) {
            std::error_code error;
            std::optional<std::filesystem::path> newest;
            std::filesystem::file_time_type newestTime{};
            for (const auto& entry :
                 std::filesystem::directory_iterator(GameDirectory(), error)) {
                if (error || !entry.is_regular_file(error) ||
                    !IsScreenshotCandidate(entry.path())) {
                    continue;
                }
                const auto writeTime = entry.last_write_time(error);
                if (error || writeTime + std::chrono::seconds(2) < requestedAt) {
                    error.clear();
                    continue;
                }
                if (!newest || writeTime > newestTime) {
                    newest = entry.path();
                    newestTime = writeTime;
                }
            }
            if (newest) {
                const auto size = std::filesystem::file_size(*newest, error);
                if (!error && size > 0) {
                    if (candidate == *newest && candidateSize == size) {
                        if (++stablePolls >= 2) return newest;
                    } else {
                        candidate = *newest;
                        candidateSize = size;
                        stablePolls = 0;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return std::nullopt;
    }

    bool GalleryCaptureService::ImportProducedFile(
        GalleryPhoto photo,
        const std::filesystem::path& source,
        std::uint32_t thumbnailWidth)
    {
        std::error_code error;
        std::filesystem::create_directories(GalleryCatalog::PhotosPath(), error);
        auto extension = source.extension();
        if (extension.empty()) extension = ".bmp";
        auto extensionText = extension.wstring();
        std::transform(
            extensionText.begin(),
            extensionText.end(),
            extensionText.begin(),
            ::towlower);
        extension = extensionText;
        const auto original = GalleryCatalog::PhotosPath() /
            (photo.id + "_original" + extension.string());
        std::filesystem::rename(source, original, error);
        if (error) {
            error.clear();
            std::filesystem::copy_file(
                source,
                original,
                std::filesystem::copy_options::overwrite_existing,
                error);
            if (error) {
                logger::error(
                    "DragonBoardVR: could not copy native screenshot '{}' into the mod gallery: {}",
                    source.string(),
                    error.message());
                return false;
            }
            std::filesystem::remove(source, error);
            if (error) {
                logger::warn(
                    "DragonBoardVR: screenshot was imported, but the original root file '{}' could not be removed: {}",
                    source.string(),
                    error.message());
                error.clear();
            }
        }

        auto display = original;
        if (extension != L".png") {
            const auto converted = GalleryCatalog::PhotosPath() /
                (photo.id + ".png");
            if (CreatePngPhoto(original, converted)) display = converted;
        }
        photo.originalPath = original.string();
        photo.imagePath = display.string();
        photo.thumbnailPath = display.string();
        const auto thumbnail = GalleryCatalog::PhotosPath() /
            (photo.id + "_thumb.png");
        if (CreateThumbnail(display, thumbnail, thumbnailWidth)) {
            photo.thumbnailPath = thumbnail.string();
        }
        if (!GalleryCatalog::GetSingleton().Add(std::move(photo))) return false;
        logger::info("DragonBoardVR: gallery screenshot imported from '{}'.", source.string());
        return true;
    }

    bool GalleryCaptureService::DeletePhotoFiles(std::string_view photoId)
    {
        auto deleted = GalleryCatalog::GetSingleton().Delete(photoId);
        if (!deleted) return false;
        std::array<std::filesystem::path, 3> paths{
            deleted->imagePath,
            deleted->thumbnailPath,
            deleted->originalPath
        };
        std::error_code error;
        for (std::size_t index = 0; index < paths.size(); ++index) {
            if (paths[index].empty()) continue;
            const bool duplicate = std::any_of(
                paths.begin(),
                paths.begin() + static_cast<std::ptrdiff_t>(index),
                [&](const auto& path) { return path == paths[index]; });
            if (duplicate) continue;
            std::filesystem::remove(paths[index], error);
            if (error) {
                logger::warn(
                    "DragonBoardVR: could not remove deleted gallery file '{}': {}",
                    paths[index].string(),
                    error.message());
                error.clear();
            }
        }
        return true;
    }

    void GalleryCaptureService::StartCaptureWorker()
    {
        const auto photo = std::exchange(_pendingPhoto, {});
        const auto requestedAt = _captureRequestedAt;
        const auto thumbnailWidth = static_cast<std::uint32_t>(std::clamp(
            vrui::VRUISettings::get().galleryThumbnailWidth, 64, 2048));
        _workerFinished.store(false, std::memory_order_release);
        _workerSucceeded.store(false, std::memory_order_release);
        _state = State::kProcessing;
        _worker = std::jthread(
            [this, photo, requestedAt, thumbnailWidth](std::stop_token stopToken) mutable {
                bool succeeded = false;
                try {
                    const auto produced = WaitForProducedFile(requestedAt, stopToken);
                    succeeded = produced &&
                        ImportProducedFile(
                            std::move(photo), *produced, thumbnailWidth);
                    if (!produced && !stopToken.stop_requested()) {
                        logger::warn(
                            "DragonBoardVR: timed out waiting for native screenshot output.");
                    }
                } catch (const std::exception& exception) {
                    logger::error(
                        "DragonBoardVR: gallery capture worker failed: {}",
                        exception.what());
                }
                _workerSucceeded.store(succeeded, std::memory_order_release);
                _workerFinished.store(true, std::memory_order_release);
            });
    }

    void GalleryCaptureService::FinishWorker()
    {
        if (_worker.joinable()) _worker.join();
        if (_workerSucceeded.exchange(false, std::memory_order_acq_rel)) {
            _catalogChanged.store(true, std::memory_order_release);
        }
        _workerFinished.store(false, std::memory_order_release);
        _captureOperation.store(false, std::memory_order_release);
        _state = State::kIdle;
    }

    void GalleryCaptureService::UpdateGameThread(float deltaTime)
    {
        (void)deltaTime;
        if (_state == State::kIdle) return;
        if (_state == State::kWaitingForEngine) {
            auto* controls = RE::MenuControls::GetSingleton();
            if (!controls || !controls->screenshotHandler || !controls->screenshotHandler->screenshotQueued) {
                StartCaptureWorker();
            }
        }
        if (_state == State::kProcessing &&
            _workerFinished.load(std::memory_order_acquire)) {
            FinishWorker();
        }
    }

    bool GalleryCaptureService::ConsumeCatalogChanged()
    {
        return _catalogChanged.exchange(false, std::memory_order_acq_rel);
    }

    void GalleryCaptureService::Reset()
    {
        if (_worker.joinable()) {
            _worker.request_stop();
            _worker.join();
        }
        _state = State::kIdle;
        _pendingPhoto = {};
        _captureRequestedAt = {};
        _workerFinished.store(false, std::memory_order_release);
        _workerSucceeded.store(false, std::memory_order_release);
        _captureOperation.store(false, std::memory_order_release);
    }
}
