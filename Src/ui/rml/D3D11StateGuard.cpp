#include "ui/rml/D3D11StateGuard.h"

#include <chrono>

namespace dragonboard::ui::rml
{
    namespace
    {
        template <class Callback>
        void Measure(float& destination, Callback&& callback)
        {
            const auto started = std::chrono::steady_clock::now();
            callback();
            const auto ended = std::chrono::steady_clock::now();
            destination += std::chrono::duration<float, std::milli>(ended - started).count();
        }
    }

    D3D11StateGuard::~D3D11StateGuard()
    {
        Restore();
    }

    bool D3D11StateGuard::Capture(ID3D11DeviceContext* context)
    {
        if (!context || _active) return false;
        _context = context;
        _timing = {};
        const auto captureStarted = std::chrono::steady_clock::now();

        Measure(_timing.renderTargetsMs, [&] {
            context->OMGetRenderTargets(
                1, _renderTarget.GetAddressOf(), _depthStencilView.GetAddressOf());
        });
        Measure(_timing.viewportScissorMs, [&] {
            _viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            context->RSGetViewports(&_viewportCount, _viewports);
            _scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            context->RSGetScissorRects(&_scissorCount, _scissors);
        });
        Measure(_timing.rasterizerMs, [&] {
            context->RSGetState(_rasterizerState.GetAddressOf());
        });
        Measure(_timing.blendDepthMs, [&] {
            context->OMGetBlendState(
                _blendState.GetAddressOf(), _blendFactor, &_sampleMask);
            context->OMGetDepthStencilState(
                _depthState.GetAddressOf(), &_stencilReference);
        });
        Measure(_timing.inputAssemblyMs, [&] {
            context->IAGetInputLayout(_inputLayout.GetAddressOf());
            context->IAGetVertexBuffers(
                0, 1, _vertexBuffer.GetAddressOf(), &_vertexStride, &_vertexOffset);
            context->IAGetIndexBuffer(
                _indexBuffer.GetAddressOf(), &_indexFormat, &_indexOffset);
            context->IAGetPrimitiveTopology(&_topology);
        });
        Measure(_timing.shadersMs, [&] {
            context->VSGetShader(_vertexShader.GetAddressOf(), nullptr, nullptr);
            context->PSGetShader(_pixelShader.GetAddressOf(), nullptr, nullptr);
            context->GSGetShader(_geometryShader.GetAddressOf(), nullptr, nullptr);
            context->HSGetShader(_hullShader.GetAddressOf(), nullptr, nullptr);
            context->DSGetShader(_domainShader.GetAddressOf(), nullptr, nullptr);
        });
        Measure(_timing.resourcesMs, [&] {
            context->VSGetConstantBuffers(0, 1, _vertexConstantBuffer.GetAddressOf());
            context->PSGetConstantBuffers(0, 1, _pixelConstantBuffer.GetAddressOf());
            context->PSGetShaderResources(0, 1, _pixelShaderResource.GetAddressOf());
            context->PSGetSamplers(0, 1, _pixelSampler.GetAddressOf());
        });

        _timing.totalMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - captureStarted).count();
        _active = true;
        return true;
    }

    void D3D11StateGuard::Restore()
    {
        if (!_active || !_context) return;
        auto* context = _context;
        const auto restoreStarted = std::chrono::steady_clock::now();

        Measure(_timing.renderTargetsMs, [&] {
            auto* renderTarget = _renderTarget.Get();
            context->OMSetRenderTargets(1, &renderTarget, _depthStencilView.Get());
        });
        Measure(_timing.viewportScissorMs, [&] {
            if (_viewportCount > 0) {
                context->RSSetViewports(_viewportCount, _viewports);
            }
            if (_scissorCount > 0) {
                context->RSSetScissorRects(_scissorCount, _scissors);
            }
        });
        Measure(_timing.rasterizerMs, [&] {
            context->RSSetState(_rasterizerState.Get());
        });
        Measure(_timing.blendDepthMs, [&] {
            context->OMSetBlendState(_blendState.Get(), _blendFactor, _sampleMask);
            context->OMSetDepthStencilState(_depthState.Get(), _stencilReference);
        });
        Measure(_timing.inputAssemblyMs, [&] {
            context->IASetInputLayout(_inputLayout.Get());
            auto* vertexBuffer = _vertexBuffer.Get();
            context->IASetVertexBuffers(
                0, 1, &vertexBuffer, &_vertexStride, &_vertexOffset);
            context->IASetIndexBuffer(_indexBuffer.Get(), _indexFormat, _indexOffset);
            context->IASetPrimitiveTopology(_topology);
        });
        Measure(_timing.shadersMs, [&] {
            context->VSSetShader(_vertexShader.Get(), nullptr, 0);
            context->PSSetShader(_pixelShader.Get(), nullptr, 0);
            context->GSSetShader(_geometryShader.Get(), nullptr, 0);
            context->HSSetShader(_hullShader.Get(), nullptr, 0);
            context->DSSetShader(_domainShader.Get(), nullptr, 0);
        });
        Measure(_timing.resourcesMs, [&] {
            auto* vertexConstantBuffer = _vertexConstantBuffer.Get();
            auto* pixelConstantBuffer = _pixelConstantBuffer.Get();
            auto* pixelShaderResource = _pixelShaderResource.Get();
            auto* pixelSampler = _pixelSampler.Get();
            context->VSSetConstantBuffers(0, 1, &vertexConstantBuffer);
            context->PSSetConstantBuffers(0, 1, &pixelConstantBuffer);
            context->PSSetShaderResources(0, 1, &pixelShaderResource);
            context->PSSetSamplers(0, 1, &pixelSampler);
        });

        _active = false;
        _context = nullptr;
        ResetBackup();
        _timing.totalMs += std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - restoreStarted).count();
    }

    bool D3D11StateGuard::IsActive() const
    {
        return _active;
    }

    const D3D11StateTiming& D3D11StateGuard::GetLastTiming() const
    {
        return _timing;
    }

    void D3D11StateGuard::ResetBackup()
    {
        _renderTarget.Reset();
        _depthStencilView.Reset();
        _rasterizerState.Reset();
        _blendState.Reset();
        _depthState.Reset();
        _inputLayout.Reset();
        _vertexBuffer.Reset();
        _indexBuffer.Reset();
        _vertexShader.Reset();
        _pixelShader.Reset();
        _geometryShader.Reset();
        _hullShader.Reset();
        _domainShader.Reset();
        _vertexConstantBuffer.Reset();
        _pixelConstantBuffer.Reset();
        _pixelShaderResource.Reset();
        _pixelSampler.Reset();
    }
}
