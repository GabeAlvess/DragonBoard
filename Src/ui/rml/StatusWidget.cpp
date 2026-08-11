#include "pch.h"

#include "ui/rml/StatusWidget.h"
#include "ui/rml/DragonBoardRmlRenderer.h"

#include <array>

#include <RmlUi/Core.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr const char* kContextName = "dragonboard_status_surface";
        constexpr int kLogicalWidth = 950;
        constexpr int kLogicalHeight = 80;
        constexpr std::array<const char*, 3> kDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/status_widget.rml",
            "SKSE/Plugins/DragonBoardVR/ui/status_widget.rml",
            "Assets/ui/rml/status_widget.rml"
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
                default: escaped += character; break;
                }
            }
            return escaped;
        }

        void SetText(Rml::ElementDocument* document, const char* id, const std::string& text)
        {
            if (!document) return;
            if (auto* element = document->GetElementById(id)) element->SetInnerRML(text);
        }
    }

    StatusWidget::~StatusWidget()
    = default;

    bool StatusWidget::Initialize(DragonBoardRmlRenderer* renderer)
    {
        return _surface.Initialize(
            renderer,
            kContextName,
            kLogicalWidth,
            kLogicalHeight,
            { kDocumentCandidates[0], kDocumentCandidates[1], kDocumentCandidates[2] });
    }

    void StatusWidget::Shutdown()
    {
        _surface.Shutdown();
    }

    void StatusWidget::SetData(
        std::string name,
        std::uint16_t level,
        std::int32_t gold,
        float weight,
        float capacity)
    {
        auto* document = _surface.GetDocument();
        SetText(document, "status-name", EscapeRml(name.empty() ? "DRAGONBORN" : name));
        SetText(document, "status-level", std::to_string(level));
        SetText(document, "status-gold", std::to_string(gold));
        SetText(document, "status-weight-current", Rml::CreateString("%.0f", weight));
        SetText(document, "status-weight-capacity", Rml::CreateString("%.0f", capacity));
        if (document) {
            if (auto* currentWeight = document->GetElementById("status-weight-current")) {
                currentWeight->SetClass("overloaded", weight > capacity);
            }
        }
        _surface.MarkDirty();
    }

    void StatusWidget::SetPointer(float u, float v, bool visible)
    {
        _surface.SetPointer(u, v, visible);
    }

    bool StatusWidget::Render(ID3D11RenderTargetView* target, int width, int height)
    {
        return _surface.Render(target, width, height);
    }
}
