#include "pch.h"
#include "VRUIItemEditPanel.h"
#include "VRUIButton.h"
#include "VRUISlider.h"
#include "VRMenuManager.h"
#include "VRUISettings.h"
#include "VRUIItemUtils.h"
#include "VRUIModelHelper.h"
#include "VRUILayoutManager.h"

namespace vrui
{
    namespace
    {
        // Calibrated in VR against the RmlUi preview circle. The residual
        // item transform needed to center the mesh was X +1.0 / Y -0.8, so
        // it is baked into the anchor and the editor sliders can open at zero.
        inline constexpr float kEditPanelPreviewAnchorX = 0.0f;
        inline constexpr float kEditPanelPreviewAnchorY = 2.2f;
        inline constexpr float kEditPanelPreviewAnchorZ = 0.23f;
        // In the transformed RmlUi preview, X maps visually to the vertical
        // direction. Calibrated in-game after confirming the axis mapping.
        inline constexpr float kInventoryPreviewAnchorX = 0.50f;
        inline constexpr float kInventoryPreviewAnchorY = 2.2f;
        // The horizontal correction belongs on Z; negative moves the preview
        // toward the visual right with the current panel orientation.
        inline constexpr float kInventoryPreviewAnchorZ = -1.30f;
        // Requested global RmlUi preview increase: previous 1.3 * 1.8 = 2.34.
        inline constexpr float kInventoryPreviewScaleMultiplier = 2.34f;
        inline constexpr float kEditPanelPageAreaX = -4.0f;
        inline constexpr float kEditPanelPageAreaZ = 4.0f;

        bool usesButtonEditorRotationMapping(const std::string& modelPath)
        {
            std::string lower = modelPath;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            return lower.find("dragonboardvr\\iconplane") != std::string::npos ||
                   lower.find("dragonboardvr/iconplane") != std::string::npos ||
                   lower.find("dragonboardvr\\slot") != std::string::npos ||
                   lower.find("dragonboardvr/slot") != std::string::npos ||
                   lower.find("dragonboardvr\\skillsicon") != std::string::npos ||
                   lower.find("dragonboardvr\\inventoryicon") != std::string::npos ||
                   lower.find("dragonboardvr\\magicicon") != std::string::npos ||
                   lower.find("dragonboardvr\\saveicon") != std::string::npos ||
                   lower.find("dragonboardvr\\favoritesicon") != std::string::npos ||
                   lower.find("dragonboardvr\\mapicon") != std::string::npos;
        }

        float editorRotXToRuntime(float editorRotX, float editorRotZ, const std::string& modelPath)
        {
            if (usesButtonEditorRotationMapping(modelPath)) {
                return editorRotZ;
            }
            return editorRotX;
        }

        float editorRotYToRuntime(float editorRotY, const std::string&)
        {
            return editorRotY;
        }

        float editorRotZToRuntime(float editorRotX, float editorRotZ, const std::string& modelPath)
        {
            if (usesButtonEditorRotationMapping(modelPath)) {
                return editorRotX;
            }
            return editorRotZ;
        }

        float runtimeRotXToEditor(float runtimeRotX, float runtimeRotZ, const std::string& modelPath)
        {
            if (usesButtonEditorRotationMapping(modelPath)) {
                return runtimeRotZ;
            }
            return runtimeRotX;
        }

        float runtimeRotYToEditor(float runtimeRotY, const std::string&)
        {
            return runtimeRotY;
        }

        float runtimeRotZToEditor(float runtimeRotX, float runtimeRotZ, const std::string& modelPath)
        {
            if (usesButtonEditorRotationMapping(modelPath)) {
                return runtimeRotX;
            }
            return runtimeRotZ;
        }

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

        void refreshSpacingRecursive(VRUIWidget* widget, float spacingX, float spacingY, float spacingZ)
        {
            if (!widget) {
                return;
            }

            if (auto* container = dynamic_cast<VRUIContainer*>(widget)) {
                container->setSpacing(spacingX, spacingY, spacingZ);
            }

            for (auto& child : widget->getChildren()) {
                refreshSpacingRecursive(child.get(), spacingX, spacingY, spacingZ);
            }
        }
    }

    VRUIItemEditPanel::VRUIItemEditPanel(const std::string& name)
        : VRUIDynamicContainer(name)
    {
    }

