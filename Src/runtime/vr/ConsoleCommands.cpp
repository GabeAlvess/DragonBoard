#include "runtime/vr/ConsoleCommands.h"

namespace dragonboard::runtime::vr
{
    void TriggerFadeOut(float durationSeconds)
    {
        if (auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
            auto args = RE::MakeFunctionArguments(true, true, 0.0f, static_cast<float>(durationSeconds));
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
            vm->DispatchStaticCall("Game", "FadeOutGame", args, callback);
        }
    }

    void RunConsoleCommand(const std::string& command, bool globalContext, const char* logLabel)
    {
        auto* script = RE::IFormFactory::Create<RE::Script>();
        if (!script) return;

        script->SetCommand(command);
        (void)script->CompileAndRun(globalContext ? nullptr : RE::PlayerCharacter::GetSingleton());

        if (logLabel && *logLabel) {
            logger::trace("{} '{}'", logLabel, command);
        }
    }
}
