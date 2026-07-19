#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace dragonboard::ui::rml
{
    class RmlPerformanceMetrics
    {
    public:
        struct RenderTiming
        {
            float updateMs = 0.0f;
            float beginFrameMs = 0.0f;
            float renderMs = 0.0f;
            float endFrameMs = 0.0f;
            float dx11StateMs = 0.0f;
            float dx11RenderTargetsMs = 0.0f;
            float dx11ViewportScissorMs = 0.0f;
            float dx11RasterizerMs = 0.0f;
            float dx11BlendDepthMs = 0.0f;
            float dx11InputAssemblyMs = 0.0f;
            float dx11ShadersMs = 0.0f;
            float dx11ResourcesMs = 0.0f;
            float totalMs = 0.0f;
            std::size_t domElements = 0;
            int width = 0;
            int height = 0;
            std::string activeDocument;
        };

        struct TimingStats
        {
            float lastMs = 0.0f;
            float averageMs = 0.0f;
            float p95Ms = 0.0f;
            float p99Ms = 0.0f;
        };

        struct Snapshot
        {
            float fps = 0.0f;
            float frameTimeMs = 0.0f;
            TimingStats present;
            TimingStats update;
            TimingStats beginFrame;
            TimingStats render;
            TimingStats endFrame;
            TimingStats dx11State;
            TimingStats dx11RenderTargets;
            TimingStats dx11ViewportScissor;
            TimingStats dx11Rasterizer;
            TimingStats dx11BlendDepth;
            TimingStats dx11InputAssembly;
            TimingStats dx11Shaders;
            TimingStats dx11Resources;
            TimingStats total;
            int panelDrawCalls = 0;
            std::size_t domElements = 0;
            float rendersPerSecond = 0.0f;
            std::uint64_t cachedFrames = 0;
            int renderWidth = 0;
            int renderHeight = 0;
            std::string activeDocument;
            std::string dirtyReason;
        };

        void ResetPresentHistory();
        void OnVisibilityChanged(bool visible);
        void AdvanceRateWindow(float presentSeconds);
        void RecordCachedFrame();
        void RecordRenderedFrame(
            float presentSeconds,
            int drawCalls,
            const RenderTiming& timing,
            std::string dirtyReason);
        [[nodiscard]] Snapshot GetSnapshot() const;

    private:
        struct PerformanceSample
        {
            float presentMs = 0.0f;
            float updateMs = 0.0f;
            float beginFrameMs = 0.0f;
            float renderMs = 0.0f;
            float endFrameMs = 0.0f;
            float dx11StateMs = 0.0f;
            float dx11RenderTargetsMs = 0.0f;
            float dx11ViewportScissorMs = 0.0f;
            float dx11RasterizerMs = 0.0f;
            float dx11BlendDepthMs = 0.0f;
            float dx11InputAssemblyMs = 0.0f;
            float dx11ShadersMs = 0.0f;
            float dx11ResourcesMs = 0.0f;
            float totalMs = 0.0f;
        };

        [[nodiscard]] TimingStats Summarize(
            float PerformanceSample::* member) const;

        std::array<float, 20> _presentFrameTimeHistory{};
        std::size_t _presentFrameTimeHistoryIndex = 0;
        std::size_t _presentFrameTimeHistoryCount = 0;
        float _presentFrameTimeHistorySum = 0.0f;
        float _presentFps = 0.0f;
        float _presentFrameMs = 0.0f;
        int _panelDrawCalls = 0;
        std::array<PerformanceSample, 240> _performanceHistory{};
        std::size_t _performanceHistoryIndex = 0;
        std::size_t _performanceHistoryCount = 0;
        float _renderRateAccumulator = 0.0f;
        std::uint32_t _rendersInRateWindow = 0;
        float _rendersPerSecond = 0.0f;
        std::uint64_t _cachedFrames = 0;
        std::size_t _domElements = 0;
        int _renderWidth = 0;
        int _renderHeight = 0;
        std::string _activeDocument;
        std::string _dirtyReason{ "Open" };
    };
}
