#pragma once

#include "ui/rml/RmlSurface.h"

#include <cstdint>
#include <string>

namespace dragonboard::ui::rml
{
    class DragonBoardRmlRenderer;

    class StatusWidget
    {
    public:
        ~StatusWidget();

        bool Initialize(DragonBoardRmlRenderer* renderer);
        void Shutdown();
        void SetData(std::int32_t gold, float weight, float capacity, std::string location);
        void SetPointer(float u, float v, bool visible);
        [[nodiscard]] bool IsDirty() const { return _surface.IsDirty(); }
        bool Render(ID3D11RenderTargetView* target, int width, int height);

    private:
        RmlSurface _surface;
    };
}