    void VRUIItemEditPanel::setTargetItem(const std::string& category, const std::string& itemName, const std::string& modelPath, uint32_t formID,
                                          float rotX, float rotY, float rotZ, float xOff, float yOff, float zOff, float scaleMult,
                                          const std::string& sourcePanel, const std::string& actionFunc)
    {
        // Rebuild once for every newly selected object so the preview mesh is
        // replaced before the RmlUi host enters preview-only mode.
        _rmlPreviewMode = false;
        _rmlPreviewLayout = RmlPreviewLayout::ItemEditor;
        _targetCategory = category;
        _targetFormID = formID;
        _targetItemName = ItemUtils::sanitizeName(itemName);
        _targetModelPath = modelPath;
        _sourcePanel = sourcePanel.empty() ? "InventoryPanel" : sourcePanel;
        _targetActionFunc = actionFunc;
        
        // Exact initial values from the main container UI
        _posX = xOff; _posY = yOff; _posZ = zOff;
        const float runtimeRotX = std::isnan(rotX) ? 0.0f : rotX;
        const float runtimeRotY = std::isnan(rotY) ? 0.0f : rotY;
        const float runtimeRotZ = std::isnan(rotZ) ? 0.0f : rotZ;
        _rotX = runtimeRotXToEditor(runtimeRotX, runtimeRotZ, _targetModelPath);
        _rotY = runtimeRotYToEditor(runtimeRotY, _targetModelPath);
        _rotZ = runtimeRotZToEditor(runtimeRotX, runtimeRotZ, _targetModelPath);
        _scale = scaleMult;

        // Calculate the exact _baseScaleMult equivalent to a 3.0f width UI Inventory button
        auto& settings = VRUISettings::get();
        // Match the dynamic item button: category scale is applied after the
        // normalized base size, including while automatic fitting is enabled.
        float specificMult = settings.itemMiscScale;
        if (_targetCategory == "Weapons") specificMult = settings.itemWeaponScale;
        else if (_targetCategory == "Armor") specificMult = settings.itemArmorScale;
        else if (_targetCategory == "Potions") specificMult = settings.itemPotionScale;
        else if (_targetCategory == "Food") specificMult = settings.itemFoodScale;

        float userMultiplier = settings.itemMeshScale * specificMult;
        if (userMultiplier < 0.1f) {
            userMultiplier = 1.0f; 
        }

        _baseScaleMult = 1.6f * userMultiplier;
        _normalizedScale = 1.0f; // Initialize _normalizedScale here

        refresh();
    }

    void VRUIItemEditPanel::saveOffsets()
    {
        ItemOffsetData data;
        data.posX = _posX; data.posY = _posY; data.posZ = _posZ;
        data.rotX = editorRotXToRuntime(_rotX, _rotZ, _targetModelPath);
        data.rotY = editorRotYToRuntime(_rotY, _targetModelPath);
        data.rotZ = editorRotZToRuntime(_rotX, _rotZ, _targetModelPath);
        data.scale = _scale;
        
        ItemUtils::setItemOverride(RE::TESForm::LookupByID(_targetFormID), data);
        VRMenuManager::get().requestSettingsSave();
        
        // Also apply the new values immediately to existing objects
        VRMenuManager::get().refreshActiveDynamicContainers();
    }

    VRUIItemEditPanel::EditState VRUIItemEditPanel::getEditState() const
    {
        EditState state;
        state.category = _targetCategory;
        state.itemName = _targetItemName;
        state.modelPath = _targetModelPath;
        state.sourcePanel = _sourcePanel;
        state.formID = static_cast<std::uint32_t>(_targetFormID);
        state.posX = _posX;
        state.posY = _posY;
        state.posZ = _posZ;
        state.rotX = _rotX;
        state.rotY = _rotY;
        state.rotZ = _rotZ;
        state.scale = _scale;
        state.magicItem = _targetCategory == "Magic";
        state.boardPinnedToWorld = VRMenuManager::get().isBoardWorldPinned();
        state.canPinToWorld = _previewWidget && _previewWidget->getNode();

        const auto elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
        if (const auto existing = VRUILayoutManager::findElementAnywhere(elementId)) {
            state.labelHidden = existing->hideLabel;
        }
        return state;
    }

