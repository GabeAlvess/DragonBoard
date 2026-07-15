#include "ui/refresh/RefreshCoordinator.h"

namespace dragonboard::ui::refresh
{
    void RefreshCoordinator::Reset()
    {
        _allPending = false;
        _dynamicPending = false;
        _fixedWidgetsPending = false;
        _transformsPending = false;
    }

    void RefreshCoordinator::RequestAll()
    {
        _allPending = true;
        _dynamicPending = true;
        _fixedWidgetsPending = true;
        _transformsPending = true;
    }

    void RefreshCoordinator::RequestDynamic()
    {
        _dynamicPending = true;
        _transformsPending = true;
    }

    void RefreshCoordinator::RequestFixedWidgets()
    {
        _fixedWidgetsPending = true;
        _transformsPending = true;
    }

    void RefreshCoordinator::RequestTransforms()
    {
        _transformsPending = true;
    }

    bool RefreshCoordinator::TakeAll()
    {
        if (!_allPending) {
            return false;
        }
        Reset();
        return true;
    }

    bool RefreshCoordinator::TakeDynamic()
    {
        const bool pending = _dynamicPending;
        _dynamicPending = false;
        return pending;
    }

    bool RefreshCoordinator::TakeFixedWidgets()
    {
        const bool pending = _fixedWidgetsPending;
        _fixedWidgetsPending = false;
        return pending;
    }

    bool RefreshCoordinator::TakeTransforms()
    {
        const bool pending = _transformsPending;
        _transformsPending = false;
        return pending;
    }
}
