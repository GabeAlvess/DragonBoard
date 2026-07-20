#pragma once

namespace dragonboard::ui::rml
{
    class RmlEntranceAnimation
    {
    public:
        [[nodiscard]] bool Configure(
            bool enabled,
            float durationSeconds,
            float feather);
        void Start();
        void Stop();
        [[nodiscard]] bool Advance(float deltaSeconds);

        [[nodiscard]] bool IsActive() const { return _active; }
        [[nodiscard]] float GetProgress() const;
        [[nodiscard]] float GetFeather() const { return _feather; }

    private:
        bool _enabled = true;
        bool _active = false;
        float _durationSeconds = 0.25f;
        float _elapsedSeconds = 0.25f;
        float _linearProgress = 1.0f;
        float _feather = 0.10f;
    };
}
