#pragma once

#include <SKSE/SKSE.h>

namespace dragonboard::bootstrap
{
    void InitializeLogging();
    bool InitializeSerialization();
    void LoadInitialSettings();
    void UpdateGameThread();
    void HandleSKSEMessage(SKSE::MessagingInterface::Message* message);
}
