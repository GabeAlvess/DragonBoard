#include "ui/rml/RmlRenderScheduler.h"

#include <algorithm>
#include <array>
#include <utility>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr bool HasReason(RmlDirtyReason reasons, RmlDirtyReason reason)
        {
            return (static_cast<std::uint32_t>(reasons) &
                    static_cast<std::uint32_t>(reason)) != 0;
        }
    }

    void RmlRenderScheduler::Configure(bool renderOnDirty, int maximumActiveFps)
    {
        _renderOnDirty = renderOnDirty;
        _maximumActiveFps = std::clamp(maximumActiveFps, 15, 240);
    }

    void RmlRenderScheduler::SetVisible(bool visible)
    {
        if (_visible == visible) return;
        _visible = visible;
        _elapsedSinceRender = 1.0f / static_cast<float>(_maximumActiveFps);
        _pendingReasons = visible ? RmlDirtyReason::kOpen : RmlDirtyReason::kNone;
        if (!visible) {
            _lastRenderedReasons = RmlDirtyReason::kNone;
        }
    }

    void RmlRenderScheduler::MarkDirty(RmlDirtyReason reason)
    {
        _pendingReasons = _pendingReasons | reason;
    }

    bool RmlRenderScheduler::ShouldRender(float deltaTime, bool animationActive)
    {
        if (!_visible) return false;
        if (!_renderOnDirty) {
            MarkDirty(RmlDirtyReason::kAnimation);
            return true;
        }
        const float interval = 1.0f / static_cast<float>(_maximumActiveFps);
        _elapsedSinceRender = std::min(
            interval * 2.0f,
            _elapsedSinceRender + std::clamp(deltaTime, 0.0f, 0.1f));

        if (animationActive) {
            MarkDirty(RmlDirtyReason::kAnimation);
        }

        return _pendingReasons != RmlDirtyReason::kNone &&
               _elapsedSinceRender + 0.00001f >= interval;
    }

    void RmlRenderScheduler::OnRendered()
    {
        const float interval = 1.0f / static_cast<float>(_maximumActiveFps);
        _lastRenderedReasons = _pendingReasons;
        _pendingReasons = RmlDirtyReason::kNone;
        _elapsedSinceRender = std::max(0.0f, _elapsedSinceRender - interval);
    }

    std::string RmlRenderScheduler::DescribePendingReasons() const
    {
        return DescribeReasons(_pendingReasons);
    }

    std::string RmlRenderScheduler::DescribeLastRenderedReasons() const
    {
        if (!_renderOnDirty) return "Continuous fallback";
        return DescribeReasons(_lastRenderedReasons);
    }

    std::string RmlRenderScheduler::DescribeReasons(RmlDirtyReason reasons)
    {
        if (reasons == RmlDirtyReason::kNone) return "None (cached)";
        constexpr std::array entries{
            std::pair{ RmlDirtyReason::kOpen, "Open" },
            std::pair{ RmlDirtyReason::kDocument, "Document" },
            std::pair{ RmlDirtyReason::kData, "Data" },
            std::pair{ RmlDirtyReason::kPointer, "Pointer" },
            std::pair{ RmlDirtyReason::kScroll, "Scroll" },
            std::pair{ RmlDirtyReason::kAnimation, "Animation" },
            std::pair{ RmlDirtyReason::kResolution, "Resolution" }
        };
        std::string result;
        for (const auto& [reason, label] : entries) {
            if (!HasReason(reasons, reason)) continue;
            if (!result.empty()) result += '|';
            result += label;
        }
        return result;
    }
}
