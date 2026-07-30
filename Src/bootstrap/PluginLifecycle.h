#pragma once

#include <SKSE/SKSE.h>

namespace dragonboard::bootstrap
{
    void InitializeLogging();
    void LoadInitialSettings();
    void HandleSKSEMessage(SKSE::MessagingInterface::Message* message);
}
