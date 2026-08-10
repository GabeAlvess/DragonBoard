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

#include <chrono>
#include <memory>
#include <string>

namespace dragonboard::ui::widgets
{
    namespace
    {
        constexpr const char* kPersistentPanelName = "Persistent_Panel";
        constexpr const char* kAlwaysVisiblePanelName = "AlwaysVisiblePanel";
        constexpr const char* kAlwaysVisibleRightHandPanelName = "AlwaysVisibleRightHandPanel";
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
                (element.pinToWorld || element.pinToRightHand) &&
                element.category == "Magic");
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
            widget->setOnSecondaryPressHandler([elementID](vrui::VRUIButton* button, vrui::EquipHand) {
                auto& currentSettings = vrui::VRUISettings::get();
                if (button && button->isGrabbed() &&
                    !currentSettings.lockPins && currentSettings.editModeEnabled) {
                    const auto scheduledAt = std::chrono::steady_clock::now();
                    // Give immediate visual feedback without destroying the
                    // button while its own callback is still on the stack.
                    button->setVisible(false);
                    auto& menuManager = vrui::VRMenuManager::get();
                    dragonboard::ui::runtime::DeferredActionController::Schedule(
                        menuManager,
                        0.0f,
                        [elementID, scheduledAt]() {
                            if (vrui::VRUISettings::get().lockPins) {
                                return;
                            }
                            const auto startedAt = std::chrono::steady_clock::now();
                            auto& deferredManager = vrui::VRMenuManager::get();
                            deferredManager.clearHover();
                            deferredManager.clearGrabbedWidget(nullptr);
                            vrui::VRUILayoutManager::removeElementAnywhere(elementID);
                            const auto layoutRemovedAt = std::chrono::steady_clock::now();
                            FixedWidgetPresenter::RefreshElement(deferredManager, elementID);
                            const auto finishedAt = std::chrono::steady_clock::now();
                            const auto layoutMs = std::chrono::duration<double, std::milli>(
                                layoutRemovedAt - startedAt).count();
                            const auto refreshMs = std::chrono::duration<double, std::milli>(
                                finishedAt - layoutRemovedAt).count();
                            const auto queueMs = std::chrono::duration<double, std::milli>(
                                startedAt - scheduledAt).count();
                            logger::info(
                                "DragonBoardVR: pinned item '{}' removed in {:.3f} ms "
                                "(queue={:.3f}, layout={:.3f}, scene={:.3f}).",
                                elementID,
                                layoutMs + refreshMs,
                                queueMs,
                                layoutMs,
                                refreshMs);
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
            const std::shared_ptr<vrui::VRUIContainer>& rightHandMagicContainer)
        {
            if (element.visuals.model.empty()) return;
            auto routedElement = element;
            if (routedElement.legacyHmdPin) {
                if (auto* rightHand = menuManager.getRightHandNode()) {
                    const auto inverseHandRotation = rightHand->world.rotate.Transpose();
                    const float handScale = rightHand->world.scale != 0.0f ?
                        rightHand->world.scale : 1.0f;
                    const RE::NiPoint3 worldPosition{
                        routedElement.transform.px,
                        routedElement.transform.py,
                        routedElement.transform.pz };
                    const auto worldRotation = ResolveRotation(routedElement.transform);
                    const auto localPosition =
                        inverseHandRotation * (worldPosition - rightHand->world.translate) /
                        handScale;
                    const auto localRotation = inverseHandRotation * worldRotation;
                    const float localScale = routedElement.transform.scale / handScale;
                    vrui::VRUILayoutManager::updateElementTransformAnywhere(
                        routedElement.id,
                        localPosition,
                        localRotation,
                        localScale,
                        routedElement.visuals.model,
                        routedElement.category,
                        routedElement.formID,
                        routedElement.actionFunc,
                        routedElement.label,
                        false,
                        true,
                        routedElement.visualTransformComposed);
                    if (const auto migrated =
                            vrui::VRUILayoutManager::findElementAnywhere(routedElement.id)) {
                        routedElement = *migrated;
                    }
                }
            }
            auto widget = CreatePinnedWidget(routedElement);
            const bool isWorldPinnedMagic = routedElement.pinToWorld && routedElement.category == "Magic";
            const bool isRightHandPinnedMagic = routedElement.pinToRightHand && routedElement.category == "Magic";
            if (isRightHandPinnedMagic) {
                rightHandMagicContainer->addElement(widget);
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
        auto alwaysVisibleRightHandPanel = menuManager.findPanelByName(kAlwaysVisibleRightHandPanelName);
        if (!persistentPanel || !alwaysVisiblePanel || !alwaysVisibleRightHandPanel) {
            return;
        }

        // Dashboard pins belong to the persistent board layer so switching to
        // Mods (or any other content panel) does not cull them with MainPanel.
        auto fixedContainer = FindOrCreateContainer(persistentPanel, "FixedWidgetsContainer");
        auto worldMagicContainer = FindOrCreateContainer(alwaysVisiblePanel, "AlwaysVisibleWidgetsContainer");
        auto rightHandMagicContainer = FindOrCreateContainer(alwaysVisibleRightHandPanel, "AlwaysVisibleRightHandWidgetsContainer");

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
                rightHandMagicContainer);
        }

        alwaysVisiblePanel->setActive(!worldMagicContainer->getChildren().empty());
        if (worldMagicContainer->getChildren().empty()) {
            alwaysVisiblePanel->hide();
            alwaysVisiblePanel->setVisible(false);
            alwaysVisiblePanel->detachFromParent();
        } else {
            if (auto* leftHand = menuManager.getLeftHandNode()) {
                alwaysVisiblePanel->attachToHandNode(
                    leftHand,
                    menuManager.getPanelOffset());
            }
            alwaysVisiblePanel->show();
        }

        alwaysVisibleRightHandPanel->setActive(!rightHandMagicContainer->getChildren().empty());
        if (rightHandMagicContainer->getChildren().empty()) {
            alwaysVisibleRightHandPanel->hide();
            alwaysVisibleRightHandPanel->setVisible(false);
            alwaysVisibleRightHandPanel->detachFromParent();
        } else {
            if (auto* rightHand = menuManager.getRightHandNode()) {
                alwaysVisibleRightHandPanel->attachToHandNode(
                    rightHand,
                    menuManager.getPanelOffset());
            }
            alwaysVisibleRightHandPanel->show();
        }

        persistentPanel->recalculateLayout();
        alwaysVisiblePanel->recalculateLayout();
        alwaysVisibleRightHandPanel->recalculateLayout();
    }

    void FixedWidgetPresenter::RefreshElement(
        vrui::VRMenuManager& menuManager,
        const std::string& elementId)
    {
        auto persistentPanel = menuManager.findPanelByName(kPersistentPanelName);
        auto alwaysVisiblePanel = menuManager.findPanelByName(kAlwaysVisiblePanelName);
        auto alwaysVisibleRightHandPanel = menuManager.findPanelByName(kAlwaysVisibleRightHandPanelName);
        if (!persistentPanel || !alwaysVisiblePanel || !alwaysVisibleRightHandPanel) return;

        auto fixedContainer = FindOrCreateContainer(
            persistentPanel, "FixedWidgetsContainer", false);
        auto worldMagicContainer = FindOrCreateContainer(
            alwaysVisiblePanel, "AlwaysVisibleWidgetsContainer", false);
        auto rightHandMagicContainer = FindOrCreateContainer(
            alwaysVisibleRightHandPanel, "AlwaysVisibleRightHandWidgetsContainer", false);

        // A pin can change target (dashboard, left hand, or world), so remove
        // only this element from every destination before adding its new state.
        RemoveElementNamed(fixedContainer, elementId);
        RemoveElementNamed(worldMagicContainer, elementId);
        RemoveElementNamed(rightHandMagicContainer, elementId);

        if (const auto element = vrui::VRUILayoutManager::findElementAnywhere(elementId)) {
            AddPinnedWidgetToTarget(
                menuManager,
                *element,
                fixedContainer,
                worldMagicContainer,
                rightHandMagicContainer);
        }

        // Keep the panel routing state correct without rebuilding unrelated pins.
        alwaysVisiblePanel->setActive(!worldMagicContainer->getChildren().empty());
        if (worldMagicContainer->getChildren().empty()) {
            alwaysVisiblePanel->hide();
            alwaysVisiblePanel->setVisible(false);
            alwaysVisiblePanel->detachFromParent();
        } else {
            if (auto* leftHand = menuManager.getLeftHandNode()) {
                alwaysVisiblePanel->attachToHandNode(leftHand, menuManager.getPanelOffset());
            }
            alwaysVisiblePanel->show();
        }

        alwaysVisibleRightHandPanel->setActive(!rightHandMagicContainer->getChildren().empty());
        if (rightHandMagicContainer->getChildren().empty()) {
            alwaysVisibleRightHandPanel->hide();
            alwaysVisibleRightHandPanel->setVisible(false);
            alwaysVisibleRightHandPanel->detachFromParent();
        } else {
            if (auto* rightHand = menuManager.getRightHandNode()) {
                alwaysVisibleRightHandPanel->attachToHandNode(
                    rightHand,
                    menuManager.getPanelOffset());
            }
            alwaysVisibleRightHandPanel->show();
        }

        // removeElement()/addElement() already recalculates the one container
        // that actually changed and propagates that layout change to its
        // parent. Recalculating all six nodes here repeated the expensive
        // scene traversal for every single pin edit/removal.
    }
}
