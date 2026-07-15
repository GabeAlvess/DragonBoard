#include "VRUIMenuMCM.h"
#include "VRUISettings.h"
#include "VRMenuManager.h"
#include <cstdio>

namespace vrui
{
    namespace
    {
        void refreshLabelsRecursive(VRUIWidget* widget)
        {
            if (!widget) {
                return;
            }

            if (auto* btn = dynamic_cast<VRUIButton*>(widget)) {
                btn->refreshLabel();
            }

            for (auto& child : widget->getChildren()) {
                refreshLabelsRecursive(child.get());
            }
        }
    }

    VRUIMenuMCM::VRUIMenuMCM(const std::string& name)
        : VRUIPanel(name)
    {
    }

    void VRUIMenuMCM::initializeVisuals()
    {
        VRUIPanel::initializeVisuals();
        
        // Main container uses Free layout so we can explicitly position tabs vs content
        _container = std::make_shared<VRUIContainer>(_name + "_MCMContainer", ContainerLayout::Free, 0.0f);
        _container->setLocalPosition({0.0f, 0.0f, 0.0f});
        addElement(_container);

        auto& settings = VRUISettings::get();

        // 1. Create Navigation Bar (Tabs) - fixed near top
        auto tabBar = std::make_shared<VRUIContainer>(_name + "_TabBar", ContainerLayout::HorizontalCenter, 1.0f);
        tabBar->setLocalPosition({0.0f, 0.0f, 8.0f});
        _container->addElement(tabBar);
        

        // 2. Create Content Area to hold the pages - starts fixed distance below tabs
        _contentArea = std::make_shared<VRUIContainer>(_name + "_ContentArea", ContainerLayout::Free, 0.0f);
        _contentArea->setLocalPosition({0.0f, 0.0f, 4.5f});
        _container->addElement(_contentArea);
        



        // -- Create Pages --
        auto pageGeneral = std::make_shared<VRUIContainer>("Page_General", ContainerLayout::VerticalDown, 0.7f);
        auto pageOffsets = std::make_shared<VRUIContainer>("Page_Offsets", ContainerLayout::VerticalDown, 0.7f);
        auto pageVisuals = std::make_shared<VRUIContainer>("Page_Visuals", ContainerLayout::VerticalDown, 0.7f);
        auto pageItemScales = std::make_shared<VRUIContainer>("Page_ItemScales", ContainerLayout::VerticalDown, 0.7f);
        auto pageLabels = std::make_shared<VRUIContainer>("Page_Labels", ContainerLayout::VerticalDown, 0.7f);

        _categoryPages.push_back(pageGeneral);
        _categoryPages.push_back(pageOffsets);
        _categoryPages.push_back(pageVisuals);
        _categoryPages.push_back(pageItemScales);
        _categoryPages.push_back(pageLabels);


        for (auto& page : _categoryPages) {
            _contentArea->addElement(page);
            page->setVisible(false); // Hide all initially
        }

        // --- PAGE 1: GENERAL & LAYOUT ---
        addSettingRow(pageGeneral, "Menu Scale", "fMenuScale", 0.05f,
            [&]() { return settings.menuScale; },
            [&](float val) { settings.menuScale = val; });

        addToggleRow(pageGeneral, "Edit Mode", "bEditModeEnabled",
            [&]() { return settings.editModeEnabled; },
            [&](bool val) { settings.editModeEnabled = val; });

        {
            auto worldPinBtn = std::make_shared<VRUIButton>("Board World Pin", "DragonBoardVR/slot01.nif", "textures/test.dds", 4.0f, 0.8f);
            auto updateWorldPinLabel = [worldPinBtn]() {
                bool pinned = VRMenuManager::get().isBoardWorldPinned();
                worldPinBtn->setLabel(pinned ? "BOARD: PINNED TO WORLD" : "BOARD: FOLLOWING HAND");
            };
            updateWorldPinLabel();
            worldPinBtn->setOnPressHandler([updateWorldPinLabel](VRUIButton*, EquipHand) {
                auto& manager = VRMenuManager::get();
                manager.setBoardWorldPinned(!manager.isBoardWorldPinned());
                updateWorldPinLabel();
                manager.refreshActivePanels();
            });
            pageGeneral->addElement(worldPinBtn);
        }

        addSettingRow(pageGeneral, "Spacing X", "fButtonSpacingX", 0.1f,
            [&]() { return settings.buttonSpacingX; },
            [&](float val) { settings.buttonSpacingX = val; });
            
        addSettingRow(pageGeneral, "Spacing Y", "fButtonSpacingY", 0.1f,
            [&]() { return settings.buttonSpacingY; },
            [&](float val) { settings.buttonSpacingY = val; });


        addSettingRow(pageOffsets, "Menu Pos X (Right)", "fMenuOffsetX", 0.5f,
            [&]() { return settings.menuOffsetX; },
            [&](float val) { settings.menuOffsetX = val; });

        addSettingRow(pageOffsets, "Menu Pos Y (Fwd)", "fMenuOffsetY", 0.5f,
            [&]() { return settings.menuOffsetY; },
            [&](float val) { settings.menuOffsetY = val; });

        addSettingRow(pageOffsets, "Menu Pos Z (Up)", "fMenuOffsetZ", 0.5f,
            [&]() { return settings.menuOffsetZ; },
            [&](float val) { settings.menuOffsetZ = val; });

        addSettingRow(pageOffsets, "Menu Rot X (Pitch)", "fMenuRotX", 5.0f,
            [&]() { return settings.menuRotX; },
            [&](float val) { settings.menuRotX = val; });

        addSettingRow(pageOffsets, "Menu Rot Y (Roll)", "fMenuRotY", 5.0f,
            [&]() { return settings.menuRotY; },
            [&](float val) { settings.menuRotY = val; });

        addSettingRow(pageOffsets, "Menu Rot Z (Yaw)", "fMenuRotZ", 5.0f,
            [&]() { return settings.menuRotZ; },
            [&](float val) { settings.menuRotZ = val; });
            
        // --- PAGE 3: SPECIFIC VISUALS ---
        addSettingRow(pageVisuals, "Button Scale", "fButtonMeshScale", 0.01f,
            [&]() { return settings.buttonMeshScale; },
            [&](float val) { settings.buttonMeshScale = val; });

        addSettingRow(pageVisuals, "Item Mesh Scale", "fItemMeshScale", 0.01f,
            [&]() { return settings.itemMeshScale; },
            [&](float val) { settings.itemMeshScale = val; });

        addSettingRow(pageVisuals, "Cont Grid Z Offset", "fContainerGridOffsetZ", 0.5f,
            [&]() { return settings.containerGridOffsetZ; },
            [&](float val) { settings.containerGridOffsetZ = val; });

        addSettingRow(pageVisuals, "Reticle Scale",   "fReticleScaleX", 0.5f,
            [&]() { return settings.reticleScaleX; },
            [&](float val) { settings.reticleScaleX = settings.reticleScaleY = settings.reticleScaleZ = val; });

        // --- PAGE 4: INDIVIDUAL SCALES ---
        addSettingRow(pageItemScales, "Weapon Scale", "fItemWeaponScale", 0.05f,
            [&]() { return settings.itemWeaponScale; },
            [&](float val) { settings.itemWeaponScale = std::max(0.01f, val); });

        addSettingRow(pageItemScales, "Armor/Clothes Scale", "fItemArmorScale", 0.05f,
            [&]() { return settings.itemArmorScale; },
            [&](float val) { settings.itemArmorScale = std::max(0.01f, val); });

        addSettingRow(pageItemScales, "Potion Scale", "fItemPotionScale", 0.05f,
            [&]() { return settings.itemPotionScale; },
            [&](float val) { settings.itemPotionScale = std::max(0.01f, val); });
            
        addSettingRow(pageItemScales, "Food/Ingred. Scale", "fItemFoodScale", 0.05f,
            [&]() { return settings.itemFoodScale; },
            [&](float val) { settings.itemFoodScale = std::max(0.01f, val); });

        addSettingRow(pageItemScales, "Misc/Book Scale", "fItemMiscScale", 0.05f,
            [&]() { return settings.itemMiscScale; },
            [&](float val) { settings.itemMiscScale = std::max(0.01f, val); });

        // --- PAGE 5: LABELS ---
        addSettingRow(pageLabels, "Label Scale", "fLabelScale", 0.01f,
            [&]() { return settings.labelScale; },
            [&](float val) { settings.labelScale = std::max(0.01f, val); });

        addSettingRow(pageLabels, "Label Spacing", "fLabelSpacing", 0.01f,
            [&]() { return settings.labelSpacing; },
            [&](float val) { settings.labelSpacing = val; });

        addSettingRow(pageLabels, "Label Pos X", "fLabelXOffset", 0.01f,
            [&]() { return settings.labelXOffset; },
            [&](float val) { settings.labelXOffset = val; });

        addSettingRow(pageLabels, "Label Pos Y", "fLabelYOffset", 0.01f,
            [&]() { return settings.labelYOffset; },
            [&](float val) { settings.labelYOffset = val; });

        addSettingRow(pageLabels, "Label Pos Z", "fLabelZOffset", 0.01f,
            [&]() { return settings.labelZOffset; },
            [&](float val) { settings.labelZOffset = val; });

        // -- Setup Tab Bar Buttons --
        std::vector<std::string> tabNames = {"General", "Offsets", "Visuals", "Scale", "Labels"};
        for (int i = 0; i < tabNames.size(); ++i) {
            auto tabBtn = std::make_shared<VRUIButton>("Tab_" + tabNames[i], "DragonBoardVR/slot01.nif", "textures/test.dds", 2.0f, 1.0f);
            tabBtn->setLabel(tabNames[i]);
            tabBtn->setOnPressHandler([weakSelf = std::weak_ptr<VRUIWidget>(weak_from_this()), i](VRUIButton*, EquipHand) {
                if (auto self = std::dynamic_pointer_cast<VRUIMenuMCM>(weakSelf.lock())) {
                    self->setCategoryPage(i);
                }
            });
            tabBar->addElement(tabBtn);
        }

        auto addSaveButtonToPage = [](std::shared_ptr<VRUIContainer> page) {
            auto saveBtn = std::make_shared<VRUIButton>("Save", "DragonBoardVR/slot01.nif", "textures/test.dds", 2.0f, 1.0f);
            saveBtn->setLabel("SAVE INI");
            saveBtn->setOnPressHandler([](VRUIButton*, EquipHand) {
                VRMenuManager::get().saveSettingsNow();
            });
            page->addElement(saveBtn);
        };
        for (auto& p : _categoryPages) {
            addSaveButtonToPage(p);
        }

        // Show the first page by default
        setCategoryPage(0);
        refreshLabelsRecursive(this);
        recalculateLayout();
    }

