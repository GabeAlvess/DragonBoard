#include "ui/rml/DragonBoardRmlRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        struct GeometryData
        {
            ComPtr<ID3D11Buffer> vertices;
            ComPtr<ID3D11Buffer> indices;
            UINT indexCount = 0;
        };

        struct TextureData
        {
            ComPtr<ID3D11Texture2D> texture;
            ComPtr<ID3D11ShaderResourceView> srv;
        };

        constexpr char kVertexShader[] = R"(
cbuffer Constants : register(b0)
{
    float2 viewport_size;
    float2 translation;
};

struct VSInput
{
    float2 position : POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

PSInput main(VSInput input)
{
    PSInput output;
    float2 pixel = input.position + translation;
    output.position = float4(
        pixel.x * (2.0 / viewport_size.x) - 1.0,
        1.0 - pixel.y * (2.0 / viewport_size.y),
        0.0,
        1.0);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}
)";

        constexpr char kPixelShader[] = R"(
Texture2D ui_texture : register(t0);
SamplerState ui_sampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return ui_texture.Sample(ui_sampler, input.texcoord) * input.color;
}
)";

        bool CompileHlslShader(
            const char* source,
            const char* profile,
            ID3DBlob** bytecode)
        {
            ComPtr<ID3DBlob> errors;
            const HRESULT result = D3DCompile(
                source,
                std::strlen(source),
                nullptr,
                nullptr,
                nullptr,
                "main",
                profile,
                D3DCOMPILE_ENABLE_STRICTNESS,
                0,
                bytecode,
                errors.GetAddressOf());
            if (FAILED(result)) {
                const char* message = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error";
                logger::error("DragonBoardVR: RmlUi shader compilation failed: {}", message);
                return false;
            }
            return true;
        }

        std::wstring Utf8ToWide(const Rml::String& value)
        {
            if (value.empty()) return {};
            const int length = MultiByteToWideChar(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (length <= 0) return {};
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
            return result;
        }

        std::filesystem::path ResolveTexturePath(const Rml::String& source)
        {
            const auto wideSource = Utf8ToWide(source);
            if (wideSource.empty()) return {};

            const std::filesystem::path requested(wideSource);
            std::error_code error;
            if (requested.is_absolute() || std::filesystem::is_regular_file(requested, error)) {
                return requested;
            }

            const auto currentDirectory = std::filesystem::current_path(error);
            if (error) return requested;
            const std::array<std::filesystem::path, 4> roots{
                currentDirectory / "Assets" / "ui" / "rml",
                currentDirectory / "Data" / "SKSE" / "Plugins" / "DragonBoardVR" / "ui",
                currentDirectory / "SKSE" / "Plugins" / "DragonBoardVR" / "ui",
                currentDirectory / "install_output" / "SKSE" / "Plugins" / "DragonBoardVR" / "ui"
            };
            for (const auto& root : roots) {
                const auto candidate = root / requested;
                error.clear();
                if (std::filesystem::is_regular_file(candidate, error)) return candidate;
            }
            return requested;
        }
    }

    struct DragonBoardRmlRenderer::Impl
    {
        struct Constants
        {
            float viewport[2];
            float translation[2];
        };

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11InputLayout> inputLayout;
        ComPtr<ID3D11Buffer> constants;
        ComPtr<ID3D11BlendState> blendState;
        ComPtr<ID3D11RasterizerState> rasterState;
        ComPtr<ID3D11DepthStencilState> depthState;
        ComPtr<ID3D11SamplerState> sampler;
        TextureData whiteTexture;

        int renderWidth = 0;
        int renderHeight = 0;
        int logicalWidth = 0;
        int logicalHeight = 0;
        bool scissorEnabled = false;
        int drawCallCount = 0;
        Rml::Rectanglei scissorRegion{};
        bool frameActive = false;

        ComPtr<ID3D11RenderTargetView> oldRtv;
        ComPtr<ID3D11DepthStencilView> oldDsv;
        D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT oldViewportCount = 0;
        D3D11_RECT oldScissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT oldScissorCount = 0;
        ComPtr<ID3D11RasterizerState> oldRasterState;
        ComPtr<ID3D11BlendState> oldBlendState;
        FLOAT oldBlendFactor[4]{};
        UINT oldSampleMask = 0;
        ComPtr<ID3D11DepthStencilState> oldDepthState;
        UINT oldStencilRef = 0;
        ComPtr<ID3D11InputLayout> oldInputLayout;
        ComPtr<ID3D11Buffer> oldVertexBuffer;
        UINT oldVertexStride = 0;
        UINT oldVertexOffset = 0;
        ComPtr<ID3D11Buffer> oldIndexBuffer;
        DXGI_FORMAT oldIndexFormat = DXGI_FORMAT_UNKNOWN;
        UINT oldIndexOffset = 0;
        D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ComPtr<ID3D11VertexShader> oldVertexShader;
        ComPtr<ID3D11PixelShader> oldPixelShader;
        ComPtr<ID3D11GeometryShader> oldGeometryShader;
        ComPtr<ID3D11HullShader> oldHullShader;
        ComPtr<ID3D11DomainShader> oldDomainShader;
        ComPtr<ID3D11Buffer> oldVsConstantBuffer;
        ComPtr<ID3D11ShaderResourceView> oldPsSrv;
        ComPtr<ID3D11SamplerState> oldPsSampler;

        void ResetBackup()
        {
            oldRtv.Reset();
            oldDsv.Reset();
            oldRasterState.Reset();
            oldBlendState.Reset();
            oldDepthState.Reset();
            oldInputLayout.Reset();
            oldVertexBuffer.Reset();
            oldIndexBuffer.Reset();
            oldVertexShader.Reset();
            oldPixelShader.Reset();
            oldGeometryShader.Reset();
            oldHullShader.Reset();
            oldDomainShader.Reset();
            oldVsConstantBuffer.Reset();
            oldPsSrv.Reset();
            oldPsSampler.Reset();
        }
    };

    DragonBoardRmlRenderer::DragonBoardRmlRenderer() :
        _impl(std::make_unique<Impl>())
    {}

    DragonBoardRmlRenderer::~DragonBoardRmlRenderer()
    {
        Shutdown();
    }

    bool DragonBoardRmlRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        if (IsReady()) return true;
        if (!device || !context) return false;

        _impl->device = device;
        _impl->context = context;

        ComPtr<ID3DBlob> vertexBytecode;
        ComPtr<ID3DBlob> pixelBytecode;
        if (!CompileHlslShader(kVertexShader, "vs_4_0", vertexBytecode.GetAddressOf()) ||
            !CompileHlslShader(kPixelShader, "ps_4_0", pixelBytecode.GetAddressOf())) {
            Shutdown();
            return false;
        }

        if (FAILED(device->CreateVertexShader(
                vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(), nullptr,
                _impl->vertexShader.GetAddressOf())) ||
            FAILED(device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(), nullptr,
                _impl->pixelShader.GetAddressOf()))) {
            Shutdown();
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Rml::Vertex, colour), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, tex_coord), D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        if (FAILED(device->CreateInputLayout(
                inputElements,
                static_cast<UINT>(std::size(inputElements)),
                vertexBytecode->GetBufferPointer(),
                vertexBytecode->GetBufferSize(),
                _impl->inputLayout.GetAddressOf()))) {
            Shutdown();
            return false;
        }

        D3D11_BUFFER_DESC constantsDesc{};
        constantsDesc.ByteWidth = sizeof(Impl::Constants);
        constantsDesc.Usage = D3D11_USAGE_DEFAULT;
        constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&constantsDesc, nullptr, _impl->constants.GetAddressOf()))) {
            Shutdown();
            return false;
        }

        D3D11_BLEND_DESC blendDesc{};
        auto& blend = blendDesc.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D11_BLEND_ONE;
        blend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D11_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.ScissorEnable = TRUE;
        rasterDesc.DepthClipEnable = TRUE;

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = FALSE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        if (FAILED(device->CreateBlendState(&blendDesc, _impl->blendState.GetAddressOf())) ||
            FAILED(device->CreateRasterizerState(&rasterDesc, _impl->rasterState.GetAddressOf())) ||
            FAILED(device->CreateDepthStencilState(&depthDesc, _impl->depthState.GetAddressOf())) ||
            FAILED(device->CreateSamplerState(&samplerDesc, _impl->sampler.GetAddressOf()))) {
            Shutdown();
            return false;
        }

        const std::uint32_t whitePixel = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = 1;
        textureDesc.Height = 1;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA textureData{};
        textureData.pSysMem = &whitePixel;
        textureData.SysMemPitch = sizeof(whitePixel);
        if (FAILED(device->CreateTexture2D(&textureDesc, &textureData, _impl->whiteTexture.texture.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(
                _impl->whiteTexture.texture.Get(), nullptr, _impl->whiteTexture.srv.GetAddressOf()))) {
            Shutdown();
            return false;
        }

        return true;
    }

    void DragonBoardRmlRenderer::Shutdown()
    {
        if (!_impl) return;
        if (_impl->frameActive) EndFrame();
        _impl->whiteTexture = {};
        _impl->sampler.Reset();
        _impl->depthState.Reset();
        _impl->rasterState.Reset();
        _impl->blendState.Reset();
        _impl->constants.Reset();
        _impl->inputLayout.Reset();
        _impl->pixelShader.Reset();
        _impl->vertexShader.Reset();
        _impl->context.Reset();
        _impl->device.Reset();
    }

    bool DragonBoardRmlRenderer::BeginFrame(
        ID3D11RenderTargetView* renderTarget, int width, int height)
    {
        return BeginFrame(renderTarget, width, height, width, height);
    }

    bool DragonBoardRmlRenderer::BeginFrame(
        ID3D11RenderTargetView* renderTarget,
        int renderWidth,
        int renderHeight,
        int logicalWidth,
        int logicalHeight)
    {
        if (!IsReady() || !renderTarget || renderWidth <= 0 || renderHeight <= 0 ||
            logicalWidth <= 0 || logicalHeight <= 0 || _impl->frameActive) {
            return false;
        }
        _impl->drawCallCount = 0;

        auto* context = _impl->context.Get();
        _impl->renderWidth = renderWidth;
        _impl->renderHeight = renderHeight;
        _impl->logicalWidth = logicalWidth;
        _impl->logicalHeight = logicalHeight;
        _impl->frameActive = true;

        context->OMGetRenderTargets(1, _impl->oldRtv.GetAddressOf(), _impl->oldDsv.GetAddressOf());
        _impl->oldViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        context->RSGetViewports(&_impl->oldViewportCount, _impl->oldViewports);
        _impl->oldScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        context->RSGetScissorRects(&_impl->oldScissorCount, _impl->oldScissors);
        context->RSGetState(_impl->oldRasterState.GetAddressOf());
        context->OMGetBlendState(
            _impl->oldBlendState.GetAddressOf(), _impl->oldBlendFactor, &_impl->oldSampleMask);
        context->OMGetDepthStencilState(_impl->oldDepthState.GetAddressOf(), &_impl->oldStencilRef);
        context->IAGetInputLayout(_impl->oldInputLayout.GetAddressOf());
        context->IAGetVertexBuffers(
            0, 1, _impl->oldVertexBuffer.GetAddressOf(), &_impl->oldVertexStride, &_impl->oldVertexOffset);
        context->IAGetIndexBuffer(
            _impl->oldIndexBuffer.GetAddressOf(), &_impl->oldIndexFormat, &_impl->oldIndexOffset);
        context->IAGetPrimitiveTopology(&_impl->oldTopology);
        context->VSGetShader(_impl->oldVertexShader.GetAddressOf(), nullptr, nullptr);
        context->PSGetShader(_impl->oldPixelShader.GetAddressOf(), nullptr, nullptr);
        context->GSGetShader(_impl->oldGeometryShader.GetAddressOf(), nullptr, nullptr);
        context->HSGetShader(_impl->oldHullShader.GetAddressOf(), nullptr, nullptr);
        context->DSGetShader(_impl->oldDomainShader.GetAddressOf(), nullptr, nullptr);
        context->VSGetConstantBuffers(0, 1, _impl->oldVsConstantBuffer.GetAddressOf());
        context->PSGetShaderResources(0, 1, _impl->oldPsSrv.GetAddressOf());
        context->PSGetSamplers(0, 1, _impl->oldPsSampler.GetAddressOf());

        context->OMSetRenderTargets(1, &renderTarget, nullptr);
        const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        context->ClearRenderTargetView(renderTarget, clear);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(renderWidth);
        viewport.Height = static_cast<float>(renderHeight);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(_impl->rasterState.Get());
        context->OMSetBlendState(_impl->blendState.Get(), nullptr, 0xFFFFFFFFu);
        context->OMSetDepthStencilState(_impl->depthState.Get(), 0);
        context->IASetInputLayout(_impl->inputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(_impl->vertexShader.Get(), nullptr, 0);
        context->PSSetShader(_impl->pixelShader.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        auto* constants = _impl->constants.Get();
        auto* sampler = _impl->sampler.Get();
        context->VSSetConstantBuffers(0, 1, &constants);
        context->PSSetSamplers(0, 1, &sampler);
        EnableScissorRegion(false);
        return true;
    }

    void DragonBoardRmlRenderer::EndFrame()
    {
        if (!_impl || !_impl->frameActive || !_impl->context) return;
        auto* context = _impl->context.Get();

        auto* oldRtv = _impl->oldRtv.Get();
        context->OMSetRenderTargets(1, &oldRtv, _impl->oldDsv.Get());
        if (_impl->oldViewportCount > 0) {
            context->RSSetViewports(_impl->oldViewportCount, _impl->oldViewports);
        }
        if (_impl->oldScissorCount > 0) {
            context->RSSetScissorRects(_impl->oldScissorCount, _impl->oldScissors);
        }
        context->RSSetState(_impl->oldRasterState.Get());
        context->OMSetBlendState(
            _impl->oldBlendState.Get(), _impl->oldBlendFactor, _impl->oldSampleMask);
        context->OMSetDepthStencilState(_impl->oldDepthState.Get(), _impl->oldStencilRef);
        context->IASetInputLayout(_impl->oldInputLayout.Get());
        auto* oldVertexBuffer = _impl->oldVertexBuffer.Get();
        context->IASetVertexBuffers(
            0, 1, &oldVertexBuffer, &_impl->oldVertexStride, &_impl->oldVertexOffset);
        context->IASetIndexBuffer(
            _impl->oldIndexBuffer.Get(), _impl->oldIndexFormat, _impl->oldIndexOffset);
        context->IASetPrimitiveTopology(_impl->oldTopology);
        context->VSSetShader(_impl->oldVertexShader.Get(), nullptr, 0);
        context->PSSetShader(_impl->oldPixelShader.Get(), nullptr, 0);
        context->GSSetShader(_impl->oldGeometryShader.Get(), nullptr, 0);
        context->HSSetShader(_impl->oldHullShader.Get(), nullptr, 0);
        context->DSSetShader(_impl->oldDomainShader.Get(), nullptr, 0);
        auto* oldVsCb = _impl->oldVsConstantBuffer.Get();
        auto* oldPsSrv = _impl->oldPsSrv.Get();
        auto* oldPsSampler = _impl->oldPsSampler.Get();
        context->VSSetConstantBuffers(0, 1, &oldVsCb);
        context->PSSetShaderResources(0, 1, &oldPsSrv);
        context->PSSetSamplers(0, 1, &oldPsSampler);

        _impl->frameActive = false;
        _impl->ResetBackup();
    }

    bool DragonBoardRmlRenderer::IsReady() const
    {
        return _impl && _impl->device && _impl->context && _impl->vertexShader &&
               _impl->pixelShader && _impl->inputLayout;
    }

    int DragonBoardRmlRenderer::GetDrawCallCount() const
    {
        return _impl ? _impl->drawCallCount : 0;
    }

    Rml::CompiledGeometryHandle DragonBoardRmlRenderer::CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices)
    {
        if (!IsReady() || vertices.empty() || indices.empty()) return {};
        auto geometry = std::make_unique<GeometryData>();

        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Rml::Vertex));
        vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertexData{};
        vertexData.pSysMem = vertices.data();

        D3D11_BUFFER_DESC indexDesc{};
        indexDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(int));
        indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indexData{};
        indexData.pSysMem = indices.data();

        if (FAILED(_impl->device->CreateBuffer(
                &vertexDesc, &vertexData, geometry->vertices.GetAddressOf())) ||
            FAILED(_impl->device->CreateBuffer(
                &indexDesc, &indexData, geometry->indices.GetAddressOf()))) {
            return {};
        }
        geometry->indexCount = static_cast<UINT>(indices.size());
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }

    void DragonBoardRmlRenderer::RenderGeometry(
        Rml::CompiledGeometryHandle geometryHandle,
        Rml::Vector2f translation,
        Rml::TextureHandle textureHandle)
    {
        if (!_impl->frameActive || !geometryHandle) return;
        auto* geometry = reinterpret_cast<GeometryData*>(geometryHandle);
        auto* texture = textureHandle ? reinterpret_cast<TextureData*>(textureHandle) : nullptr;

        Impl::Constants constants{
            { static_cast<float>(_impl->logicalWidth), static_cast<float>(_impl->logicalHeight) },
            { translation.x, translation.y }
        };
        _impl->context->UpdateSubresource(_impl->constants.Get(), 0, nullptr, &constants, 0, 0);

        const UINT stride = sizeof(Rml::Vertex);
        const UINT offset = 0;
        auto* vertexBuffer = geometry->vertices.Get();
        auto* srv = texture ? texture->srv.Get() : _impl->whiteTexture.srv.Get();
        _impl->context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        _impl->context->IASetIndexBuffer(geometry->indices.Get(), DXGI_FORMAT_R32_UINT, 0);
        _impl->context->PSSetShaderResources(0, 1, &srv);

        D3D11_RECT scissor{};
        if (_impl->scissorEnabled) {
            const float scaleX = static_cast<float>(_impl->renderWidth) /
                static_cast<float>(_impl->logicalWidth);
            const float scaleY = static_cast<float>(_impl->renderHeight) /
                static_cast<float>(_impl->logicalHeight);
            scissor.left = std::clamp(
                static_cast<LONG>(std::floor(_impl->scissorRegion.Left() * scaleX)),
                0L,
                static_cast<LONG>(_impl->renderWidth));
            scissor.top = std::clamp(
                static_cast<LONG>(std::floor(_impl->scissorRegion.Top() * scaleY)),
                0L,
                static_cast<LONG>(_impl->renderHeight));
            scissor.right = std::clamp<LONG>(
                static_cast<LONG>(std::ceil(_impl->scissorRegion.Right() * scaleX)),
                scissor.left,
                static_cast<LONG>(_impl->renderWidth));
            scissor.bottom = std::clamp<LONG>(
                static_cast<LONG>(std::ceil(_impl->scissorRegion.Bottom() * scaleY)),
                scissor.top,
                static_cast<LONG>(_impl->renderHeight));
        } else {
            scissor.right = _impl->renderWidth;
            scissor.bottom = _impl->renderHeight;
        }
        _impl->context->RSSetScissorRects(1, &scissor);
        _impl->context->DrawIndexed(geometry->indexCount, 0, 0);
        ++_impl->drawCallCount;
    }

    void DragonBoardRmlRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
    {
        delete reinterpret_cast<GeometryData*>(geometry);
    }

    Rml::TextureHandle DragonBoardRmlRenderer::LoadTexture(
        Rml::Vector2i& textureDimensions,
        const Rml::String& source)
    {
        textureDimensions = {};
        if (!IsReady() || source.empty()) return {};

        const auto imagePath = ResolveTexturePath(source);
        if (imagePath.empty()) {
            logger::warn("DragonBoardVR: RmlUi could not decode image path '{}'.", source);
            return {};
        }

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool releaseCom = SUCCEEDED(comResult);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            logger::warn("DragonBoardVR: COM initialization failed for RmlUi image '{}'.", source);
            return {};
        }

        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        UINT width = 0;
        UINT height = 0;
        std::vector<Rml::byte> pixels;

        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf()));
        if (SUCCEEDED(result)) {
            result = factory->CreateDecoderFromFilename(
                imagePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
        }
        if (SUCCEEDED(result)) result = decoder->GetFrame(0, frame.GetAddressOf());
        if (SUCCEEDED(result)) result = frame->GetSize(&width, &height);
        if (SUCCEEDED(result)) result = factory->CreateFormatConverter(converter.GetAddressOf());
        if (SUCCEEDED(result)) {
            result = converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom);
        }
        if (SUCCEEDED(result) && width > 0 && height > 0) {
            const UINT stride = width * 4;
            pixels.resize(static_cast<std::size_t>(stride) * height);
            result = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        }

        Rml::TextureHandle texture{};
        if (SUCCEEDED(result) && !pixels.empty()) {
            textureDimensions = {
                static_cast<int>(width),
                static_cast<int>(height)
            };
            texture = GenerateTexture(pixels, textureDimensions);
        }
        if (!texture) {
            textureDimensions = {};
            logger::warn(
                "DragonBoardVR: failed to load RmlUi image texture '{}' (resolved='{}'): 0x{:08X}.",
                source, imagePath.string(), static_cast<unsigned long>(result));
        }

        converter.Reset();
        frame.Reset();
        decoder.Reset();
        factory.Reset();
        if (releaseCom) CoUninitialize();
        return texture;
    }

    Rml::TextureHandle DragonBoardRmlRenderer::GenerateTexture(
        Rml::Span<const Rml::byte> source,
        Rml::Vector2i sourceDimensions)
    {
        if (!IsReady() || source.empty() || sourceDimensions.x <= 0 || sourceDimensions.y <= 0) return {};
        auto texture = std::make_unique<TextureData>();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(sourceDimensions.x);
        desc.Height = static_cast<UINT>(sourceDimensions.y);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = source.data();
        data.SysMemPitch = static_cast<UINT>(sourceDimensions.x * 4);
        if (FAILED(_impl->device->CreateTexture2D(&desc, &data, texture->texture.GetAddressOf())) ||
            FAILED(_impl->device->CreateShaderResourceView(
                texture->texture.Get(), nullptr, texture->srv.GetAddressOf()))) {
            return {};
        }
        return reinterpret_cast<Rml::TextureHandle>(texture.release());
    }

    void DragonBoardRmlRenderer::ReleaseTexture(Rml::TextureHandle texture)
    {
        delete reinterpret_cast<TextureData*>(texture);
    }

    void DragonBoardRmlRenderer::EnableScissorRegion(bool enable)
    {
        _impl->scissorEnabled = enable;
    }

    void DragonBoardRmlRenderer::SetScissorRegion(Rml::Rectanglei region)
    {
        _impl->scissorRegion = region;
    }
}
