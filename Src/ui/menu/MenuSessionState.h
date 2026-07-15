#pragma once

namespace dragonboard::ui::menu
{
    class MenuSessionState
    {
    public:
        void Reset() { _open = false; }
        bool IsOpen() const { return _open; }
        bool Toggle() { _open = !_open; return _open; }
        bool Close()
        {
            if (!_open) return false;
            _open = false;
            return true;
        }

    private:
        bool _open = false;
    };
}
