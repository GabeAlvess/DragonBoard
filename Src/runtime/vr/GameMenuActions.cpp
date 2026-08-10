#include "GameMenuActions.h"

#include <RE/I/IStackCallbackFunctor.h>
#include <RE/V/VirtualMachine.h>

namespace dragonboard::runtime::vr
{
    namespace
    {
        class SaveRequestCallback final : public RE::BSScript::IStackCallbackFunctor
        {
        public:
            void operator()(RE::BSScript::Variable) override
            {
                logger::info(
                    "DragonBoardVR: Papyrus accepted the normal save request.");
            }

            void SetObject(
                const RE::BSTSmartPointer<RE::BSScript::Object>&) override
            {}
        };
    }

    void QueueNewSave()
    {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([]() {
                auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
                if (!vm) {
                    logger::error(
                        "DragonBoardVR: cannot request a new save because the Papyrus VM is unavailable.");
                    return;
                }

                auto* args = RE::MakeFunctionArguments();
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                    new SaveRequestCallback());
                if (!vm->DispatchStaticCall(
                        "Game",
                        "RequestSave",
                        args,
                        callback)) {
                    logger::error(
                        "DragonBoardVR: Papyrus rejected RequestSave.");
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
