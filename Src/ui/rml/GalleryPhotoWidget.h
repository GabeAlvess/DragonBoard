#pragma once

#include "ui/rml/RmlSurface.h"

#include <string>
#include <string_view>

namespace dragonboard::ui::rml
{
    class DragonBoardRmlRenderer;

    class GalleryPhotoWidget
    {
    public:
        bool Initialize(DragonBoardRmlRenderer* renderer, std::string contextName);
        void Shutdown();
        void SetPhoto(
            std::string_view imagePath,
            std::string_view location,
            std::string_view date);
        [[nodiscard]] bool IsDirty() const { return _surface.IsDirty(); }
        bool Render(ID3D11RenderTargetView* target, int width, int height);

    private:
        RmlSurface _surface;
    };
}
