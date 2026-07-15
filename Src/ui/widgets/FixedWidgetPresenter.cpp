#include "ui/widgets/FixedWidgetPresenter.h"

#include "ui/widgets/FixedWidgetActionHandler.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRUIContainer.h"
#include "vrui/VRUILayoutManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"

#include <memory>
#include <string>

namespace dragonboard::ui::widgets
{
    namespace
    {
        constexpr const char* kMainPanelName = "MainPanel";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleHmdPanelName = "AlwaysVisibleHmdPanel";
        constexpr const char* kDashboardContainerName = "Dashboard";

        [[nodiscard]] std::shared_ptr<vrui::VRUIContainer> FindOrCreateContainer(
            const std::shared_ptr<vrui::VRUIPanel>& panel,
            const std::string& containerName)
        {
            std::shared_ptr<vrui::VRUIContainer> container;
            for (auto& child : panel->getChildren()) {
                if (child && child->getName() == containerName) {
                    container = std::dynamic_pointer_cast<vrui::VRUIContainer>(child);
                    break;
                }
            }

            if (container) {
                container->clearElements();
            } else {
                container = std::make_shared<vrui::VRUIContainer>(
                    containerName,
                    vrui::ContainerLayout::Free,
                    0.0f);
                panel->addElement(container);
            }
            return container;
        }

