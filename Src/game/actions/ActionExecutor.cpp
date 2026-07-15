#include "game/actions/ActionExecutor.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>

#include <RE/A/ActorEquipManager.h>
#include <RE/I/IFormFactory.h>
#include <RE/M/MagicCaster.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/Script.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESShout.h>

namespace dragonboard::game::actions
{
    namespace
    {
        constexpr std::string_view kCommandPrefix = "cmd:";
        constexpr std::string_view kCastPowerPrefix = "c++:cast_power ";
        constexpr std::string_view kEquipItemPrefix = "c++:equip_item ";

        [[nodiscard]] bool ParseFormID(std::string_view value, std::uint32_t& result)
        {
            const auto* begin = value.data();
            const auto* end = begin + value.size();
            const auto parseResult = std::from_chars(begin, end, result, 16);
            return parseResult.ec == std::errc{} && parseResult.ptr == end;
        }

        [[nodiscard]] std::string TrimLeadingWhitespace(std::string value)
        {
            const auto first = value.find_first_not_of(" \t");
            if (first == std::string::npos) {
                return value;
            }
            value.erase(0, first);
            return value;
        }

        [[nodiscard]] bool ExecuteConsoleCommand(const ParsedAction& action, ExecutionContext context)
        {
            auto command = action.command;
            if (context == ExecutionContext::kModsPanel) {
                command = TrimLeadingWhitespace(std::move(command));
            }

            auto* script = RE::IFormFactory::Create<RE::Script>();
            if (!script) {
                return false;
            }

            script->SetCommand(command);
            (void)script->CompileAndRun(RE::PlayerCharacter::GetSingleton());
            if (context == ExecutionContext::kModsPanel) {
                logger::trace("DragonBoardVR: Executed mod action: '{}'", command);
            } else {
                logger::trace("DragonBoardVR: Executed pinned mod action (cmd): '{}'", command);
            }
            return true;
        }

        [[nodiscard]] bool ExecuteCastPower(const ParsedAction& action, ExecutionContext context)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* form = RE::TESForm::LookupByID(action.formID);
            if (!player || !form) {
                return false;
            }

            auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
            if (!caster) {
                return false;
            }

            if (form->Is(RE::FormType::Shout)) {
                auto* shout = form->As<RE::TESShout>();
                if (shout->variations[0].spell) {
                    caster->CastSpellImmediate(shout->variations[0].spell, false, nullptr, 1.0f, false, 0.0f, player);
                }
            } else if (form->Is(RE::FormType::Spell)) {
                caster->CastSpellImmediate(form->As<RE::SpellItem>(), false, nullptr, 1.0f, false, 0.0f, player);
            }

            if (context == ExecutionContext::kModsPanel) {
                logger::trace("DragonBoardVR: Cast spell/shout {:08X} natively.", action.formID);
            } else {
                logger::trace("DragonBoardVR: Executed pinned mod action (cast): {:08X}", action.formID);
            }
            return true;
        }

        [[nodiscard]] bool ExecuteEquipItem(
            const ParsedAction& action,
            EquipSide side,
            ExecutionContext context)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* form = RE::TESForm::LookupByID(action.formID);
            if (!player || !form || !form->IsBoundObject()) {
                return false;
            }

            const auto slotFormID = side == EquipSide::kLeft ? 0x13F43 : 0x13F42;
            auto* slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID);
            RE::ActorEquipManager::GetSingleton()->EquipObject(
                player,
                form->As<RE::TESBoundObject>(),
                nullptr,
                1,
                slot,
                true,
                false,
                false);

            if (context == ExecutionContext::kModsPanel) {
                logger::trace("DragonBoardVR: Equipped item {:08X} natively.", action.formID);
            } else {
                logger::trace("DragonBoardVR: Executed pinned mod action (equip): {:08X}", action.formID);
            }
            return true;
        }
    }

    ParsedAction Parse(std::string_view serializedAction)
    {
        if (serializedAction.starts_with(kCommandPrefix)) {
            return { ActionKind::kConsoleCommand, std::string(serializedAction.substr(kCommandPrefix.size())), 0 };
        }

        const auto parseFormAction = [serializedAction](std::string_view prefix, ActionKind kind) {
            ParsedAction result;
            if (!serializedAction.starts_with(prefix) ||
                !ParseFormID(serializedAction.substr(prefix.size()), result.formID)) {
                return result;
            }
            result.kind = kind;
            return result;
        };

        if (serializedAction.starts_with(kCastPowerPrefix)) {
            return parseFormAction(kCastPowerPrefix, ActionKind::kCastPower);
        }
        if (serializedAction.starts_with(kEquipItemPrefix)) {
            return parseFormAction(kEquipItemPrefix, ActionKind::kEquipItem);
        }
        return {};
    }

    std::string MakeCastPower(std::uint32_t formID)
    {
        return std::format("{}{:08X}", kCastPowerPrefix, formID);
    }

    std::string MakeEquipItem(std::uint32_t formID)
    {
        return std::format("{}{:08X}", kEquipItemPrefix, formID);
    }

    bool IsDangerousConsoleCommand(std::string_view command)
    {
        std::string lowerCommand(command);
        std::transform(lowerCommand.begin(), lowerCommand.end(), lowerCommand.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

        return lowerCommand.starts_with("coc ") ||
               lowerCommand.starts_with("cow ") ||
               lowerCommand.starts_with("player.moveto") ||
               lowerCommand.starts_with("player.placeatme") ||
               lowerCommand.starts_with("loadgame") ||
               lowerCommand.starts_with("qqq") ||
               lowerCommand.starts_with("quit");
    }

    bool Execute(const ParsedAction& action, EquipSide side, ExecutionContext context)
    {
        switch (action.kind) {
        case ActionKind::kConsoleCommand:
            return ExecuteConsoleCommand(action, context);
        case ActionKind::kCastPower:
            return ExecuteCastPower(action, context);
        case ActionKind::kEquipItem:
            return ExecuteEquipItem(action, side, context);
        default:
            return false;
        }
    }
}
