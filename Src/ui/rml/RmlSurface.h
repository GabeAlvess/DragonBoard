#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

struct ID3D11RenderTargetView;

namespace Rml
{
    class Context;
    class ElementDocument;
}

namespace dragonboard::ui::rml
{
    class DragonBoardRmlRenderer;

    class RmlSurface
    {
    public:
        ~RmlSurface();

        bool Initialize(
            DragonBoardRmlRenderer* renderer,
            std::string contextName,
            int logicalWidth,
            int logicalHeight,
            std::initializer_list<const char*> documentCandidates);
        void Shutdown();
        void SetPointer(float u, float v, bool visible);
        void MarkDirty() { _dirty = true; }
        [[nodiscard]] bool IsDirty() const { return _dirty; }
        [[nodiscard]] Rml::ElementDocument* GetDocument() const { return _document; }
        bool Render(ID3D11RenderTargetView* target, int width, int height);

    private:
        DragonBoardRmlRenderer* _renderer = nullptr;
        Rml::Context* _context = nullptr;
        Rml::ElementDocument* _document = nullptr;
        std::string _contextName;
        int _logicalWidth = 0;
        int _logicalHeight = 0;
        bool _dirty = true;
        std::uint32_t _renderCount = 0;
        int _pointerX = -1;
        int _pointerY = -1;
        bool _pointerVisible = false;
    };
}