    void VRUIItemEditPanel::setWorkingTransform(
        float posX, float posY, float posZ,
        float rotX, float rotY, float rotZ, float scale)
    {
        _posX = std::clamp(posX, -20.0f, 20.0f);
        _posY = std::clamp(posY, -20.0f, 20.0f);
        _posZ = std::clamp(posZ, -20.0f, 20.0f);
        _rotX = std::clamp(rotX, -180.0f, 180.0f);
        _rotY = std::clamp(rotY, -180.0f, 180.0f);
        _rotZ = std::clamp(rotZ, -180.0f, 180.0f);
        _scale = std::clamp(scale, 0.01f, 5.0f);
        // Only the selected preview mesh is updated here. Dynamic inventory
        // containers remain untouched until their source panel is restored.
        updatePreview();
    }

    void VRUIItemEditPanel::setRmlPreviewMode(bool enabled)
    {
        _rmlPreviewMode = enabled;
        for (const auto& child : getChildren()) {
            if (child) {
                child->setVisible(!enabled || child == _previewWidget);
            }
        }
        if (_previewWidget) {
            _previewWidget->setVisible(true);
            updatePreview();
        }
    }

    void VRUIItemEditPanel::setRmlPreviewLayout(RmlPreviewLayout layout)
    {
        _rmlPreviewLayout = layout;
        updateInventoryPreviewInteraction();
        if (_previewWidget) {
            updatePreview();
        }
    }

    void VRUIItemEditPanel::setInventoryPreviewInteractionHandler(
        InventoryPreviewInteractionHandler handler)
    {
        _inventoryPreviewInteractionHandler = std::move(handler);
        updateInventoryPreviewInteraction();
    }

    void VRUIItemEditPanel::updateInventoryPreviewInteraction()
    {
        if (!_previewWidget) return;

        const bool enabled =
            (_rmlPreviewLayout == RmlPreviewLayout::Inventory ||
             _rmlPreviewLayout == RmlPreviewLayout::Magic) &&
            _targetFormID != 0 &&
            static_cast<bool>(_inventoryPreviewInteractionHandler);
        _previewWidget->setDashboardPinned(enabled);
        if (enabled) {
            _previewWidget->setOnPressHandler([this](VRUIButton*, EquipHand hand) {
                if ((_rmlPreviewLayout == RmlPreviewLayout::Inventory ||
                     _rmlPreviewLayout == RmlPreviewLayout::Magic) &&
                    _targetFormID != 0 &&
                    _inventoryPreviewInteractionHandler) {
                    _inventoryPreviewInteractionHandler(
                        static_cast<std::uint32_t>(_targetFormID), hand);
                }
            });
        } else {
            _previewWidget->setOnPressHandler({});
        }
    }

    void VRUIItemEditPanel::applyItemOffsets()
    {
        ItemOffsetData data;
        data.posX = _posX;
        data.posY = _posY;
        data.posZ = _posZ;
        data.rotX = editorRotXToRuntime(_rotX, _rotZ, _targetModelPath);
        data.rotY = editorRotYToRuntime(_rotY, _targetModelPath);
        data.rotZ = editorRotZToRuntime(_rotX, _rotZ, _targetModelPath);
        data.scale = _scale;
        ItemUtils::setItemOverride(RE::TESForm::LookupByID(_targetFormID), data);
        VRMenuManager::get().requestSettingsSave();

        // The source container is hidden while the RmlUi editor is open. It is
        // rebuilt naturally when Back restores it, so do not synchronously
        // recreate all of its 3D items during Apply.
    }

    void VRUIItemEditPanel::applyCategoryOffsets()
    {
        ItemOffsetData data;
        data.posX = _posX;
        data.posY = _posY;
        data.posZ = _posZ;
        data.rotX = editorRotXToRuntime(_rotX, _rotZ, _targetModelPath);
        data.rotY = editorRotYToRuntime(_rotY, _targetModelPath);
        data.rotZ = editorRotZToRuntime(_rotX, _rotZ, _targetModelPath);
        data.scale = _scale;
        VRUISettings::get().categoryOverrides[_targetCategory] = data;
        VRMenuManager::get().requestSettingsSave();
        // See applyItemOffsets(): defer the source container rebuild until it
        // is selected again.
    }

    void VRUIItemEditPanel::resetItemOffsets()
    {
        ItemUtils::eraseItemOverride(RE::TESForm::LookupByID(_targetFormID));
        VRMenuManager::get().requestSettingsSave();
        VRMenuManager::get().refreshActiveDynamicContainers();
    }

