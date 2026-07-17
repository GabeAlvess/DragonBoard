#include "VRUIModsContainer.h"
#include "game/actions/ActionExecutor.h"
#include "ModActionManager.h"
#include "VRUIButton.h"
#include "VRMenuManager.h"
#include "VRUIItemEditPanel.h"
#include "VRUIItemUtils.h"

namespace vrui {

    namespace actions = dragonboard::game::actions;

    namespace
    {
        [[nodiscard]] actions::EquipSide ToEquipSide(EquipHand hand)
        {
            return hand == EquipHand::kLeft ? actions::EquipSide::kLeft : actions::EquipSide::kRight;
        }
    }

    VRUIModsContainer::VRUIModsContainer(const std::string& name)
        : VRUIDynamicContainer(name)
    {
    }

    void VRUIModsContainer::refresh()
    {
        clearElements();

        // Dynamic buttons from ModActionManager
        auto actions = ModActionManager::get().getActions();
        for (size_t i = 0; i < actions.size(); ++i) {
            const auto& action = actions[i];
            auto btn = std::make_shared<VRUIButton>(action.label, action.iconPath, "textures\\test.dds", 2.0f, 2.0f);
            
            const auto parsedAction = actions::Parse(action.command);
            if (parsedAction.kind != actions::ActionKind::kUnknown) {
                btn->setOnPressHandler([parsedAction](VRUIButton*, EquipHand hand) {
                    VRMenuManager::get().toggleMenu();
                    (void)actions::Execute(
                        parsedAction,
                        ToEquipSide(hand),
                        actions::ExecutionContext::kModsPanel);
                });
            } else {
                btn->setOnPressHandler([command = action.command](VRUIButton*, EquipHand) {
                    // RE::DebugNotification(("Unknown Action: " + command).c_str());
                });
            }

            // Secondary press: Open Edit Panel for pinning
            btn->setOnSecondaryPressHandler([action](VRUIButton*, EquipHand) {
                auto editPanel = std::dynamic_pointer_cast<VRUIItemEditPanel>(VRMenuManager::get().findPanelByName("ItemEditPanel"));
                if (editPanel) {
                    const auto parsedAction = actions::Parse(action.command);
                    uint32_t fId = parsedAction.formID;
                    std::string category = "Mods";
                    if (parsedAction.kind == actions::ActionKind::kCastPower) {
                        category = "Magic";
                    } else if (parsedAction.kind == actions::ActionKind::kEquipItem) {
                        category = "Misc";
                    }
                    
                    editPanel->setTargetItem(category, action.label, action.iconPath, fId, 
                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, "ModsPanel", action.command);
                    VRMenuManager::get().switchToPanel("ItemEditPanel");
                }
            });

            addElement(btn);
        }

        recalculateLayout();
    }
}
