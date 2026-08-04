#include "pch.h"

#include "DragonBoardVR_API.h"
#include "bootstrap/PluginLifecycle.h"
#include "papyrus/PapyrusPanelBridge.h"

DragonBoardVR_API::IDragonBoardVR* GetDragonBoardVRAPI();
DragonBoardVR_API::IDragonBoardVR2* GetDragonBoardVRAPI2();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* loadInterface)
{
    dragonboard::bootstrap::InitializeLogging();

    logger::trace("===================================================");
    logger::trace("{} v{} - LOADING", Plugin::NAME, Plugin::VERSION.string());
    logger::trace("===================================================");

    REL::Module::reset();
    SKSE::Init(loadInterface);
    dragonboard::bootstrap::LoadInitialSettings();

    const auto* papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(dragonboard::papyrus::RegisterPanelFunctions)) {
        logger::critical("DragonBoardVR: Failed to register Papyrus panel API.");
        return false;
    }

    auto* messaging = reinterpret_cast<SKSE::MessagingInterface*>(
        loadInterface->QueryInterface(SKSE::LoadInterface::kMessaging));
    if (!messaging) {
        logger::critical("DragonBoardVR: Failed to load messaging interface!");
        return false;
    }

    messaging->RegisterListener("SKSE", dragonboard::bootstrap::HandleSKSEMessage);
    logger::trace("DragonBoardVR: Plugin loaded successfully!");
    return true;
}

extern "C" DLLEXPORT DragonBoardVR_API::IDragonBoardVR* RequestPluginAPI()
{
    logger::trace("DragonBoardVR: panel API requested");
    return GetDragonBoardVRAPI();
}

extern "C" DLLEXPORT DragonBoardVR_API::IDragonBoardVR2* RequestPluginAPI2()
{
    logger::trace("DragonBoardVR: version 2 panel API requested");
    return GetDragonBoardVRAPI2();
}
