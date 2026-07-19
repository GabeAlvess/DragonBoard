#pragma once

#include <cstdint>
#include <string>

namespace dragonboard::ui::rml
{
    enum class RmlDirtyReason : std::uint32_t
    {
        kNone = 0,
        kOpen = 1u << 0,
        kDocument = 1u << 1,
        kData = 1u << 2,
        kPointer = 1u << 3,
        kScroll = 1u << 4,
        kAnimation = 1u << 5,
        kResolution = 1u << 6
    };

    constexpr RmlDirtyReason operator|(RmlDirtyReason left, RmlDirtyReason right)
    {
        return static_cast<RmlDirtyReason>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    class RmlRenderScheduler
    {
    public:
        void Configure(bool renderOnDirty, int maximumActiveFps);
        void SetVisible(bool visible);
        void MarkDirty(RmlDirtyReason reason);
        [[nodiscard]] bool ShouldRender(float deltaTime, bool animationActive);
        void OnRendered();

        [[nodiscard]] bool IsVisible() const { return _visible; }
        [[nodiscard]] bool IsRenderOnDirtyEnabled() const { return _renderOnDirty; }
        [[nodiscard]] RmlDirtyReason GetPendingReasons() const { return _pendingReasons; }
        [[nodiscard]] RmlDirtyReason GetLastRenderedReasons() const { return _lastRenderedReasons; }
        [[nodiscard]] std::string DescribePendingReasons() const;
        [[nodiscard]] std::string DescribeLastRenderedReasons() const;

    private:
        [[nodiscard]] static std::string DescribeReasons(RmlDirtyReason reasons);

        bool _visible = false;
        bool _renderOnDirty = true;
        int _maximumActiveFps = 60;
        float _elapsedSinceRender = 0.0f;
        RmlDirtyReason _pendingReasons = RmlDirtyReason::kNone;
        RmlDirtyReason _lastRenderedReasons = RmlDirtyReason::kNone;
    };
}
