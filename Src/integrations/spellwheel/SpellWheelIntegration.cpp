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

        bool ToggleUsingOpenWheelState()
        {
            if (!g_spellWheelInterface) {
                return false;
            }

            const bool mainOpen = g_spellWheelInterface->IsMainWheelOpen();
            const bool secondaryOpen = g_spellWheelInterface->IsSecondaryWheelOpen();
            if (mainOpen != secondaryOpen) {
                ToggleMenuForWheel(secondaryOpen ? 1 : 0);
                return true;
            }

            logger::trace(
                "DragonBoardVR: Spell Wheel state was ambiguous during activation "
                "(main={}, secondary={}); requesting the last wheel as fallback.",
                mainOpen,
                secondaryOpen);
            return false;
        }

        class WheelIdCallback final : public RE::BSScript::IStackCallbackFunctor
        {
        public:
            void operator()(RE::BSScript::Variable result) override
            {
                if (result.IsInt()) {
                    const auto wheelId = static_cast<std::int32_t>(result.GetUInt());
                    logger::trace(
                        "DragonBoardVR: Spell Wheel reported last activated wheel {}.",
                        wheelId == 0 ? "main" : "secondary");
                    ToggleMenuForWheel(wheelId);
                    return;
                }

                logger::warn(
                    "DragonBoardVR: GetLastActivatedWheelId returned an unexpected type; "
                    "using the configured menu hand.");
                vrui::VRMenuManager::get().toggleMenu(true);
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
                if (!event) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (event->tag != kToggleEvent) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (ToggleUsingOpenWheelState()) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (!RequestLastActivatedWheel()) {
                    logger::warn(
                        "DragonBoardVR: Could not dispatch GetLastActivatedWheelId; "
                        "using the configured menu hand.");
                    vrui::VRMenuManager::get().toggleMenu(true);
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
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            if (!g_playerSinkRegistered) {
                logger::trace("DragonBoardVR: Player unavailable for Spell Wheel event registration; will retry.");
            }
            return;
        }

        // AddAnimationGraphEventSink() is idempotent: it returns false when this
        // sink is already present and true only when it had to add it. The
        // player's animation graph can be rebuilt during play (for example by
        // equipment/animation changes), so an old successful registration must
        // not prevent us from attaching to the replacement graph.
        const bool added = player->AddAnimationGraphEventSink(
            ToggleEventSink::GetSingleton());
        if (added) {
            if (g_playerSinkRegistered) {
                logger::info(
                    "DragonBoardVR: Spell Wheel event bridge restored after animation graph change ({}).",
                    kToggleEvent);
            } else {
                logger::info(
                    "DragonBoardVR: Spell Wheel transient event bridge registered ({}).",
                    kToggleEvent);
            }
            g_playerSinkRegistered = true;
        } else if (!g_playerSinkRegistered) {
            logger::trace("DragonBoardVR: Spell Wheel event bridge not ready; will retry.");
        }
    }

    void Update(float deltaTime)
    {
        static float registrationCheckTimer = 0.0f;
        registrationCheckTimer -= (std::max)(deltaTime, 0.0f);
        if (registrationCheckTimer > 0.0f) return;

        registrationCheckTimer = 0.1f;
        RegisterPlayerEventSink();
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
        logger::trace(
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

            // GetLastActivatedWheelId already reports the physical wheel:
            // main=right and secondary=left, including Skyrim's native
            // left-handed mode. Applying bLeftHandedMode again swaps it twice.
            const bool useLeftHand = secondaryWheel;
            auto& settings = vrui::VRUISettings::get();
            settings.setUseLeftHandAsMenu(
                useLeftHand,
                leftHandedMode);

            logger::trace(
                "DragonBoardVR: Spell Wheel activation mapped to {} hand "
                "(wheel={}, leftHandedMode={}, poseMirrored={}, "
                "offsetX={:.1f}, rotY={:.1f}, rotZ={:.1f}).",
                useLeftHand ? "left" : "right",
                secondaryWheel ? "secondary" : "main",
                leftHandedMode,
                settings.isMenuPoseMirrored(),
                settings.menuOffsetX,
                settings.menuRotY,
                settings.menuRotZ);
            vrui::VRMenuManager::get().toggleMenu(true);
        });
    }
}
