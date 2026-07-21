#pragma once

#include <cstdint>
#include <string_view>

namespace dragonboard::ui::rml
{
    enum class RmlEntranceStyle : std::uint32_t
    {
        kRadial = 0,
        kReverseRadial = 1,
        kFade = 2,
        kLeftToRight = 3,
        kRightToLeft = 4
    };

    [[nodiscard]] RmlEntranceStyle ParseRmlEntranceStyle(std::string_view value);

    class RmlEntranceAnimation
    {
    public:
        [[nodiscard]] bool Configure(
            bool enabled,
            float durationSeconds,
            float feather,
            RmlEntranceStyle style = RmlEntranceStyle::kRadial);
        void Start();
        void Stop();
        [[nodiscard]] bool Advance(float deltaSeconds);

        [[nodiscard]] bool IsActive() const { return _active; }
        [[nodiscard]] float GetProgress() const;
        [[nodiscard]] float GetFeather() const { return _feather; }
        [[nodiscard]] RmlEntranceStyle GetStyle() const { return _style; }

    private:
        bool _enabled = true;
        bool _active = false;
        float _durationSeconds = 0.25f;
        float _elapsedSeconds = 0.25f;
        float _linearProgress = 1.0f;
        float _feather = 0.10f;
        RmlEntranceStyle _style = RmlEntranceStyle::kRadial;
    };
}
