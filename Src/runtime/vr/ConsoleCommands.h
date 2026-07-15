#pragma once

#include <string>

namespace dragonboard::runtime::vr
{
    void TriggerFadeOut(float durationSeconds);
    void RunConsoleCommand(const std::string& command, bool globalContext, const char* logLabel);
}
