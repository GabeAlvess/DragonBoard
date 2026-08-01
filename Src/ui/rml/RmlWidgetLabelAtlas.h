#pragma once

#include "ui/rml/RmlSurface.h"

#include <RE/Skyrim.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

struct ID3D11Device;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace dragonboard::ui::rml
{
    class DragonBoardRmlRenderer;

    struct RmlWidgetLabelPlacement
    {
        RE::NiPoint3 localPosition{};
        RE::NiMatrix3 localRotation{};
        float localScale = 1.0f;
        float physicalWidth = 2.5f;
        float physicalHeight = 0.75f;
    };

    class RmlWidgetLabelAtlas
    {
    public:
        ~RmlWidgetLabelAtlas();

        bool Attach(
            std::string id,
            RE::NiNode* parent,
            std::string text,
            const RmlWidgetLabelPlacement& placement);
        bool SetText(std::string_view id, std::string text);
        void UpdateGameThread();
        void RenderPresentThread(
            ID3D11Device* device,
            DragonBoardRmlRenderer* renderer);
        void Shutdown();

    private:
        static constexpr std::size_t kCapacity = 32;
        static constexpr std::uint32_t kTextureWidth = 256;
        static constexpr std::uint32_t kTextureHeight = 64;

        struct Entry
        {
            std::string text;
            RE::NiPointer<RE::NiNode> root;
            RE::NiPointer<RE::NiNode> visual;
            RE::NiPointer<RE::BSLightingShaderProperty> shaderProperty;
            RE::NiPointer<RE::NiSourceTexture> sourceTexture;
            RE::BSGraphics::Texture* originalRendererTexture = nullptr;
            ID3D11Texture2D* renderTexture = nullptr;
            ID3D11RenderTargetView* renderTarget = nullptr;
            ID3D11ShaderResourceView* shaderResource = nullptr;
            std::unique_ptr<RE::BSGraphics::Texture> textureBridge;
            bool textureBound = false;
            bool pixelsLogged = false;
            bool dirty = true;
        };

        bool EnsureRenderTargetPresentThread(Entry& entry, ID3D11Device* device);
        bool EnsureSurfacePresentThread(DragonBoardRmlRenderer* renderer);
        static std::string EscapeRml(std::string_view text);
        static void ApplyPlacement(
            Entry& entry,
            const RmlWidgetLabelPlacement& placement);
        static void ReleaseRenderResources(Entry& entry);

        std::mutex _mutex;
        std::unordered_map<std::string, Entry> _entries;
        RmlSurface _surface;
    };
}
