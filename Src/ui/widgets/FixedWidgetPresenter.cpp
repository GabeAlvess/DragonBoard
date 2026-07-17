#include "ui/widgets/FixedWidgetPresenter.h"

#include "ui/widgets/FixedWidgetActionHandler.h"
#include "ui/runtime/DeferredActionController.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRUIContainer.h"
#include "vrui/VRUILayoutManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUISettings.h"
#include "vrui/VRUIItemUtils.h"

#include <memory>
#include <string>

namespace dragonboard::ui::widgets
{
    namespace
    {
        constexpr const char* kPersistentPanelName = "Persistent_Panel";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleHmdPanelName = "AlwaysVisibleHmdPanel";
        constexpr const char* kDashboardContainerName = "Dashboard";

        [[nodiscard]] std::shared_ptr<vrui::VRUIContainer> FindOrCreateContainer(
            const std::shared_ptr<vrui::VRUIPanel>& panel,
            const std::string& containerName,
            bool clearExisting = true)
        {
            std::shared_ptr<vrui::VRUIContainer> container;
            for (auto& child : panel->getChildren()) {
                if (child && child->getName() == containerName) {
                    container = std::dynamic_pointer_cast<vrui::VRUIContainer>(child);
                    break;
                }
            }

            if (container) {
                if (clearExisting) {
                    container->clearElements();
                }
            } else {
                container = std::make_shared<vrui::VRUIContainer>(
                    containerName,
                    vrui::ContainerLayout::Free,
                    0.0f);
                panel->addElement(container);
            }
            return container;
        }

