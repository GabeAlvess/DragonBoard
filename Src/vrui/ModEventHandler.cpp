#include "pch.h"
#include "ModEventHandler.h"
#include "VRMenuManager.h"

namespace vrui
{
    ModCallbackEventHandler* ModCallbackEventHandler::GetSingleton()
    {
        static ModCallbackEventHandler singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl ModCallbackEventHandler::ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*)
    {
        if (a_event) {
            if (a_event->eventName == "DragonBoardVR_Toggle") {
                logger::trace("DragonBoardVR: Mod Event received -> toggling menu");
                VRMenuManager::get().toggleMenu();
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
}