    bool VRUIItemEditPanel::pinToDashboard()
    {
        if (VRMenuManager::get().isBoardWorldPinned()) {
            logger::warn(
                "DragonBoardVR: dashboard pin rejected for {:08X} '{}' because the board is world pinned.",
                static_cast<std::uint32_t>(_targetFormID), _targetItemName);
            return false;
        }
        const auto elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
        VRUILayoutManager::updateElementTransformAnywhereDirect(
            elementId,
            { _posX, _posY, _posZ },
            editorRotXToRuntime(_rotX, _rotZ, _targetModelPath),
            editorRotYToRuntime(_rotY, _targetModelPath),
            editorRotZToRuntime(_rotX, _rotZ, _targetModelPath),
            _scale, _targetModelPath, _targetCategory, _targetFormID, _targetActionFunc);
        VRUILayoutManager::setElementPinToWorld(elementId, false);
        VRUILayoutManager::setElementPinToHmdWorld(elementId, false);
        VRMenuManager::get().refreshFixedWidgets();
        logger::info(
            "DragonBoardVR: item {:08X} '{}' pinned to dashboard as '{}'.",
            static_cast<std::uint32_t>(_targetFormID), _targetItemName, elementId);
        return true;
    }

    bool VRUIItemEditPanel::pinToLeftHand()
    {
        if (_targetCategory != "Magic") return false;
        const auto elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
        VRUILayoutManager::updateElementTransformAnywhereDirect(
            elementId,
            { _posX, _posY, _posZ },
            editorRotXToRuntime(_rotX, _rotZ, _targetModelPath),
            editorRotYToRuntime(_rotY, _targetModelPath),
            editorRotZToRuntime(_rotX, _rotZ, _targetModelPath),
            _scale, _targetModelPath, _targetCategory, _targetFormID, _targetActionFunc);
        VRUILayoutManager::setElementPinToWorld(elementId, true);
        VRUILayoutManager::setElementPinToHmdWorld(elementId, false);
        VRMenuManager::get().refreshFixedWidgets();
        return true;
    }

    bool VRUIItemEditPanel::pinToWorld()
    {
        if (_targetCategory != "Magic" || !_previewWidget || !_previewWidget->getNode()) return false;
        const auto elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
        const auto* previewNode = _previewWidget->getNode();
        VRUILayoutManager::updateElementTransformAnywhere(
            elementId,
            previewNode->world.translate,
            previewNode->world.rotate,
            previewNode->world.scale,
            _targetModelPath, _targetCategory, _targetFormID, _targetActionFunc);
        VRUILayoutManager::setElementPinToHmdWorld(elementId, true);
        VRUILayoutManager::setElementPinToWorld(elementId, false);
        VRMenuManager::get().refreshFixedWidgets();
        return true;
    }

    bool VRUIItemEditPanel::togglePinnedLabel()
    {
        const auto elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
        const auto existing = VRUILayoutManager::findElementAnywhere(elementId);
        const bool hideLabel = existing ? !existing->hideLabel : true;
        VRUILayoutManager::setElementHideLabel(elementId, hideLabel);
        VRMenuManager::get().refreshFixedWidgets();
        return hideLabel;
    }

    void VRUIItemEditPanel::syncRotationFromPreviewGrab(const RE::NiMatrix3& localRotation)
    {
        float runtimeRotX = 0.0f;
        float runtimeRotY = 0.0f;
        float runtimeRotZ = 0.0f;
        VRUILayoutManager::getMatrixEuler(localRotation, runtimeRotX, runtimeRotY, runtimeRotZ);

        runtimeRotX /= kDegToRad;
        runtimeRotY /= kDegToRad;
        runtimeRotZ /= kDegToRad;

        _rotX = runtimeRotXToEditor(runtimeRotX, runtimeRotZ, _targetModelPath);
        _rotY = runtimeRotYToEditor(runtimeRotY, _targetModelPath);
        _rotZ = runtimeRotZToEditor(runtimeRotX, runtimeRotZ, _targetModelPath);
    }

