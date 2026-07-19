#pragma once

#include <cstdint>

namespace dragonboard::integrations::spellwheel
{
    void Initialize();
    void RegisterPlayerEventSink();
    void ToggleMenuForWheel(std::int32_t wheelId);
}
