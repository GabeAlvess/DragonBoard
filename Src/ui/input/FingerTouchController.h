#pragma once

#include <memory>

namespace vrui
{
    class VRMenuManager;
    class VRUIWidget;
}

namespace dragonboard::ui::input
{
    class FingerTouchController
    {
    public:
        static FingerTouchController& GetSingleton();

        [[nodiscard]] bool Update(vrui::VRMenuManager& manager, float deltaTime);

    private:
        FingerTouchController() = default;

        void Deactivate(vrui::VRMenuManager& manager);
        void ReleasePressedWidget();

        bool _active = false;
        bool _contactLatched = false;
        bool _awaitingWithdrawal = false;
        bool _rmlTouchScrolling = false;
        bool _rmlTapPulseDown = false;
        float _frontSign = 1.0f;
        float _rmlTouchStartU = 0.0f;
        float _rmlTouchStartV = 0.0f;
        float _rmlTapU = 0.0f;
        float _rmlTapV = 0.0f;
        bool _pressedLeftHand = false;
        std::weak_ptr<vrui::VRUIWidget> _pressedWidget;
    };
}