    void VRUIItemEditPanel::updatePreview()
    {
        if (_previewWidget) {
            const bool listPanelLayout =
                _rmlPreviewLayout == RmlPreviewLayout::Inventory ||
                _rmlPreviewLayout == RmlPreviewLayout::Magic;
            const float anchorX = listPanelLayout ? kInventoryPreviewAnchorX : kEditPanelPreviewAnchorX;
            const float anchorY = listPanelLayout ? kInventoryPreviewAnchorY : kEditPanelPreviewAnchorY;
            const float anchorZ = listPanelLayout ? kInventoryPreviewAnchorZ : kEditPanelPreviewAnchorZ;
            const float previewScale = listPanelLayout ? kInventoryPreviewScaleMultiplier : 1.0f;
            RE::NiMatrix3 rot;
            VRUILayoutManager::setMatrixEuler(
                rot,
                editorRotXToRuntime(_rotX, _rotZ, _targetModelPath) * 0.017453292f,
                editorRotYToRuntime(_rotY, _targetModelPath) * 0.017453292f,
                editorRotZToRuntime(_rotX, _rotZ, _targetModelPath) * 0.017453292f
            );
            
            _previewWidget->setLocalRotation(RE::NiMatrix3());
            _previewWidget->setLocalPosition({
                anchorX,
                anchorY,
                anchorZ
            });
            _previewWidget->setLocalScale(_normalizedScale * _baseScaleMult * previewScale);
            _previewWidget->setPrimaryVisualTransform({ _posX, _posY, _posZ }, rot, _scale);
        }
    }

    void VRUIItemEditPanel::setEditPage(int index)
    {
        if (index < 0 || index >= _editPages.size()) return;
        _activeTab = index;

        // Find the page area in our layout
        if (auto pageArea = dynamic_cast<VRUIContainer*>(findWidgetByName(_name + "_pageArea"))) {
            pageArea->clearElements();
            pageArea->addElement(_editPages[index]);
            _editPages[index]->setVisible(true);
        }

        recalculateLayout();
        VRMenuManager::get().refreshActiveDynamicContainers();
    }

