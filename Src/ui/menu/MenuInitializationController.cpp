#include "MenuInitializationController.h"

#include "gameplay/CombatSlowTime.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"
#include "vrui/VRUIWidget.h"

#include <Windows.h>
#include <RE/B/BSVisit.h>

namespace dragonboard::ui::menu
{
    void MenuInitializationController::Initialize(vrui::VRMenuManager& manager)
    {
        dragonboard::gameplay::CombatSlowTime::GetSingleton().Close();
        if (manager._initialized) {
            for (auto& panel : manager._panelRegistry.GetPanels()) {
                panel->hide();
                panel->setVisible(false);
                panel->detachFromParent();
            }
            manager._panelRegistry.Clear();
        }
        manager._deferredTasks.Clear();
        manager._refreshCoordinator.Reset();
        manager._settingsSaveScheduler.Reset();

        auto& settings = vrui::VRUISettings::get();
        const std::string iniPath = vrui::VRUISettings::getDefaultIniPath();
        settings.load(iniPath);
        manager._boardPinState.Reset(settings.menuScale);

        if (settings.verboseLogging) {
            spdlog::set_level(spdlog::level::trace);
            logger::trace("DragonBoardVR: Verbose logging ENABLED (trace level)");
        }

        manager._iniChangeWatcher.Track(iniPath);
        manager._layoutIniChangeWatcher.Track(
            vrui::VRUISettings::getDefaultLayoutIniPath());
        manager._stateIniChangeWatcher.Track(
            vrui::VRUISettings::getDefaultStateIniPath());

        if (GetModuleHandleA("vrik.dll") != nullptr) {
            manager._isVRIKInstalled = true;
            logger::trace(
                "DragonBoardVR: VRIK detected. We will attach UI to the 3rd person skeleton.");
        } else {
            manager._isVRIKInstalled = false;
            logger::trace(
                "DragonBoardVR: VRIK not detected. We will attach UI to the 1st person skeleton.");
        }

        auto& beam = manager._pointerVisual.Beam();
        auto& reticle = manager._pointerVisual.Reticle();

        beam = vrui::VRUIWidget::loadModelFromNif(settings.laserNifPath);
        if (!beam) {
            beam = vrui::VRUIWidget::loadModelFromNif("meshes\\magic\\shockbeam01.nif");
            if (!beam) {
                beam = vrui::VRUIWidget::loadModelFromNif(
                    "DragonBoardVR\\IconPlane.nif");
            }
        }

        reticle = vrui::VRUIWidget::loadModelFromNif(
            "DragonBoardVR\\font\\symbol.nif");
        if (!reticle) {
            reticle = vrui::VRUIWidget::loadModelFromNif(
                "DragonBoardVR\\Dragonbeam.nif");
        }
        if (reticle) {
            RE::BSVisit::TraverseScenegraphObjects(
                reticle.get(),
                [](RE::NiAVObject* object) -> RE::BSVisit::BSVisitControl {
                    object->collisionObject = nullptr;
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            reticle->SetAppCulled(false);
            reticle->local.scale = 3.0f;
        }

        if (beam) {
            RE::BSVisit::TraverseScenegraphObjects(
                beam.get(),
                [](RE::NiAVObject* object) -> RE::BSVisit::BSVisitControl {
                    object->collisionObject = nullptr;
                    return RE::BSVisit::BSVisitControl::kContinue;
                });

            RE::BSVisit::TraverseScenegraphGeometries(
                beam.get(),
                [](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
                    if (geometry) geometry->SetAppCulled(false);
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
            beam->SetAppCulled(false);
            beam->local.scale = 0.0f;

            logger::trace("DragonBoardVR: Laser pointer mesh loaded successfully.");
        } else {
            logger::warn(
                "DragonBoardVR: Failed to load laser pointer mesh. Laser will not be drawn.");
        }

        manager._initialized = true;
        logger::trace("DragonBoardVR: VRMenuManager initialized");
    }
}
