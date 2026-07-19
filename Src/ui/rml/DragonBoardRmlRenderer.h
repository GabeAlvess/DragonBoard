#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace dragonboard::ui::rml
{
    class DragonBoardRmlRenderer final : public Rml::RenderInterface
    {
    public:
        DragonBoardRmlRenderer();
        ~DragonBoardRmlRenderer() override;

        bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
        void Shutdown();
        bool BeginFrame(ID3D11RenderTargetView* renderTarget, int width, int height);
        bool BeginFrame(
            ID3D11RenderTargetView* renderTarget,
            int renderWidth,
            int renderHeight,
            int logicalWidth,
            int logicalHeight);
        void EndFrame();
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] int GetDrawCallCount() const;

        Rml::CompiledGeometryHandle CompileGeometry(
            Rml::Span<const Rml::Vertex> vertices,
            Rml::Span<const int> indices) override;
        void RenderGeometry(
            Rml::CompiledGeometryHandle geometry,
            Rml::Vector2f translation,
            Rml::TextureHandle texture) override;
        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

        Rml::TextureHandle LoadTexture(
            Rml::Vector2i& textureDimensions,
            const Rml::String& source) override;
        Rml::TextureHandle GenerateTexture(
            Rml::Span<const Rml::byte> source,
            Rml::Vector2i sourceDimensions) override;
        void ReleaseTexture(Rml::TextureHandle texture) override;

        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
