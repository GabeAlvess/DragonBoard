#include "GameMenuActions.h"

namespace dragonboard::runtime::vr
{
    void QueueQuickSave()
    {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([]() {
                auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
                auto* userEvents = RE::UserEvents::GetSingleton();
                if (!inputManager || !userEvents) return;

                auto* down = RE::ButtonEvent::Create(
                    RE::INPUT_DEVICE::kKeyboard, userEvents->quicksave, 0x3F, 1.0f, 0.0f);
                if (down) {
                    RE::InputEvent* event = down;
                    inputManager->SendEvent(&event);
                }
                auto* up = RE::ButtonEvent::Create(
                    RE::INPUT_DEVICE::kKeyboard, userEvents->quicksave, 0x3F, 0.0f, 0.1f);
                if (up) {
                    RE::InputEvent* event = up;
                    inputManager->SendEvent(&event);
                }
            });
        }
    }

    void ShowGameMenu(const std::string& menuName)
    {
        if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(menuName.c_str(), RE::UI_MESSAGE_TYPE::kShow, nullptr);
        }
    }
}
