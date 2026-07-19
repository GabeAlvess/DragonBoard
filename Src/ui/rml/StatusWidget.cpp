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
        constexpr int kLogicalWidth = 250;
        constexpr int kLogicalHeight = 32;
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
        std::int32_t gold, float weight, float capacity, std::string location)
    {
        auto* document = _surface.GetDocument();
        SetText(document, "status-gold", std::to_string(gold));
        SetText(document, "status-weight", Rml::CreateString("%.1f / %.0f", weight, capacity));
        SetText(
            document,
            "status-location",
            EscapeRml(location.empty() ? "SKYRIM" : location));
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
