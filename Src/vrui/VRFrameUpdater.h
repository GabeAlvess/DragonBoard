#pragma once

#include <RE/B/BSTEvent.h>
#include <RE/I/InputEvent.h>
#include <chrono>

namespace vrui
{
    class VRFrameUpdater : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static VRFrameUpdater* GetSingleton();
        static void Register();

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* a_eventList,
            RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

    private:
        VRFrameUpdater();
        std::chrono::high_resolution_clock::time_point _lastTime;
    };
}
