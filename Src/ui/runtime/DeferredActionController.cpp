#include "DeferredActionController.h"

#include "runtime/vr/ConsoleCommands.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

#include <chrono>

namespace dragonboard::ui::runtime
{
    namespace
    {
        using PerfClock = std::chrono::steady_clock;

        double ElapsedMs(const PerfClock::time_point& start, const PerfClock::time_point& end)
        {
            return std::chrono::duration<double, std::milli>(end - start).count();
        }
    }

    void DeferredActionController::RequestSettingsSave(
        vrui::VRMenuManager& manager,
        float delay)
    {
        manager._settingsSaveScheduler.Request(delay);
    }

    void DeferredActionController::SaveSettingsNow(vrui::VRMenuManager& manager)
    {
        manager._settingsSaveScheduler.Reset();
        const bool verbosePerf = vrui::VRUISettings::get().verboseLogging;
        const auto saveStart = verbosePerf ? PerfClock::now() : PerfClock::time_point{};
        const auto iniPath = vrui::VRUISettings::getDefaultIniPath();
        vrui::VRUISettings::get().save(iniPath);
        // This write came from DragonBoard itself. Advance the watcher baseline
        // immediately so the next poll does not mistake it for an external edit
        // and request a global refresh that rebuilds every pinned widget.
        manager._iniChangeWatcher.Track(iniPath);
        if (verbosePerf) {
            const double saveMs = ElapsedMs(saveStart, PerfClock::now());
            if (saveMs >= 0.5) {
                logger::trace("DragonBoardVR: Settings save took {:.3f}ms", saveMs);
            }
        }
    }

    void DeferredActionController::Schedule(
        vrui::VRMenuManager& manager,
        float delaySeconds,
        std::function<void()> task)
    {
        manager._deferredTasks.Schedule(delaySeconds, std::move(task));
    }

    void DeferredActionController::Process(vrui::VRMenuManager& manager, float deltaTime)
    {
        if (manager._settingsSaveScheduler.Update(deltaTime)) {
            SaveSettingsNow(manager);
        }

        manager._deferredTasks.Update(deltaTime);
    }

    void DeferredActionController::ExecuteConsoleCommand(
        vrui::VRMenuManager& manager,
        const std::string& command,
        bool isDangerous,
        const char* logLabel)
    {
        if (command.empty()) return;

        if (isDangerous) {
            dragonboard::runtime::vr::TriggerFadeOut(1.5f);
            Schedule(manager, 1.5f, [command, logLabel]() {
                dragonboard::runtime::vr::RunConsoleCommand(command, true, logLabel);
            });
            return;
        }

        Schedule(manager, 0.0f, [command, logLabel]() {
            dragonboard::runtime::vr::RunConsoleCommand(command, false, logLabel);
        });
    }
}
