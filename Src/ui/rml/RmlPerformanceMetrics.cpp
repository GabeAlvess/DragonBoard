#include "ui/rml/RmlPerformanceMetrics.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace dragonboard::ui::rml
{
    void RmlPerformanceMetrics::ResetPresentHistory()
    {
        _presentFrameTimeHistory.fill(0.0f);
        _presentFrameTimeHistoryIndex = 0;
        _presentFrameTimeHistoryCount = 0;
        _presentFrameTimeHistorySum = 0.0f;
        _presentFps = 0.0f;
        _presentFrameMs = 0.0f;
    }

    void RmlPerformanceMetrics::OnVisibilityChanged(bool visible)
    {
        _renderRateAccumulator = 0.0f;
        _rendersInRateWindow = 0;
        _rendersPerSecond = 0.0f;
        if (visible) {
            _cachedFrames = 0;
            _dirtyReason = "Open";
        }
    }

    void RmlPerformanceMetrics::AdvanceRateWindow(float presentSeconds)
    {
        _renderRateAccumulator += presentSeconds;
        if (_renderRateAccumulator >= 1.0f) {
            _rendersPerSecond = static_cast<float>(_rendersInRateWindow) /
                _renderRateAccumulator;
            _renderRateAccumulator = 0.0f;
            _rendersInRateWindow = 0;
        }
    }

    void RmlPerformanceMetrics::RecordCachedFrame()
    {
        ++_cachedFrames;
    }

    void RmlPerformanceMetrics::RecordRenderedFrame(
        float presentSeconds,
        int drawCalls,
        const RenderTiming& timing,
        std::string dirtyReason)
    {
        ++_rendersInRateWindow;
        if (_presentFrameTimeHistoryCount < _presentFrameTimeHistory.size()) {
            _presentFrameTimeHistory[_presentFrameTimeHistoryIndex] = presentSeconds;
            _presentFrameTimeHistorySum += presentSeconds;
            ++_presentFrameTimeHistoryCount;
        } else {
            _presentFrameTimeHistorySum -=
                _presentFrameTimeHistory[_presentFrameTimeHistoryIndex];
            _presentFrameTimeHistory[_presentFrameTimeHistoryIndex] = presentSeconds;
            _presentFrameTimeHistorySum += presentSeconds;
        }
        _presentFrameTimeHistoryIndex =
            (_presentFrameTimeHistoryIndex + 1) % _presentFrameTimeHistory.size();

        const float averageFrameSeconds = _presentFrameTimeHistoryCount > 0 ?
            _presentFrameTimeHistorySum /
                static_cast<float>(_presentFrameTimeHistoryCount) :
            0.0f;
        _presentFrameMs = averageFrameSeconds * 1000.0f;
        _presentFps = averageFrameSeconds > 0.0f ? 1.0f / averageFrameSeconds : 0.0f;
        _panelDrawCalls = drawCalls;

        auto& sample = _performanceHistory[_performanceHistoryIndex];
        sample.presentMs = presentSeconds * 1000.0f;
        sample.updateMs = timing.updateMs;
        sample.beginFrameMs = timing.beginFrameMs;
        sample.renderMs = timing.renderMs;
        sample.endFrameMs = timing.endFrameMs;
        sample.dx11StateMs = timing.dx11StateMs;
        sample.dx11RenderTargetsMs = timing.dx11RenderTargetsMs;
        sample.dx11ViewportScissorMs = timing.dx11ViewportScissorMs;
        sample.dx11RasterizerMs = timing.dx11RasterizerMs;
        sample.dx11BlendDepthMs = timing.dx11BlendDepthMs;
        sample.dx11InputAssemblyMs = timing.dx11InputAssemblyMs;
        sample.dx11ShadersMs = timing.dx11ShadersMs;
        sample.dx11ResourcesMs = timing.dx11ResourcesMs;
        sample.totalMs = timing.totalMs;
        _performanceHistoryIndex =
            (_performanceHistoryIndex + 1) % _performanceHistory.size();
        _performanceHistoryCount = std::min(
            _performanceHistoryCount + 1,
            _performanceHistory.size());

        _domElements = timing.domElements;
        _renderWidth = timing.width;
        _renderHeight = timing.height;
        _activeDocument = timing.activeDocument;
        _dirtyReason = std::move(dirtyReason);
    }

    RmlPerformanceMetrics::TimingStats RmlPerformanceMetrics::Summarize(
        float PerformanceSample::* member) const
    {
        TimingStats result;
        if (_performanceHistoryCount == 0) return result;

        std::vector<float> values;
        values.reserve(_performanceHistoryCount);
        float sum = 0.0f;
        for (std::size_t index = 0; index < _performanceHistoryCount; ++index) {
            const float value = _performanceHistory[index].*member;
            values.push_back(value);
            sum += value;
        }
        std::sort(values.begin(), values.end());
        const auto percentile = [&values](float percentileValue) {
            const auto rank = static_cast<std::size_t>(std::ceil(
                percentileValue * static_cast<float>(values.size()))) - 1;
            return values[std::min(rank, values.size() - 1)];
        };
        const std::size_t lastIndex =
            (_performanceHistoryIndex + _performanceHistory.size() - 1) %
            _performanceHistory.size();
        result.lastMs = _performanceHistory[lastIndex].*member;
        result.averageMs = sum / static_cast<float>(_performanceHistoryCount);
        result.p95Ms = percentile(0.95f);
        result.p99Ms = percentile(0.99f);
        return result;
    }

    RmlPerformanceMetrics::Snapshot RmlPerformanceMetrics::GetSnapshot() const
    {
        Snapshot snapshot;
        snapshot.fps = _presentFps;
        snapshot.frameTimeMs = _presentFrameMs;
        snapshot.present = Summarize(&PerformanceSample::presentMs);
        snapshot.update = Summarize(&PerformanceSample::updateMs);
        snapshot.beginFrame = Summarize(&PerformanceSample::beginFrameMs);
        snapshot.render = Summarize(&PerformanceSample::renderMs);
        snapshot.endFrame = Summarize(&PerformanceSample::endFrameMs);
        snapshot.dx11State = Summarize(&PerformanceSample::dx11StateMs);
        snapshot.dx11RenderTargets = Summarize(&PerformanceSample::dx11RenderTargetsMs);
        snapshot.dx11ViewportScissor = Summarize(&PerformanceSample::dx11ViewportScissorMs);
        snapshot.dx11Rasterizer = Summarize(&PerformanceSample::dx11RasterizerMs);
        snapshot.dx11BlendDepth = Summarize(&PerformanceSample::dx11BlendDepthMs);
        snapshot.dx11InputAssembly = Summarize(&PerformanceSample::dx11InputAssemblyMs);
        snapshot.dx11Shaders = Summarize(&PerformanceSample::dx11ShadersMs);
        snapshot.dx11Resources = Summarize(&PerformanceSample::dx11ResourcesMs);
        snapshot.total = Summarize(&PerformanceSample::totalMs);
        snapshot.panelDrawCalls = _panelDrawCalls;
        snapshot.domElements = _domElements;
        snapshot.rendersPerSecond = _rendersPerSecond;
        snapshot.cachedFrames = _cachedFrames;
        snapshot.renderWidth = _renderWidth;
        snapshot.renderHeight = _renderHeight;
        snapshot.activeDocument = _activeDocument;
        snapshot.dirtyReason = _dirtyReason;
        return snapshot;
    }
}