    void VRUIItemEditPanel::refresh()
    {
        if (_rmlPreviewMode) {
            updatePreview();
            return;
        }

        // Use Free layout for the main panel to anchor components at precise MCM-style heights
        setLayout(ContainerLayout::Free);
        clearElements();
        _editPages.clear();

        std::string titleText = "Editing: " + _targetItemName;
        auto titleBtn = std::make_shared<VRUIButton>(_name + "_title", "", "", 8.0f, 0.8f);
        titleBtn->setMaxCharsPerLine(64); // Prevent wrapping in the edit panel title
        titleBtn->setLabel(titleText);
        titleBtn->setLocalPosition({ 0.0f, 0.0f, 9.0f }); // Anchor at very top
        addElement(titleBtn);

        // Tab Navigation Bar (MCM Style)
        auto tabBar = std::make_shared<VRUIContainer>(_name + "_tabs", ContainerLayout::HorizontalCenter, 0.4f);
        tabBar->setLocalPosition({ 0.0f, 0.0f, 7.0f }); // Lowered to 7.0f
        
        std::vector<std::string> tabNames = {"Position", "Rotation", "Scale", "Pin"};
        for (int i = 0; i < tabNames.size(); ++i) {
            auto tabBtn = std::make_shared<VRUIButton>(tabNames[i], "", "", 1.8f, 0.8f);
            tabBtn->setOnPressHandler([this, i](VRUIButton*, EquipHand) {
                this->setEditPage(i);
            });
            tabBar->addElement(tabBtn);
        }
        addElement(tabBar);

        // Preview Area (3D Model) - reserve space on the right side of the panel
        auto previewRow = std::make_shared<VRUIContainer>(_name + "_preview", ContainerLayout::HorizontalCenter, 1.0f);
        previewRow->setLocalPosition({ kEditPanelPreviewAnchorX, 0.0f, kEditPanelPreviewAnchorZ + 1.6f });
        // The spacer reserves a non-interactive area where the preview mesh lives.
        auto previewSpacer = std::make_shared<VRUIButton>("", "", "", 4.0f, 1.0f);
        previewRow->addElement(previewSpacer);
        addElement(previewRow);

        if (_previewWidget) {
            removeChild(_previewWidget);
            _previewWidget = nullptr;
        }

        if (!_targetModelPath.empty()) {
            // Replicate exactly what Dashboard does
            auto* targetForm = RE::TESForm::LookupByID(_targetFormID);
            const auto transformSource = ItemUtils::getItemTransformSource(targetForm);
            _previewWidget = std::make_shared<VRUIButton>(
                "", _targetModelPath, "", 1.0f, 1.0f,
                editorRotXToRuntime(_rotX, _rotZ, _targetModelPath),
                editorRotYToRuntime(_rotY, _targetModelPath),
                editorRotZToRuntime(_rotX, _rotZ, _targetModelPath),
                _posX, _posY, _posZ, _scale, false, transformSource);

            // For untouched items, synchronize the editor with the automatic
            // baseline (including a NIF inventory marker if one was used).
            // Explicit INI values remain unchanged and are never replaced.
            if (VRUISettings::get().normalizeItemVisuals &&
                !ItemUtils::isExplicitOverride(transformSource)) {
                RE::NiPoint3 resolvedPosition;
                RE::NiMatrix3 resolvedRotation;
                float resolvedScale = 1.0f;
                if (_previewWidget->getPrimaryVisualTransform(
                        resolvedPosition, resolvedRotation, resolvedScale)) {
                    _posX = resolvedPosition.x;
                    _posY = resolvedPosition.y;
                    _posZ = resolvedPosition.z;
                    _scale = resolvedScale;
                    syncRotationFromPreviewGrab(resolvedRotation);
                }
            }
            
            if (_previewWidget) {
                _previewWidget->setItemRotationPersistence(_targetFormID, _posX, _posY, _posZ, _scale);
                _previewWidget->setOnGrabReleaseHandler([this](VRUIButton* btn) {
                    if (!btn) {
                        return;
                    }
                    RE::NiPoint3 position;
                    RE::NiMatrix3 rotation;
                    float scale = 1.0f;
                    if (!btn->getPrimaryVisualTransform(position, rotation, scale)) {
                        return;
                    }

                    _posX = std::clamp(position.x, -20.0f, 20.0f);
                    _posY = std::clamp(position.y, -20.0f, 20.0f);
                    _posZ = std::clamp(position.z, -20.0f, 20.0f);
                    // Preserve the exact scale reached by two-hand editing.
                    // Existing item overrides can legitimately exceed the
                    // slider's ordinary range.
                    _scale = std::max(0.01f, scale);
                    syncRotationFromPreviewGrab(rotation);

                    // Keep the exact released scene transform visible. The
                    // RmlUi draft and sliders are synchronized separately,
                    // without reconstructing the preview in the same frame.
                    if (_workingTransformChangedHandler) {
                        _workingTransformChangedHandler(getEditState());
                    }
                });
                addChild(_previewWidget);
            }
        }
        updateInventoryPreviewInteraction();
        updatePreview();

        // Page Host Area (MCM Style Content Area)
        auto pageArea = std::make_shared<VRUIContainer>(_name + "_pageArea", ContainerLayout::VerticalDown, 1.0f);
        pageArea->setLocalPosition({ kEditPanelPageAreaX, 0.0f, kEditPanelPageAreaZ }); // Anchor content on the left side
        addElement(pageArea);

        // Create the actual content pages
        auto pagePos = std::make_shared<VRUIContainer>(_name + "_pagePos", ContainerLayout::VerticalDown, 1.2f);
        auto pageRot = std::make_shared<VRUIContainer>(_name + "_pageRot", ContainerLayout::VerticalDown, 1.2f);
        auto pageScl = std::make_shared<VRUIContainer>(_name + "_pageScl", ContainerLayout::VerticalDown, 1.2f);

        _editPages.push_back(pagePos);
        _editPages.push_back(pageRot);
        _editPages.push_back(pageScl);

        // --- PAGE 4: PIN ---
        auto pagePin = std::make_shared<VRUIContainer>(_name + "_page_pin", ContainerLayout::VerticalDown, 1.5f);
        const bool boardPinnedToWorld = VRMenuManager::get().isBoardWorldPinned();

        auto pinBtn = std::make_shared<VRUIButton>(boardPinnedToWorld ? "BOARD PINNED TO WORLD" : "PIN TO DASHBOARD", "DragonBoardVR/IsEquipped.nif", "", 6.0f, 0.8f);
        pinBtn->setOnPressHandler([this](VRUIButton*, EquipHand) {
            if (VRMenuManager::get().isBoardWorldPinned()) {
                return;
            }

            // Priority: JSON Layout (Dashboard container)
            std::string elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
            
            // Generate rotation matrix from current Euler angles
            RE::NiMatrix3 rot;
            VRUILayoutManager::setMatrixEuler(
                rot,
                editorRotXToRuntime(_rotX, _rotZ, _targetModelPath) * kDegToRad,
                editorRotYToRuntime(_rotY, _targetModelPath) * kDegToRad,
                editorRotZToRuntime(_rotX, _rotZ, _targetModelPath) * kDegToRad
            );
            
            VRUILayoutManager::updateElementTransformAnywhereDirect(elementId,
                { _posX, _posY, _posZ },
                editorRotXToRuntime(_rotX, _rotZ, _targetModelPath),
                editorRotYToRuntime(_rotY, _targetModelPath),
                editorRotZToRuntime(_rotX, _rotZ, _targetModelPath),
                _scale,
                _targetModelPath,
                _targetCategory,
                _targetFormID,
                _targetActionFunc);
            VRUILayoutManager::setElementPinToWorld(elementId, false);
            VRUILayoutManager::setElementPinToHmdWorld(elementId, false);
            
            // Refresh HUD to show new pin from JSON
            VRMenuManager::get().refreshFixedWidgets();
        });
        pagePin->addElement(pinBtn);

        if (_targetCategory == "Magic") {
            auto pinWorldBtn = std::make_shared<VRUIButton>("PIN TO LHAND", "DragonBoardVR/IsEquipped.nif", "", 6.0f, 0.8f);
            pinWorldBtn->setOnPressHandler([this](VRUIButton*, EquipHand) {
                std::string elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);

                VRUILayoutManager::updateElementTransformAnywhereDirect(
                    elementId,
                    { _posX, _posY, _posZ },
                    editorRotXToRuntime(_rotX, _rotZ, _targetModelPath),
                    editorRotYToRuntime(_rotY, _targetModelPath),
                    editorRotZToRuntime(_rotX, _rotZ, _targetModelPath),
                    _scale,
                    _targetModelPath,
                    _targetCategory,
                    _targetFormID,
                    _targetActionFunc);
                VRUILayoutManager::setElementPinToWorld(elementId, true);
                VRUILayoutManager::setElementPinToHmdWorld(elementId, false);
                VRMenuManager::get().refreshFixedWidgets();
            });
            pagePin->addElement(pinWorldBtn);

            auto pinHmdWorldBtn = std::make_shared<VRUIButton>("PIN TO WORLD", "DragonBoardVR/IsEquipped.nif", "", 6.0f, 0.8f);
            pinHmdWorldBtn->setOnPressHandler([this](VRUIButton*, EquipHand) {
                if (!_previewWidget || !_previewWidget->getNode()) {
                    return;
                }

                std::string elementId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
                RE::NiNode* previewNode = _previewWidget->getNode();
                RE::NiPoint3 worldPos = previewNode->world.translate;
                RE::NiMatrix3 worldRot = previewNode->world.rotate;

                VRUILayoutManager::updateElementTransformAnywhere(
                    elementId,
                    worldPos,
                    worldRot,
                    previewNode->world.scale,
                    _targetModelPath,
                    _targetCategory,
                    _targetFormID,
                    _targetActionFunc);
                VRUILayoutManager::setElementPinToHmdWorld(elementId, true);
                VRUILayoutManager::setElementPinToWorld(elementId, false);
                VRMenuManager::get().refreshFixedWidgets();
            });
            pagePin->addElement(pinHmdWorldBtn);
        }

