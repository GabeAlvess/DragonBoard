#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dragonboard::game::actions
{
    enum class ActionKind
    {
        kUnknown,
        kConsoleCommand,
        kCastPower,
        kEquipItem
    };

    enum class EquipSide
    {
        kLeft,
        kRight
    };

    enum class ExecutionContext
    {
        kModsPanel,
        kPinnedWidget
    };

    struct ParsedAction
    {
        ActionKind kind{ ActionKind::kUnknown };
        std::string command;
        std::uint32_t formID{ 0 };
    };

    [[nodiscard]] ParsedAction Parse(std::string_view serializedAction);
    [[nodiscard]] std::string MakeCastPower(std::uint32_t formID);
    [[nodiscard]] std::string MakeEquipItem(std::uint32_t formID);
    [[nodiscard]] bool IsDangerousConsoleCommand(std::string_view command);
    [[nodiscard]] bool Execute(const ParsedAction& action, EquipSide side, ExecutionContext context);
}
