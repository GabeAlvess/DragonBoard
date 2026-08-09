#include "pch.h"

#include "ui/rml/GalleryPhotoWidget.h"
#include "ui/rml/DragonBoardRmlRenderer.h"

#include <array>

#include <RmlUi/Core.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr int kLogicalWidth = 1024;
        constexpr int kLogicalHeight = 1080;
        constexpr std::array<const char*, 3> kDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/gallery_photo_pin.rml",
            "SKSE/Plugins/DragonBoardVR/ui/gallery_photo_pin.rml",
            "Assets/ui/rml/gallery_photo_pin.rml"
        };

        std::string EscapeRml(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value) {
                switch (character) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                default: escaped += character; break;
                }
            }
            return escaped;
        }

        void SetText(Rml::ElementDocument* document, const char* id, std::string_view text)
        {
            if (!document) return;
            if (auto* element = document->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(text));
            }
        }

        std::string FormatPhotoDateTime(std::string_view date)
        {
            if (date.size() < 5) return std::string(date);
            const auto isDigit = [](char value) { return value >= '0' && value <= '9'; };
            std::size_t timeIndex = std::string_view::npos;
            for (std::size_t index = date.size() - 4; index-- > 0;) {
                if (isDigit(date[index]) &&
                    isDigit(date[index + 1]) &&
                    date[index + 2] == ':' &&
                    isDigit(date[index + 3]) &&
                    isDigit(date[index + 4])) {
                    timeIndex = index;
                    break;
                }
            }
            if (timeIndex == std::string_view::npos) return std::string(date);

            auto day = date.substr(0, timeIndex);
            while (!day.empty() && (day.back() == ' ' || day.back() == '-')) {
                day.remove_suffix(1);
            }
            if (const auto comma = day.find(','); comma != std::string_view::npos) {
                day = day.substr(0, comma);
            }
            const auto time = date.substr(timeIndex, 5);
            if (day.empty()) return std::string(time);
            return std::string(day) + "  -  " + std::string(time);
        }
    }

    bool GalleryPhotoWidget::Initialize(
        DragonBoardRmlRenderer* renderer, std::string contextName)
    {
        return _surface.Initialize(
            renderer,
            std::move(contextName),
            kLogicalWidth,
            kLogicalHeight,
            { kDocumentCandidates[0], kDocumentCandidates[1], kDocumentCandidates[2] });
    }

    void GalleryPhotoWidget::Shutdown()
    {
        _surface.Shutdown();
    }

    void GalleryPhotoWidget::SetPhoto(
        std::string_view imagePath,
        std::string_view location,
        std::string_view date)
    {
        auto* document = _surface.GetDocument();
        if (!document) return;
        if (auto* image = document->GetElementById("photo-image")) {
            image->SetAttribute("src", std::string(imagePath));
        }
        SetText(document, "photo-location", location);
        SetText(document, "photo-date-time", FormatPhotoDateTime(date));
        _surface.MarkDirty();
    }

    bool GalleryPhotoWidget::Render(
        ID3D11RenderTargetView* target, int width, int height)
    {
        return _surface.Render(target, width, height);
    }
}
