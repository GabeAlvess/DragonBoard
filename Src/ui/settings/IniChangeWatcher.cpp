#include "IniChangeWatcher.h"

namespace dragonboard::ui::settings
{
    void IniChangeWatcher::Track(const std::string& path)
    {
        try {
            if (std::filesystem::exists(path)) {
                _lastModifiedTime = std::filesystem::last_write_time(path);
            }
        } catch (...) {
        }
    }

    bool IniChangeWatcher::Update(float deltaTime, const std::string& path, float checkInterval)
    {
        _checkTimer += deltaTime;
        if (_checkTimer < checkInterval) {
            return false;
        }
        _checkTimer = 0.0f;

        try {
            if (std::filesystem::exists(path)) {
                const auto newTime = std::filesystem::last_write_time(path);
                if (newTime != _lastModifiedTime) {
                    _lastModifiedTime = newTime;
                    return true;
                }
            }
        } catch (...) {
        }

        return false;
    }
}
