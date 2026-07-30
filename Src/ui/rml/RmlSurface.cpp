#include "pch.h"

#include "ui/rml/RmlSurface.h"
#include "ui/rml/DragonBoardRmlRenderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <RmlUi/Core.h>

namespace dragonboard::ui::rml
{
    RmlSurface::~RmlSurface()
    {
        Shutdown();
    }

    bool RmlSurface::Initialize(
        DragonBoardRmlRenderer* renderer,
        std::string contextName,
        int logicalWidth,
        int logicalHeight,
        std::initializer_list<const char*> documentCandidates)
    {
        if (_context && _document && _renderer == renderer &&
            _contextName == contextName && _logicalWidth == logicalWidth &&
            _logicalHeight == logicalHeight) {
            return true;
        }
        Shutdown();
        if (!renderer || !renderer->IsReady() || contextName.empty() ||
            logicalWidth <= 0 || logicalHeight <= 0) {
            return false;
        }

        _renderer = renderer;
        _contextName = std::move(contextName);
        _logicalWidth = logicalWidth;
        _logicalHeight = logicalHeight;
        _context = Rml::CreateContext(
            _contextName, Rml::Vector2i(_logicalWidth, _logicalHeight), renderer);
        if (!_context) {
            Shutdown();
            return false;
        }
        for (const auto* path : documentCandidates) {
            if (!path || !std::filesystem::exists(path)) continue;
            _document = _context->LoadDocument(path);
            if (_document) break;
        }
        if (!_document) {
            logger::error(
                "DragonBoardVR: independent RmlUi document for context '{}' could not load.",
                _contextName);
            Shutdown();
            return false;
        }
        _document->Show();
        _dirty = true;
        logger::info(
            "DragonBoardVR: independent RmlUi context '{}' initialized ({}x{}).",
            _contextName,
            _logicalWidth,
            _logicalHeight);
        return true;
    }

    void RmlSurface::Shutdown()
    {
        _document = nullptr;
        if (_context && !_contextName.empty()) {
            Rml::RemoveContext(_contextName);
        }
        _context = nullptr;
        _renderer = nullptr;
        _contextName.clear();
        _logicalWidth = 0;
        _logicalHeight = 0;
        _dirty = true;
        _renderCount = 0;
        _pointerX = -1;
        _pointerY = -1;
        _pointerVisible = false;
    }

    void RmlSurface::SetPointer(float u, float v, bool visible)
    {
        auto* cursor = _document ? _document->GetElementById("vr-cursor") : nullptr;
        if (!cursor || _logicalWidth <= 0 || _logicalHeight <= 0) return;
        const int x = std::clamp(
            static_cast<int>(std::lround(u * static_cast<float>(_logicalWidth - 1))),
            0,
            _logicalWidth - 1);
        const int y = std::clamp(
            static_cast<int>(std::lround(v * static_cast<float>(_logicalHeight - 1))),
            0,
            _logicalHeight - 1);
        if (_pointerVisible == visible && (!visible || (_pointerX == x && _pointerY == y))) {
            return;
        }
        _pointerVisible = visible;
        _pointerX = x;
        _pointerY = y;
        cursor->SetProperty("display", visible ? "block" : "none");
        if (visible) {
            cursor->SetProperty("left", std::to_string(x) + "px");
            cursor->SetProperty("top", std::to_string(y) + "px");
        }
        _dirty = true;
    }

    bool RmlSurface::Render(
        ID3D11RenderTargetView* target, int width, int height)
    {
        if (!_context || !_document || !_renderer || !target || !_dirty) return false;
        if (!_context->Update()) return false;
        if (!_renderer->BeginFrame(
                target, width, height, _logicalWidth, _logicalHeight)) {
            return false;
        }
        bool rendered = false;
        try {
            rendered = _context->Render();
            _renderer->EndFrame();
        } catch (...) {
            _renderer->EndFrame();
            throw;
        }
        if (rendered) {
            _dirty = false;
            ++_renderCount;
            if (_renderCount <= 3) {
                logger::trace(
                    "DragonBoardVR: independent surface '{}' rendered frame {} "
                    "to target {} ({}x{}).",
                    _contextName,
                    _renderCount,
                    static_cast<const void*>(target),
                    width,
                    height);
            }
        }
        return rendered;
    }
}