        [[nodiscard]] RE::NiMatrix3 ResolveRotation(const vrui::UIJSONTransform& transform)
        {
            RE::NiMatrix3 rotation;
            if (transform.hasMatrix) {
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        rotation.entry[row][column] = transform.m[row][column];
                    }
                }
            } else {
                constexpr float kDegToRad = 3.14159265f / 180.0f;
                vrui::VRUILayoutManager::setMatrixEuler(
                    rotation,
                    transform.rx * kDegToRad,
                    transform.ry * kDegToRad,
                    transform.rz * kDegToRad);
            }
            return rotation;
        }

        void MigrateLegacyFixedWidgets(vrui::VRUISettings& settings)
        {
            logger::info(
                "DragonBoardVR: Migrating {} fixed widgets from INI to JSON Dashboard",
                settings.fixedWidgets.size());

            constexpr float kDegToRad = 3.14159265f / 180.0f;
            for (const auto& fixedWidget : settings.fixedWidgets) {
                if (fixedWidget.nifPath.empty()) {
                    continue;
                }

                RE::NiMatrix3 rotation;
                vrui::VRUILayoutManager::setMatrixEuler(
                    rotation,
                    fixedWidget.rotX * kDegToRad,
                    fixedWidget.rotY * kDegToRad,
                    fixedWidget.rotZ * kDegToRad);
                const std::string elementID = fixedWidget.name + "_" + std::to_string(fixedWidget.formID);
                vrui::VRUILayoutManager::updateElementTransformAnywhere(
                    elementID,
                    RE::NiPoint3(fixedWidget.posX, fixedWidget.posY, fixedWidget.posZ),
                    rotation,
                    fixedWidget.scale,
                    fixedWidget.nifPath,
                    fixedWidget.category,
                    fixedWidget.formID);
            }
        }
    }

    void FixedWidgetPresenter::Refresh(vrui::VRMenuManager& menuManager)
    {
        auto& settings = vrui::VRUISettings::get();
        auto mainPanel = menuManager.findPanelByName(kMainPanelName);
        auto alwaysVisiblePanel = menuManager.findPanelByName(kAlwaysVisiblePanelName);
        auto alwaysVisibleHmdPanel = menuManager.findPanelByName(kAlwaysVisibleHmdPanelName);
        if (!mainPanel || !alwaysVisiblePanel || !alwaysVisibleHmdPanel) {
            return;
        }

        auto fixedContainer = FindOrCreateContainer(mainPanel, "FixedWidgetsContainer");
        auto worldMagicContainer = FindOrCreateContainer(alwaysVisiblePanel, "AlwaysVisibleWidgetsContainer");
        auto hmdWorldMagicContainer = FindOrCreateContainer(alwaysVisibleHmdPanel, "AlwaysVisibleHmdWidgetsContainer");

        auto pinnedElements = vrui::VRUILayoutManager::getContainerElements(kDashboardContainerName);
        if (pinnedElements.empty() && !settings.fixedWidgets.empty()) {
            MigrateLegacyFixedWidgets(settings);
            pinnedElements = vrui::VRUILayoutManager::getContainerElements(kDashboardContainerName);
        }

        for (const auto& element : pinnedElements) {
            if (element.visuals.model.empty()) {
                continue;
            }

            auto widget = std::make_shared<vrui::VRUIButton>(
                element.id,
                element.visuals.model,
                "",
                1.0f,
                1.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f);
            widget->setLayoutId(element.id);
            widget->setLabel(element.hideLabel ? "" : element.label);
            widget->setNoPopAnimation(true);
            widget->setDashboardPinned(true);
            widget->setAmbientWiggleEnabled(
                (element.pinToWorld || element.pinToHmdWorld) && element.category == "Magic");
            widget->setDimensions(2.0f, 2.0f);
            widget->setLocalPosition({ element.transform.px, element.transform.py, element.transform.pz });

            const auto rotation = ResolveRotation(element.transform);
            widget->setLocalRotation(rotation);
            widget->setLocalScale(element.transform.scale);

            vrui::FixedWidgetItem actionData;
            actionData.formID = element.formID;
            actionData.category = element.category;
            actionData.actionFunc = element.actionFunc;
            widget->setOnPressHandler([actionData](vrui::VRUIButton*, vrui::EquipHand hand) {
                FixedWidgetActionHandler::Execute(actionData, hand);
            });

            const std::string elementID = element.id;
            widget->setOnSecondaryLongPressHandler([elementID](vrui::VRUIButton*, vrui::EquipHand) {
                auto& currentSettings = vrui::VRUISettings::get();
                if (currentSettings.editModeEnabled) {
                    vrui::VRUILayoutManager::removeElementAnywhere(elementID);
                    vrui::VRMenuManager::get().refreshFixedWidgets();
                    RE::DebugNotification("DragonBoardVR: Pinned item removed.");
                }
            });

            const bool isWorldPinnedMagic = element.pinToWorld && element.category == "Magic";
            const bool isHmdWorldPinnedMagic = element.pinToHmdWorld && element.category == "Magic";
            if (isHmdWorldPinnedMagic) {
                const auto worldRotation = ResolveRotation(element.transform);
                const RE::NiPoint3 headWorldPosition = menuManager.getHeadNode() ?
                    menuManager.getHeadNode()->world.translate : RE::NiPoint3{};
                widget->setWorldLockedToHeadSpace(
                    true,
                    { element.transform.px, element.transform.py, element.transform.pz },
                    worldRotation,
                    element.transform.scale,
                    headWorldPosition);
                hmdWorldMagicContainer->addElement(widget);
            } else if (isWorldPinnedMagic) {
                worldMagicContainer->addElement(widget);
            } else {
                fixedContainer->addElement(widget);
            }
        }

        alwaysVisiblePanel->setActive(!worldMagicContainer->getChildren().empty());
        if (worldMagicContainer->getChildren().empty()) {
            alwaysVisiblePanel->hide();
            alwaysVisiblePanel->setVisible(false);
            alwaysVisiblePanel->detachFromParent();
        } else {
            if (menuManager.isBoardWorldPinned()) {
                if (auto* pinnedAttachNode = menuManager.resolvePinnedAttachNode(menuManager.getPlayerSkeletonRoot())) {
                    alwaysVisiblePanel->attachToNode(pinnedAttachNode);
                }
            } else if (auto* menuHand = menuManager.getMenuHandNode()) {
                alwaysVisiblePanel->attachToHandNode(
                    menuHand,
                    menuManager.getPanelOffset());
            }
            alwaysVisiblePanel->show();
        }

        alwaysVisibleHmdPanel->setActive(!hmdWorldMagicContainer->getChildren().empty());
        if (hmdWorldMagicContainer->getChildren().empty()) {
            alwaysVisibleHmdPanel->hide();
            alwaysVisibleHmdPanel->setVisible(false);
            alwaysVisibleHmdPanel->detachFromParent();
        } else {
            if (auto* headNode = menuManager.getHeadNode()) {
                alwaysVisibleHmdPanel->attachToNode(headNode);
                alwaysVisibleHmdPanel->setLocalPosition({ 0.0f, 0.0f, 0.0f });
                RE::NiMatrix3 identityRotation;
                identityRotation.SetEulerAnglesXYZ(0.0f, 0.0f, 0.0f);
                alwaysVisibleHmdPanel->setLocalRotation(identityRotation);
                alwaysVisibleHmdPanel->setLocalScale(1.0f);
            }
            alwaysVisibleHmdPanel->show();
        }

        mainPanel->recalculateLayout();
        alwaysVisiblePanel->recalculateLayout();
        alwaysVisibleHmdPanel->recalculateLayout();
    }
}