    void VRUIMenuMCM::setCategoryPage(int index)
    {
        for (int i = 0; i < _categoryPages.size(); ++i) {
            if (_categoryPages[i]) {
                _categoryPages[i]->setVisible(i == index);
            }
        }
        
        if (_contentArea) _contentArea->recalculateLayout();
        VRMenuManager::get().refreshActivePanels();
    }

    void VRUIMenuMCM::show()
    {
        VRUIPanel::show();
    }

    void VRUIMenuMCM::recalculateLayout()
    {
        // Let base class position all children using VerticalDown layout
        VRUIPanel::recalculateLayout();
    }

    void VRUIMenuMCM::addSettingRow(std::shared_ptr<VRUIContainer> parent,
                                 const std::string& label, 
                                 const std::string& settingKey,
                                 float step,
                                 std::function<float()> getter,
                                 std::function<void(float)> setter)
    {
        // Row Layout: [-] [ LABEL : VALUE ] [+]
        auto row = std::make_shared<VRUIContainer>(_name + "_row_" + settingKey, ContainerLayout::HorizontalCenter, 1.0f);
        
        auto minusBtn = std::make_shared<VRUIButton>("<<<", "DragonBoardVR/slot01.nif", "textures/test.dds", 0.9f, 0.5f);
        
        auto labelWidget = std::make_shared<VRUIButton>(label, "DragonBoardVR/slot01.nif", "textures/test.dds", 4.0f, 0.5f);
        
        auto updateLabel = [label, labelWidget, getter]() {
            std::string formatted = std::format("{}: {:.2f}", label, getter());
            labelWidget->setLabel(formatted);
        };
        updateLabel();

        // Interaction logic
        minusBtn->setOnPressHandler([updateLabel, getter, setter, step](VRUIButton*, EquipHand) {
            setter(getter() - step);
            updateLabel();
            VRMenuManager::get().refreshActivePanels();
        });

        // Plus Button
        auto plusBtn = std::make_shared<VRUIButton>(">>>", "DragonBoardVR/slot01.nif", "textures/test.dds", 0.9f, 0.5f);
        plusBtn->setOnPressHandler([updateLabel, getter, setter, step](VRUIButton*, EquipHand) {
            setter(getter() + step);
            updateLabel();
            VRMenuManager::get().refreshActivePanels();
        });

        // Add to row in order: [Plus] [Display] [Minus]
        row->addElement(plusBtn);
        row->addElement(labelWidget);
        row->addElement(minusBtn);

        parent->addElement(row);
    }

    void VRUIMenuMCM::addToggleRow(std::shared_ptr<VRUIContainer> parent,
                                 const std::string& label, 
                                 const std::string& settingKey,
                                 std::function<bool()> getter,
                                 std::function<void(bool)> setter)
    {
        // Toggle row is just a single wide button
        auto row = std::make_shared<VRUIContainer>(_name + "_toggle_row_" + settingKey, ContainerLayout::HorizontalCenter, 1.0f);
        auto toggleBtn = std::make_shared<VRUIButton>(label, "DragonBoardVR/slot01.nif", "textures/test.dds", 6.0f, 0.4f);
        
        auto updateLabel = [label, toggleBtn, getter]() {
            std::string status = getter() ? ": ON" : ": OFF";
            toggleBtn->setLabel(label + status);
        };
        updateLabel();

        toggleBtn->setOnPressHandler([updateLabel, getter, setter](VRUIButton*, EquipHand) {
            setter(!getter());
            updateLabel();
            VRMenuManager::get().refreshActivePanels();
        });

        row->addElement(toggleBtn);
        parent->addElement(row);
    }
}
