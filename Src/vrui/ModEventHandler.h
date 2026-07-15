#pragma once

#include <RE/B/BSTEvent.h>
#include <SKSE/Events.h>

namespace vrui
{
    class ModCallbackEventHandler : public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        static ModCallbackEventHandler* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ModCallbackEvent* a_event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>*) override;
    };
}
