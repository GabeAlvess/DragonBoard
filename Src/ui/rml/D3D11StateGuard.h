#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace dragonboard::ui::rml
{
    struct D3D11StateTiming
    {
        float renderTargetsMs = 0.0f;
        float viewportScissorMs = 0.0f;
        float rasterizerMs = 0.0f;
        float blendDepthMs = 0.0f;
        float inputAssemblyMs = 0.0f;
        float shadersMs = 0.0f;
        float resourcesMs = 0.0f;
        float totalMs = 0.0f;
    };

    class D3D11StateGuard
    {
    public:
        D3D11StateGuard() = default;
        ~D3D11StateGuard();

        D3D11StateGuard(const D3D11StateGuard&) = delete;
        D3D11StateGuard& operator=(const D3D11StateGuard&) = delete;

        bool Capture(ID3D11DeviceContext* context);
        void Restore();
        [[nodiscard]] bool IsActive() const;
        [[nodiscard]] const D3D11StateTiming& GetLastTiming() const;

    private:
        void ResetBackup();

        ID3D11DeviceContext* _context = nullptr;
        bool _active = false;
        D3D11StateTiming _timing{};

        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> _renderTarget;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> _depthStencilView;
        D3D11_VIEWPORT _viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT _viewportCount = 0;
        D3D11_RECT _scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT _scissorCount = 0;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
        FLOAT _blendFactor[4]{};
        UINT _sampleMask = 0;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthState;
        UINT _stencilReference = 0;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> _inputLayout;
        Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer;
        UINT _vertexStride = 0;
        UINT _vertexOffset = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer> _indexBuffer;
        DXGI_FORMAT _indexFormat = DXGI_FORMAT_UNKNOWN;
        UINT _indexOffset = 0;
        D3D11_PRIMITIVE_TOPOLOGY _topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> _vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> _pixelShader;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> _geometryShader;
        Microsoft::WRL::ComPtr<ID3D11HullShader> _hullShader;
        Microsoft::WRL::ComPtr<ID3D11DomainShader> _domainShader;
        Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexConstantBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _pixelShaderResource;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> _pixelSampler;
    };
}