        void RemoveElementNamed(
            const std::shared_ptr<vrui::VRUIContainer>& container,
            const std::string& elementId)
        {
            if (!container) return;
            const auto children = container->getChildren();
            for (const auto& child : children) {
                if (child && child->getName() == elementId) {
                    container->removeElement(child);
                }
            }
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

        [[nodiscard]] std::shared_ptr<vrui::VRUIButton> CreatePinnedWidget(
            const vrui::UIJSONElement& element)
        {
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float posX = 0.0f;
            float posY = 0.0f;
            float posZ = 0.0f;
            float itemScale = 1.0f;
            auto transformSource = vrui::ItemUtils::ItemTransformSource::TypeFallback;
            if (element.visualTransformComposed) {
                transformSource = vrui::ItemUtils::getItemOverrides(
                    RE::TESForm::LookupByID(element.formID),
                    rotX, rotY, rotZ,
                    posX, posY, posZ,
                    itemScale);
            }
            auto widget = std::make_shared<vrui::VRUIButton>(
                element.id,
                element.visuals.model,
                "",
                1.0f,
                1.0f,
                rotX,
                rotY,
                rotZ,
                posX,
                posY,
                posZ,
                itemScale,
                true,
                transformSource);
            // The JSON root already contains the fully composed transform of
            // the preview visual. Keep the newly loaded PrimaryVisualTransform
            // neutral so rotation and scale are not applied a second time.
            widget->setPrimaryVisualIdentityOnLoad(element.visualTransformComposed);
            widget->setLayoutId(element.id);
            widget->setLabel(element.hideLabel ? "" : element.label);
            widget->setNoPopAnimation(true);
            widget->setDashboardPinned(true);
            widget->setAmbientWiggleEnabled(
                (element.pinToWorld || element.pinToHmdWorld) && element.category == "Magic");
            widget->setDimensions(2.0f, 2.0f);
            widget->setLocalPosition({ element.transform.px, element.transform.py, element.transform.pz });
            widget->setLocalRotation(ResolveRotation(element.transform));
            widget->setLocalScale(element.transform.scale);

            vrui::FixedWidgetItem actionData;
            actionData.formID = element.formID;
            actionData.category = element.category;
            actionData.actionFunc = element.actionFunc;
            widget->setOnPressHandler([actionData](vrui::VRUIButton*, vrui::EquipHand hand) {
                FixedWidgetActionHandler::Execute(actionData, hand);
            });

            const std::string elementID = element.id;
            widget->setOnSecondaryLongPressHandler([elementID](vrui::VRUIButton* button, vrui::EquipHand) {
                auto& currentSettings = vrui::VRUISettings::get();
                if (currentSettings.editModeEnabled) {
                    // Give immediate visual feedback without destroying the
                    // button while its own callback is still on the stack.
                    if (button) {
                        button->setVisible(false);
                    }
                    auto& menuManager = vrui::VRMenuManager::get();
                    dragonboard::ui::runtime::DeferredActionController::Schedule(
                        menuManager,
                        0.0f,
                        [elementID]() {
                            auto& deferredManager = vrui::VRMenuManager::get();
                            deferredManager.clearHover();
                            deferredManager.clearGrabbedWidget(nullptr);
                            vrui::VRUILayoutManager::removeElementAnywhere(elementID);
                            FixedWidgetPresenter::RefreshElement(deferredManager, elementID);
                            RE::DebugNotification("DragonBoardVR: Pinned item removed.");
                        });
                }
            });
            return widget;
        }

        void AddPinnedWidgetToTarget(
            vrui::VRMenuManager& menuManager,
            const vrui::UIJSONElement& element,
            const std::shared_ptr<vrui::VRUIContainer>& fixedContainer,
            const std::shared_ptr<vrui::VRUIContainer>& worldMagicContainer,
            const std::shared_ptr<vrui::VRUIContainer>& hmdWorldMagicContainer)
        {
            if (element.visuals.model.empty()) return;
            auto widget = CreatePinnedWidget(element);
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
        auto persistentPanel = menuManager.findPanelByName(kPersistentPanelName);
        auto alwaysVisiblePanel = menuManager.findPanelByName(kAlwaysVisiblePanelName);
        auto alwaysVisibleHmdPanel = menuManager.findPanelByName(kAlwaysVisibleHmdPanelName);
        if (!persistentPanel || !alwaysVisiblePanel || !alwaysVisibleHmdPanel) {
            return;
        }

        // Dashboard pins belong to the persistent board layer so switching to
        // Mods (or any other content panel) does not cull them with MainPanel.
        auto fixedContainer = FindOrCreateContainer(persistentPanel, "FixedWidgetsContainer");
        auto worldMagicContainer = FindOrCreateContainer(alwaysVisiblePanel, "AlwaysVisibleWidgetsContainer");
        auto hmdWorldMagicContainer = FindOrCreateContainer(alwaysVisibleHmdPanel, "AlwaysVisibleHmdWidgetsContainer");

        auto pinnedElements = vrui::VRUILayoutManager::getContainerElements(kDashboardContainerName);
        if (pinnedElements.empty() && !settings.fixedWidgets.empty()) {
            MigrateLegacyFixedWidgets(settings);
            pinnedElements = vrui::VRUILayoutManager::getContainerElements(kDashboardContainerName);
        }

        for (const auto& element : pinnedElements) {
            AddPinnedWidgetToTarget(
                menuManager,
                element,
                fixedContainer,
                worldMagicContainer,
                hmdWorldMagicContainer);
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

        persistentPanel->recalculateLayout();
        alwaysVisiblePanel->recalculateLayout();
        alwaysVisibleHmdPanel->recalculateLayout();
    }

    void FixedWidgetPresenter::RefreshElement(
        vrui::VRMenuManager& menuManager,
        const std::string& elementId)
    {
        auto persistentPanel = menuManager.findPanelByName(kPersistentPanelName);
        auto alwaysVisiblePanel = menuManager.findPanelByName(kAlwaysVisiblePanelName);
        auto alwaysVisibleHmdPanel = menuManager.findPanelByName(kAlwaysVisibleHmdPanelName);
        if (!persistentPanel || !alwaysVisiblePanel || !alwaysVisibleHmdPanel) return;

        auto fixedContainer = FindOrCreateContainer(
            persistentPanel, "FixedWidgetsContainer", false);
        auto worldMagicContainer = FindOrCreateContainer(
            alwaysVisiblePanel, "AlwaysVisibleWidgetsContainer", false);
        auto hmdWorldMagicContainer = FindOrCreateContainer(
            alwaysVisibleHmdPanel, "AlwaysVisibleHmdWidgetsContainer", false);

        // A pin can change target (dashboard, left hand, or world), so remove
        // only this element from every destination before adding its new state.
        RemoveElementNamed(fixedContainer, elementId);
        RemoveElementNamed(worldMagicContainer, elementId);
        RemoveElementNamed(hmdWorldMagicContainer, elementId);

        if (const auto element = vrui::VRUILayoutManager::findElementAnywhere(elementId)) {
            AddPinnedWidgetToTarget(
                menuManager,
                *element,
                fixedContainer,
                worldMagicContainer,
                hmdWorldMagicContainer);
        }

        // Keep the panel routing state correct without rebuilding unrelated pins.
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
                alwaysVisiblePanel->attachToHandNode(menuHand, menuManager.getPanelOffset());
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

        // removeElement()/addElement() already recalculates the one container
        // that actually changed and propagates that layout change to its
        // parent. Recalculating all six nodes here repeated the expensive
        // scene traversal for every single pin edit/removal.
    }
}
