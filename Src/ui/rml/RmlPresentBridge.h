#pragma once

namespace dragonboard::ui::rml
{
    class RmlPresentBridge
    {
    public:
        using PresentCallback = void (*)(float deltaTime);

        [[nodiscard]] static bool Install(PresentCallback callback);
        [[nodiscard]] static bool IsInstalled();
    };
}
