#pragma once

#include <filesystem>
#include <string>

namespace dragonboard::ui::settings
{
    class IniChangeWatcher
    {
    public:
        void Track(const std::string& path);
        bool Update(float deltaTime, const std::string& path, float checkInterval);

    private:
        std::filesystem::file_time_type _lastModifiedTime;
        float _checkTimer = 0.0f;
    };
}