        // Toggle: Hide/Show label on pinned dashboard item
        {
            std::string elemId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
            auto existing = VRUILayoutManager::findElementAnywhere(elemId);
            bool currentlyHidden = existing ? existing->hideLabel : false;

            auto labelToggleBtn = std::make_shared<VRUIButton>(
                currentlyHidden ? "LABEL: OCULTO" : "LABEL: VISIVEL", "", "", 6.0f, 0.8f);

            labelToggleBtn->setOnPressHandler([this, labelToggleBtn](VRUIButton*, EquipHand) mutable {
                std::string elemId = "Pinned_" + _targetItemName + "_" + std::to_string(_targetFormID);
                auto existing = VRUILayoutManager::findElementAnywhere(elemId);
                bool newHide = existing ? !existing->hideLabel : true;
                VRUILayoutManager::setElementHideLabel(elemId, newHide);
                labelToggleBtn->setLabel(newHide ? "LABEL: OCULTO" : "LABEL: VISIVEL");
                VRMenuManager::get().refreshFixedWidgets();
            });
            pagePin->addElement(labelToggleBtn);
        }

        _editPages.push_back(pagePin);

        auto addValueRow = [this](const std::string& label, float& valueRef, float minVal, float maxVal, float step, std::shared_ptr<VRUIContainer> parent) {
            auto row = std::make_shared<VRUIContainer>(_name + "_row_" + label, ContainerLayout::HorizontalCenter, 0.4f);
            auto plusBtn = std::make_shared<VRUIButton>(">>>", "", "", 0.9f, 0.5f);
            auto labelWidget = std::make_shared<VRUIButton>("", "", "", 3.0f, 0.5f);
            auto minusBtn = std::make_shared<VRUIButton>("<<<", "", "", 0.9f, 0.5f);
            
            auto updateLabel = [label, labelWidget, &valueRef]() {
                labelWidget->setLabel(std::format("{}: {:.2f}", label, valueRef));
            };
            updateLabel();
            
            minusBtn->setOnPressHandler([this, updateLabel, &valueRef, minVal, step](VRUIButton*, EquipHand) {
                valueRef = std::max(minVal, valueRef - step);
                updateLabel(); this->updatePreview();
            });
            plusBtn->setOnPressHandler([this, updateLabel, &valueRef, maxVal, step](VRUIButton*, EquipHand) {
                valueRef = std::min(maxVal, valueRef + step);
                updateLabel(); this->updatePreview();
            });
            
            row->addElement(plusBtn);
            row->addElement(labelWidget);
            row->addElement(minusBtn);
            parent->addElement(row);
        };

