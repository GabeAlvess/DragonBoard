#pragma once

#include "VRUIDynamicContainer.h"
#include <cstdint>
#include <functional>
#include <string>

namespace vrui
{
    class VRUIItemEditPanel : public VRUIDynamicContainer
    {
    public:
        using InventoryPreviewInteractionHandler =
            std::function<void(std::uint32_t, EquipHand)>;

        enum class RmlPreviewLayout : std::uint8_t
        {
            ItemEditor,
            Inventory,
            Magic
        };

        struct EditState
        {
            std::string category;
            std::string itemName;
            std::string modelPath;
            std::string sourcePanel;
            std::uint32_t formID = 0;
            float posX = 0.0f;
            float posY = 0.0f;
            float posZ = 0.0f;
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float scale = 1.0f;
            bool magicItem = false;
            bool boardPinnedToWorld = false;
            bool labelHidden = false;
            bool canPinToWorld = false;
        };

        using WorkingTransformChangedHandler =
            std::function<void(const EditState&)>;

        explicit VRUIItemEditPanel(const std::string& name);

        void setTargetItem(const std::string& category, const std::string& itemName, const std::string& modelPath, uint32_t formID,
                           float rotX, float rotY, float rotZ, float posX, float posY, float posZ, float scale,
                           const std::string& sourcePanel = "InventoryPanel", const std::string& actionFunc = "");

        void refresh() override;
        void updatePreview();
        void setRmlPreviewMode(bool enabled);
        void setRmlPreviewLayout(RmlPreviewLayout layout);
        [[nodiscard]] std::shared_ptr<class VRUIButton> getPreviewWidget() const
        {
            return _previewWidget;
        }
        void setInventoryPreviewInteractionHandler(
            InventoryPreviewInteractionHandler handler);
        void setWorkingTransformChangedHandler(
            WorkingTransformChangedHandler handler)
        {
            _workingTransformChangedHandler = std::move(handler);
        }
        void setEditPage(int index);

        // Game-thread backend used by both the classic 3D editor and the
        // document-driven local RmlUi editor.
        [[nodiscard]] EditState getEditState() const;
        void setWorkingTransform(float posX, float posY, float posZ,
                                 float rotX, float rotY, float rotZ, float scale);
        void applyItemOffsets();
        void applyCategoryOffsets();
        void resetItemOffsets();
        bool pinToDashboard();
        bool pinToLeftHand();
        bool pinToRightHand();
        bool pinToWorld();
        bool togglePinnedLabel();

    private:
        void saveOffsets();
        [[nodiscard]] std::string allocatePinElementId() const;
        void syncWorkingTransformFromPreview();
        bool getPreviewVisualWorldTransform(
            RE::NiPoint3& position, RE::NiMatrix3& rotation, float& scale) const;
        bool getPreviewVisualTransformRelativeTo(
            RE::NiNode* parent,
            RE::NiPoint3& position, RE::NiMatrix3& rotation, float& scale) const;
        void syncRotationFromPreviewGrab(const RE::NiMatrix3& localRotation);
        void updateInventoryPreviewInteraction();

        std::string _targetCategory;
        std::string _targetItemName;
        std::string _targetModelPath;
        std::string _sourcePanel = "InventoryPanel"; // Panel to return to on Back/Reset
        std::string _targetActionFunc;
        int _targetFormID = 0;

        std::shared_ptr<class VRUIButton> _previewWidget;
        float _baseScaleMult = 4.0f;
        float _normalizedScale = 1.0f;
        bool _rmlPreviewMode = false;
        bool _previewRootTransformConfigured = false;
        RmlPreviewLayout _rmlPreviewLayout = RmlPreviewLayout::ItemEditor;
        InventoryPreviewInteractionHandler _inventoryPreviewInteractionHandler;
        WorkingTransformChangedHandler _workingTransformChangedHandler;

        int _activeTab = 0;
        std::vector<std::shared_ptr<VRUIContainer>> _editPages;

        // Active working data
        float _posX = 0.0f;
        float _posY = 0.0f;
        float _posZ = 0.0f;
        float _rotX = 0.0f;
        float _rotY = 0.0f;
        float _rotZ = 0.0f;
        float _scale = 1.0f;
    };
}
