#include "pch.h"

#include "integrations/spellwheel/SpellWheelIntegration.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

namespace dragonboard::integrations::spellwheel
{
    namespace
    {
        constexpr auto kToggleEvent = "DragonBoardVR_SpellWheelToggle";
        constexpr std::uint32_t kMessageGetInterface = 0xFA27C15D;

        class ISpellWheelInterface001
        {
        public:
            virtual unsigned int getBuildNumber() = 0;
            virtual bool IsMainWheelOpen() = 0;
            virtual bool IsSecondaryWheelOpen() = 0;
            virtual void SpawnConjurationCircle(RE::NiPoint3 position) = 0;
            virtual void CloseOstimWheels() = 0;
        };

        struct SpellWheelMessage
        {
            void* (*GetApiFunction)(unsigned int revisionNumber) = nullptr;
        };

        ISpellWheelInterface001* g_spellWheelInterface = nullptr;

        void ToggleUsingOpenWheelState()
        {
            if (!g_spellWheelInterface) {
                logger::warn(
                    "DragonBoardVR: Spell Wheel event received without an active API; "
                    "using the configured menu hand.");
                vrui::VRMenuManager::get().toggleMenu(true);
                return;
            }

            const bool mainOpen = g_spellWheelInterface->IsMainWheelOpen();
            const bool secondaryOpen = g_spellWheelInterface->IsSecondaryWheelOpen();
            if (mainOpen != secondaryOpen) {
                ToggleMenuForWheel(secondaryOpen ? 1 : 0);
                return;
            }

            logger::warn(
                "DragonBoardVR: Spell Wheel fallback had an ambiguous wheel state "
                "(main={}, secondary={}); using the configured menu hand.",
                mainOpen,
                secondaryOpen);
            vrui::VRMenuManager::get().toggleMenu(true);
        }

        class WheelIdCallback final : public RE::BSScript::IStackCallbackFunctor
        {
        public:
            void operator()(RE::BSScript::Variable result) override
            {
                if (result.IsInt()) {
                    const auto wheelId = static_cast<std::int32_t>(result.GetUInt());
                    logger::info(
                        "DragonBoardVR: Spell Wheel reported last activated wheel {}.",
                        wheelId == 0 ? "main" : "secondary");
                    ToggleMenuForWheel(wheelId);
                    return;
                }

                logger::warn(
                    "DragonBoardVR: GetLastActivatedWheelId returned an unexpected type; "
                    "using wheel-state fallback.");
                ToggleUsingOpenWheelState();
            }

            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
        };

        bool RequestLastActivatedWheel()
        {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) return false;

            auto* args = RE::MakeFunctionArguments();
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(
                new WheelIdCallback());
            return vm->DispatchStaticCall(
                "SpellWheelVR_PluginScript",
                "GetLastActivatedWheelId",
                args,
                callback);
        }

        class ToggleEventSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
        {
        public:
            static ToggleEventSink* GetSingleton()
            {
                static ToggleEventSink singleton;
                return &singleton;
            }

            RE::BSEventNotifyControl ProcessEvent(
                const RE::BSAnimationGraphEvent* event,
                RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
            {
                if (!event || event->tag != kToggleEvent) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (!RequestLastActivatedWheel()) {
                    logger::warn(
                        "DragonBoardVR: Could not dispatch GetLastActivatedWheelId; "
                        "using wheel-state fallback.");
                    ToggleUsingOpenWheelState();
                }

                return RE::BSEventNotifyControl::kContinue;
            }
        };

        bool g_playerSinkRegistered = false;
    }

    void Initialize()
    {
        if (g_spellWheelInterface) return;

        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            logger::warn("DragonBoardVR: SKSE messaging unavailable for Spell Wheel integration.");
            return;
        }

        SpellWheelMessage message;
        messaging->Dispatch(
            kMessageGetInterface,
            &message,
            sizeof(message),
            "SpellWheelVR");
        if (!message.GetApiFunction) {
            logger::info("DragonBoardVR: Spell Wheel not found; optional integration disabled.");
            return;
        }

        g_spellWheelInterface = static_cast<ISpellWheelInterface001*>(
            message.GetApiFunction(1));
        if (g_spellWheelInterface) {
            logger::info(
                "DragonBoardVR: Spell Wheel integration ready (build {}).",
                g_spellWheelInterface->getBuildNumber());
        } else {
            logger::warn("DragonBoardVR: Spell Wheel returned no compatible interface.");
        }
    }

    void RegisterPlayerEventSink()
    {
        if (g_playerSinkRegistered) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::warn("DragonBoardVR: Player unavailable for Spell Wheel event registration.");
            return;
        }

        g_playerSinkRegistered = player->AddAnimationGraphEventSink(
            ToggleEventSink::GetSingleton());
        if (g_playerSinkRegistered) {
            logger::info(
                "DragonBoardVR: Spell Wheel transient event bridge registered ({}).",
                kToggleEvent);
        } else {
            logger::warn("DragonBoardVR: Failed to register Spell Wheel event bridge.");
        }
    }

    void ToggleMenuForWheel(std::int32_t wheelId)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            logger::error(
                "DragonBoardVR: Spell Wheel bridge failed; "
                "SKSE task interface unavailable.");
            return;
        }

        const bool secondaryWheel = wheelId != 0;
        logger::info(
            "DragonBoardVR: Spell Wheel bridge requested (wheel={}).",
            secondaryWheel ? "secondary" : "main");
        tasks->AddTask([secondaryWheel]() {
            bool leftHandedMode = false;
            if (const auto* setting = RE::GetINISetting("bLeftHandedMode:VRInput")) {
                leftHandedMode = setting->GetBool();
            } else {
                logger::warn(
                    "DragonBoardVR: bLeftHandedMode:VRInput unavailable; "
                    "assuming right-handed mode.");
            }

            // Spell Wheel: right-handed mode maps main=right and secondary=left.
            // Skyrim's left-handed mode swaps those physical hands.
            const bool useLeftHand = leftHandedMode ? !secondaryWheel : secondaryWheel;
            vrui::VRUISettings::get().setUseLeftHandAsMenu(useLeftHand);

            logger::info(
                "DragonBoardVR: Spell Wheel activation mapped to {} hand "
                "(wheel={}, leftHandedMode={}).",
                useLeftHand ? "left" : "right",
                secondaryWheel ? "secondary" : "main",
                leftHandedMode);
            vrui::VRMenuManager::get().toggleMenu(true);
        });
    }
}
