#include "FrameUpdateController.h"

#include "diagnostics/FingerTrackingProbe.h"
#include "gameplay/CombatSlowTime.h"
#include "integrations/spellwheel/SpellWheelIntegration.h"
#include "ui/panels/BoardPinWatchdog.h"
#include "ui/equipment/EquipInteractionController.h"
#include "ui/input/InteractionInputController.h"
#include "ui/input/FingerTouchController.h"
#include "ui/input/PointerInteractionController.h"
#include "ui/refresh/RefreshPipelineController.h"
#include "ui/runtime/DeferredActionController.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"

#include <chrono>
#include <mutex>

namespace dragonboard::ui::frame
{
    namespace
    {
        using PerfClock = std::chrono::steady_clock;
        std::mutex updateMutex;

        double ElapsedMs(const PerfClock::time_point& start, const PerfClock::time_point& end)
        {
            return std::chrono::duration<double, std::milli>(end - start).count();
        }
    }

    void FrameUpdateController::Update(vrui::VRMenuManager& manager, float deltaTime)
    {
        std::lock_guard<std::mutex> lock(updateMutex);
        const bool verbosePerf = vrui::VRUISettings::get().verboseLogging;
        const auto frameStart = verbosePerf ? PerfClock::now() : PerfClock::time_point{};

        vrui::VRUIButton::resetFrameLoadCounter();
        dragonboard::ui::runtime::DeferredActionController::Process(manager, deltaTime);
        dragonboard::integrations::spellwheel::Update(deltaTime);

        manager._menuToggleCooldown.Advance(deltaTime);
        dragonboard::ui::equipment::EquipInteractionController::Update(manager, deltaTime);

        if (!manager._initialized) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded() || !player->Get3D(!manager._isVRIKInstalled)) {
            return;
        }

        const auto& settings = vrui::VRUISettings::get();
        dragonboard::diagnostics::FingerTrackingProbe::GetSingleton().Update(
            manager.getPlayerSkeletonRoot(),
            manager._isVRIKInstalled,
            manager._menuSession.IsOpen(),
            settings.fingerTrackingProbe,
            settings.fingerTrackingProbeMarkers,
            settings.fingerTrackingMarkerScale,
            settings.fingerTrackingTipExtension,
            RE::NiPoint3{
                settings.fingerTouchOffsetX,
                settings.fingerTouchOffsetY,
                settings.fingerTouchOffsetZ },
            settings.fingerTrackingProbeInterval,
            deltaTime);

        if (manager._boardPinState.IsPinned() &&
            dragonboard::ui::panels::BoardPinWatchdog::ShouldReturnToHand(
                manager._boardPinState,
                manager._panelRegistry.GetPanels(),
                manager.getMenuHandNode(),
                deltaTime,
                verbosePerf)) {
            manager.setBoardWorldPinned(false);
        }

        if (manager._menuSession.IsOpen() && manager._boardPinState.IsPinned()) {
            manager._refreshCoordinator.RequestTransforms();
        }

        dragonboard::ui::input::InteractionInputController::ProcessActivation(
            manager, deltaTime);
        if (manager._menuSession.IsOpen()) {
            const bool touchActive =
                dragonboard::ui::input::FingerTouchController::GetSingleton().Update(
                    manager, deltaTime);
            if (!touchActive) {
                dragonboard::ui::input::PointerInteractionController::Process(manager, deltaTime);
                dragonboard::ui::input::InteractionInputController::ProcessButtons(
                    manager, deltaTime);
            }
        } else {
            (void)dragonboard::ui::input::FingerTouchController::GetSingleton().Update(
                manager, deltaTime);
        }

        const std::string iniPath = vrui::VRUISettings::getDefaultIniPath();
        if (manager._iniChangeWatcher.Update(deltaTime, iniPath, 0.50f)) {
            logger::trace(
                "DragonBoardVR: INI file modification detected (real-time Edit Mode), reloading settings...");
            vrui::VRUISettings::get().load(iniPath);
            dragonboard::gameplay::CombatSlowTime::GetSingleton().Reconfigure(
                settings.slowTimeOnOpen,
                settings.slowTimeMultiplier);
            manager._refreshCoordinator.RequestAll();
        }

        const auto refreshStart = verbosePerf ? PerfClock::now() : PerfClock::time_point{};
        dragonboard::ui::refresh::RefreshPipelineController::ProcessPending(manager);
        const auto refreshEnd = verbosePerf ? PerfClock::now() : PerfClock::time_point{};

        const auto panelUpdateStart = verbosePerf ? PerfClock::now() : PerfClock::time_point{};
        for (auto& panel : manager._panelRegistry.GetPanels()) {
            if (panel) panel->update(deltaTime);
        }
        const auto panelUpdateEnd = verbosePerf ? PerfClock::now() : PerfClock::time_point{};

        if (verbosePerf) {
            const double refreshMs = ElapsedMs(refreshStart, refreshEnd);
            const double panelUpdateMs = ElapsedMs(panelUpdateStart, panelUpdateEnd);
            const double frameMs = ElapsedMs(frameStart, panelUpdateEnd);
            if (refreshMs >= 0.75 || panelUpdateMs >= 0.75 || frameMs >= 2.0) {
                logger::trace(
                    "DragonBoardVR: Perf frame={:.3f}ms refresh={:.3f}ms panelUpdate={:.3f}ms flags(all={},dynamic={},fixed={},transform={},savePending={})",
                    frameMs,
                    refreshMs,
                    panelUpdateMs,
                    manager._refreshCoordinator.IsAllPending(),
                    manager._refreshCoordinator.IsDynamicPending(),
                    manager._refreshCoordinator.IsFixedWidgetsPending(),
                    manager._refreshCoordinator.AreTransformsPending(),
                    manager._settingsSaveScheduler.IsPending());
            }
        }
    }
}
