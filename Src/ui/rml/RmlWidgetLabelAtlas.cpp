#include "pch.h"

#include "ui/rml/RmlWidgetLabelAtlas.h"
#include "ui/rml/DragonBoardRmlRenderer.h"
#include "ui/rml/RmlSceneSurfaceUtils.h"
#include "vrui/VRUIWidget.h"

#include <array>

#include <d3d11.h>
#include <RmlUi/Core.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr float kScenePlaneExtent = 170.666656f;
        constexpr const char* kContextName = "dragonboard_widget_label_renderer";
        constexpr std::array<const char*, 3> kDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/widget_label_atlas.rml",
            "SKSE/Plugins/DragonBoardVR/ui/widget_label_atlas.rml",
            "Assets/ui/rml/widget_label_atlas.rml"
        };

        void LogTexturePixels(
            ID3D11Device* device,
            ID3D11Texture2D* texture,
            std::string_view id)
        {
            if (!device || !texture) return;

            D3D11_TEXTURE2D_DESC descriptor{};
            texture->GetDesc(&descriptor);
            descriptor.Usage = D3D11_USAGE_STAGING;
            descriptor.BindFlags = 0;
            descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            descriptor.MiscFlags = 0;

            ID3D11Texture2D* staging = nullptr;
            if (FAILED(device->CreateTexture2D(&descriptor, nullptr, &staging))) return;

            ID3D11DeviceContext* context = nullptr;
            device->GetImmediateContext(&context);
            if (!context) {
                staging->Release();
                return;
            }

            context->CopyResource(staging, texture);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                std::uint32_t visiblePixels = 0;
                std::uint8_t maximumAlpha = 0;
                for (std::uint32_t y = 0; y < descriptor.Height; ++y) {
                    const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch;
                    for (std::uint32_t x = 0; x < descriptor.Width; ++x) {
                        const auto alpha = row[static_cast<std::size_t>(x) * 4 + 3];
                        if (alpha != 0) ++visiblePixels;
                        maximumAlpha = (std::max)(maximumAlpha, alpha);
                    }
                }
                context->Unmap(staging, 0);
                logger::trace(
                    "DragonBoardVR: widget label '{}' target contains {} visible pixels "
                    "with max alpha {}.",
                    id,
                    visiblePixels,
                    maximumAlpha);
            }
            context->Release();
            staging->Release();
        }

    }

    RmlWidgetLabelAtlas::~RmlWidgetLabelAtlas()
    {
        Shutdown();
    }

    bool RmlWidgetLabelAtlas::Attach(
        std::string id,
        RE::NiNode* parent,
        std::string text,
        const RmlWidgetLabelPlacement& placement)
    {
        if (id.empty() || !parent) return false;

        std::scoped_lock lock(_mutex);
        if (auto existing = _entries.find(id); existing != _entries.end()) {
            auto& entry = existing->second;
            if (entry.root && entry.root->parent != parent) {
                if (entry.root->parent) entry.root->parent->DetachChild(entry.root.get());
                parent->AttachChild(entry.root.get());
            }
            entry.text = std::move(text);
            entry.dirty = true;
            ApplyPlacement(entry, placement);
            if (entry.root) entry.root->SetAppCulled(entry.text.empty());
            return true;
        }
        if (_entries.size() >= kCapacity) {
            logger::error(
                "DragonBoardVR: RmlUi widget label renderer is full ({} labels).",
                kCapacity);
            return false;
        }

        Entry entry;
        entry.text = std::move(text);
        entry.root = RE::NiPointer<RE::NiNode>(RE::NiNode::Create(1));
        entry.visual = vrui::VRUIWidget::loadModelFromNif(
            "DragonBoardVR\\WidgetLabelScreen.nif", false);
        if (!entry.root || !entry.visual) {
            logger::error(
                "DragonBoardVR: failed to create RmlUi widget label '{}'.",
                id);
            return false;
        }

        entry.root->name = "DragonBoardVR_RmlWidgetLabelRoot";
        entry.visual->name = "DragonBoardVR_RmlWidgetLabelVisual";
        ApplyPlacement(entry, placement);

        bool materialFound = false;
        RE::BSVisit::TraverseScenegraphGeometries(
            entry.visual.get(),
            [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
                auto* property = geometry ? geometry->lightingShaderProp_cast() : nullptr;
                auto* sourceMaterial = property ?
                    static_cast<RE::BSLightingShaderMaterialBase*>(
                        property->GetBaseMaterial()) :
                    nullptr;
                if (!property || !sourceMaterial || !sourceMaterial->diffuseTexture) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                auto sourceTexture = CreateIsolatedSourceTexture(
                    *sourceMaterial->diffuseTexture);
                auto* materialCopy = static_cast<RE::BSLightingShaderMaterialBase*>(
                    sourceMaterial->Create());
                if (!sourceTexture || !materialCopy) {
                    logger::error(
                        "DragonBoardVR: widget label '{}' could not isolate texture={} "
                        "or material={}.",
                        id,
                        sourceTexture != nullptr,
                        materialCopy != nullptr);
                    return RE::BSVisit::BSVisitControl::kStop;
                }

                materialCopy->CopyMembers(sourceMaterial);
                materialCopy->diffuseTexture = sourceTexture;
                materialCopy->texCoordOffset[0] = { 0.0f, 0.0f };
                materialCopy->texCoordScale[0] = { 1.0f, 1.0f };
                materialCopy->hashKey = (std::numeric_limits<std::uint32_t>::max)();
                property->SetMaterial(materialCopy, true);

                auto* installedMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(
                    property->GetBaseMaterial());
                if (!installedMaterial) {
                    logger::error(
                        "DragonBoardVR: widget label '{}' lost its material after installation.",
                        id);
                    return RE::BSVisit::BSVisitControl::kStop;
                }
                installedMaterial->diffuseTexture = sourceTexture;
                installedMaterial->texCoordOffset[0] = { 0.0f, 0.0f };
                installedMaterial->texCoordScale[0] = { 1.0f, 1.0f };
                installedMaterial->hashKey = (std::numeric_limits<std::uint32_t>::max)();
                property->InvalidateTextures(0);
                property->DoClearRenderPasses();

                entry.shaderProperty = RE::NiPointer<RE::BSLightingShaderProperty>(property);
                entry.sourceTexture = std::move(sourceTexture);
                entry.originalRendererTexture = entry.sourceTexture->rendererTexture;
                materialFound = true;
                logger::trace(
                    "DragonBoardVR: widget label '{}' attached property={} material={} "
                    "texture={}.",
                    id,
                    static_cast<const void*>(property),
                    static_cast<const void*>(installedMaterial),
                    static_cast<const void*>(entry.sourceTexture.get()));
                return RE::BSVisit::BSVisitControl::kStop;
            });
        if (!materialFound || !entry.sourceTexture) {
            logger::error(
                "DragonBoardVR: widget label '{}' has no usable isolated texture.",
                id);
            return false;
        }

        entry.root->AttachChild(entry.visual.get());
        entry.root->SetAppCulled(entry.text.empty());
        parent->AttachChild(entry.root.get());
        RE::NiUpdateData updateData;
        entry.root->Update(updateData);
        entry.root->UpdateWorldBound();

        _entries.emplace(std::move(id), std::move(entry));
        return true;
    }

    bool RmlWidgetLabelAtlas::SetText(std::string_view id, std::string text)
    {
        std::scoped_lock lock(_mutex);
        const auto found = _entries.find(std::string(id));
        if (found == _entries.end()) return false;
        if (found->second.text == text) return true;
        found->second.text = std::move(text);
        found->second.dirty = true;
        if (found->second.root) {
            found->second.root->SetAppCulled(found->second.text.empty());
        }
        return true;
    }

    void RmlWidgetLabelAtlas::UpdateGameThread()
    {
        std::scoped_lock lock(_mutex);
        for (auto& [id, entry] : _entries) {
            (void)id;
            if (!entry.sourceTexture || !entry.textureBridge || entry.textureBound) continue;
            entry.sourceTexture->rendererTexture = entry.textureBridge.get();
            if (entry.shaderProperty) {
                entry.shaderProperty->InvalidateTextures(0);
                entry.shaderProperty->DoClearRenderPasses();
            }
            entry.textureBound = true;
            logger::trace(
                "DragonBoardVR: widget label '{}' bound render texture {} (srv={}).",
                id,
                static_cast<const void*>(entry.renderTexture),
                static_cast<const void*>(entry.shaderResource));
        }
    }

    void RmlWidgetLabelAtlas::RenderPresentThread(
        ID3D11Device* device,
        DragonBoardRmlRenderer* renderer)
    {
        std::scoped_lock lock(_mutex);
        if (_entries.empty() || !EnsureSurfacePresentThread(renderer)) return;

        auto* document = _surface.GetDocument();
        auto* label = document ? document->GetElementById("widget-label-text") : nullptr;
        if (!label) return;

        for (auto& [id, entry] : _entries) {
            (void)id;
            if (!EnsureRenderTargetPresentThread(entry, device) || !entry.dirty) continue;
            label->SetInnerRML(EscapeRml(entry.text));
            _surface.MarkDirty();
            if (_surface.Render(
                    entry.renderTarget,
                    static_cast<int>(kTextureWidth),
                    static_cast<int>(kTextureHeight))) {
                entry.dirty = false;
                logger::trace(
                    "DragonBoardVR: widget label '{}' rendered text '{}' to {}x{} target {}.",
                    id,
                    entry.text,
                    kTextureWidth,
                    kTextureHeight,
                    static_cast<const void*>(entry.renderTarget));
                if (!entry.pixelsLogged) {
                    LogTexturePixels(device, entry.renderTexture, id);
                    entry.pixelsLogged = true;
                }
            }
        }
    }

    void RmlWidgetLabelAtlas::Shutdown()
    {
        std::scoped_lock lock(_mutex);
        _surface.Shutdown();
        for (auto& [id, entry] : _entries) {
            (void)id;
            if (entry.sourceTexture && entry.textureBound) {
                entry.sourceTexture->rendererTexture = entry.originalRendererTexture;
            }
            if (entry.root && entry.root->parent) {
                entry.root->parent->DetachChild(entry.root.get());
            }
            ReleaseRenderResources(entry);
        }
        _entries.clear();
    }

    bool RmlWidgetLabelAtlas::EnsureRenderTargetPresentThread(
        Entry& entry,
        ID3D11Device* device)
    {
        if (entry.renderTexture && entry.renderTarget && entry.shaderResource &&
            entry.textureBridge) {
            return true;
        }
        if (!device) return false;

        D3D11_TEXTURE2D_DESC descriptor{};
        descriptor.Width = kTextureWidth;
        descriptor.Height = kTextureHeight;
        descriptor.MipLevels = 1;
        descriptor.ArraySize = 1;
        descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descriptor.SampleDesc.Count = 1;
        descriptor.Usage = D3D11_USAGE_DEFAULT;
        descriptor.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(
                &descriptor, nullptr, &entry.renderTexture)) ||
            FAILED(device->CreateRenderTargetView(
                entry.renderTexture, nullptr, &entry.renderTarget)) ||
            FAILED(device->CreateShaderResourceView(
                entry.renderTexture, nullptr, &entry.shaderResource))) {
            ReleaseRenderResources(entry);
            logger::error("DragonBoardVR: failed to create a widget label render target.");
            return false;
        }

        entry.textureBridge = std::make_unique<RE::BSGraphics::Texture>();
        entry.textureBridge->texture = entry.renderTexture;
        entry.textureBridge->unk08 = 0;
        entry.textureBridge->resourceView = entry.shaderResource;
        return true;
    }

    bool RmlWidgetLabelAtlas::EnsureSurfacePresentThread(
        DragonBoardRmlRenderer* renderer)
    {
        return _surface.Initialize(
            renderer,
            kContextName,
            static_cast<int>(kTextureWidth),
            static_cast<int>(kTextureHeight),
            { kDocumentCandidates[0], kDocumentCandidates[1], kDocumentCandidates[2] });
    }

    std::string RmlWidgetLabelAtlas::EscapeRml(std::string_view text)
    {
        std::string escaped;
        escaped.reserve(text.size());
        for (const char character : text) {
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

    void RmlWidgetLabelAtlas::ApplyPlacement(
        Entry& entry,
        const RmlWidgetLabelPlacement& placement)
    {
        if (!entry.root || !entry.visual) return;
        entry.root->local.translate = placement.localPosition;
        entry.root->local.rotate = placement.localRotation;
        entry.root->local.scale = placement.localScale;

        RE::NiMatrix3 fit{};
        fit.entry[0][0] = placement.physicalWidth / kScenePlaneExtent;
        fit.entry[1][1] = placement.physicalHeight / kScenePlaneExtent;
        entry.visual->local.translate = {};
        entry.visual->local.rotate = fit;
        entry.visual->local.scale = 1.0f;
    }

    void RmlWidgetLabelAtlas::ReleaseRenderResources(Entry& entry)
    {
        entry.textureBridge.reset();
        if (entry.shaderResource) entry.shaderResource->Release();
        if (entry.renderTarget) entry.renderTarget->Release();
        if (entry.renderTexture) entry.renderTexture->Release();
        entry.shaderResource = nullptr;
        entry.renderTarget = nullptr;
        entry.renderTexture = nullptr;
        entry.textureBound = false;
    }
}
