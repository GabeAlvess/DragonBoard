#pragma once

#include "VRUIDynamicContainer.h"
#include <RE/Skyrim.h>
#include <unordered_map>
#include <memory>
#include "VRUIInventoryContainer.h"

namespace vrui
{
    class VRUIButton;

    /**
     * @brief A dynamic container that displays the player's favorite items and spells.
     */
    class VRUIFavoritesContainer : public VRUIDynamicContainer
    {
    public:
        explicit VRUIFavoritesContainer(const std::string& name, 
                                      ContainerLayout layout = ContainerLayout::Grid,
                                      float spacing = 3.6f, 
                                      float scale = 1.0f);

        void refresh() override;
        void updateEquippedStates() override;

        // Maps item/spell FormID → button for per-button equipped refresh
        std::unordered_map<uint32_t, std::weak_ptr<VRUIButton>> _formToButton;
        
        InventoryFilterMode _currentFilter = InventoryFilterMode::All;
        void setFilter(InventoryFilterMode mode) { _currentFilter = mode; _currentPage = 0; }
    };
}
