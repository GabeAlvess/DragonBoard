#pragma once

namespace dragonboard::ui::refresh
{
    class RefreshCoordinator
    {
    public:
        void Reset();

        void RequestAll();
        void RequestDynamic();
        void RequestFixedWidgets();
        void RequestTransforms();

        [[nodiscard]] bool TakeAll();
        [[nodiscard]] bool TakeDynamic();
        [[nodiscard]] bool TakeFixedWidgets();
        [[nodiscard]] bool TakeTransforms();

        [[nodiscard]] bool IsAllPending() const { return _allPending; }
        [[nodiscard]] bool IsDynamicPending() const { return _dynamicPending; }
        [[nodiscard]] bool IsFixedWidgetsPending() const { return _fixedWidgetsPending; }
        [[nodiscard]] bool AreTransformsPending() const { return _transformsPending; }

    private:
        bool _allPending{ false };
        bool _dynamicPending{ false };
        bool _fixedWidgetsPending{ false };
        bool _transformsPending{ false };
    };
}
