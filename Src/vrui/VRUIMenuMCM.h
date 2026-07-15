#pragma once

#include "VRUIPanel.h"
#include "VRUIContainer.h"
#include "VRUIButton.h"
#include <vector>

namespace vrui
{
    /**
     * @brief A specialized panel for in-game configuration (Virtual MCM).
     * 
     * Allows adjusting VRUISettings values in real-time, performing saving, 
     * and immediate layout updates.
     */
    class VRUIMenuMCM : public VRUIPanel
    {
    public:
        explicit VRUIMenuMCM(const std::string& name = "VirtualMCM");
        
        /// Initialize the MCM layout and controls
        void initializeVisuals() override;

        /// Override show to recenter content (needs scene graph to be valid)
        void show() override;

        /// Override layout to always re-center content vertically after layout
        void recalculateLayout() override;

        void setOnBackHandler(std::function<void()> handler) { _onBackHandler = handler; }

    private:
        /// Helper: center the MCM container vertically
        void centerContainer();

        /// Helper to change the active category tab
        void setCategoryPage(int index);

        /// Helper to create a setting row inside a specific parent container
        void addSettingRow(std::shared_ptr<VRUIContainer> parent,
                         const std::string& label, 
                         const std::string& settingKey,
                         float step,
                         std::function<float()> getter,
                         std::function<void(float)> setter);

        void addToggleRow(std::shared_ptr<VRUIContainer> parent,
                         const std::string& label, 
                         const std::string& settingKey,
                         std::function<bool()> getter,
                         std::function<void(bool)> setter);

        std::shared_ptr<VRUIContainer> _container;     // Master vertical container
        std::shared_ptr<VRUIContainer> _contentArea;   // Holds the active page
        std::vector<std::shared_ptr<VRUIContainer>> _categoryPages;
        std::function<void()> _onBackHandler;
    };
}