        addValueRow("Pos X", _posX, -20.0f, 20.0f, 0.1f, pagePos);
        addValueRow("Pos Y", _posY, -20.0f, 20.0f, 0.1f, pagePos);
        addValueRow("Pos Z", _posZ, -20.0f, 20.0f, 0.1f, pagePos);
        addValueRow("Rot X", _rotX, -180.0f, 180.0f, 1.0f, pageRot);
        addValueRow("Rot Y", _rotY, -180.0f, 180.0f, 1.0f, pageRot);
        addValueRow("Rot Z", _rotZ, -180.0f, 180.0f, 1.0f, pageRot);
        addValueRow("Scale Override", _scale, 0.01f, 5.0f, 0.01f, pageScl);

        // Actions Footer (Fixed at the absolute bottom like MCM)
        auto footer = std::make_shared<VRUIContainer>(_name + "_footer", ContainerLayout::HorizontalCenter, 0.5f);
        footer->setLocalPosition({ 0.0f, 0.0f, -6.8f });
        
        auto btnSaveItem = std::make_shared<VRUIButton>("Apply Item", "", "", 1.8f, 0.8f);
        btnSaveItem->setOnPressHandler([this](VRUIButton*, EquipHand) { saveOffsets(); });
        footer->addElement(btnSaveItem);

        auto btnSaveCategory = std::make_shared<VRUIButton>("Apply Category", "", "", 1.8f, 0.8f);
        btnSaveCategory->setOnPressHandler([this](VRUIButton*, EquipHand) {
            ItemOffsetData data;
            data.posX = _posX; data.posY = _posY; data.posZ = _posZ;
            data.rotX = editorRotXToRuntime(_rotX, _rotZ, _targetModelPath);
            data.rotY = editorRotYToRuntime(_rotY, _targetModelPath);
            data.rotZ = editorRotZToRuntime(_rotX, _rotZ, _targetModelPath);
            data.scale = _scale;
            VRUISettings::get().categoryOverrides[_targetCategory] = data;
            VRMenuManager::get().requestSettingsSave();
            VRMenuManager::get().refreshActiveDynamicContainers();
        });
        footer->addElement(btnSaveCategory);

        auto btnReset = std::make_shared<VRUIButton>("Reset", "", "", 1.2f, 0.8f);
        btnReset->setOnPressHandler([this](VRUIButton*, EquipHand) {
            ItemUtils::eraseItemOverride(RE::TESForm::LookupByID(_targetFormID));
            VRMenuManager::get().requestSettingsSave();
            VRMenuManager::get().refreshActiveDynamicContainers();
            VRMenuManager::get().switchToPanel(_sourcePanel);
        });
        footer->addElement(btnReset);

        auto btnBack = std::make_shared<VRUIButton>("Back", "", "", 1.2f, 0.8f);
        btnBack->setOnPressHandler([this](VRUIButton*, EquipHand) {
            VRMenuManager::get().switchToPanel(_sourcePanel);
        });
        footer->addElement(btnBack);
        
        addElement(footer);

        // Initialize display
        setEditPage(_activeTab);
        refreshLabelsRecursive(this);
        {
            auto& settings = VRUISettings::get();
            refreshSpacingRecursive(this, settings.buttonSpacingX, settings.buttonSpacingY, 0.0f);
        }
        recalculateLayout();
    }
}
