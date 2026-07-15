#pragma once

#include "VRUIDynamicContainer.h"
#include <RE/Skyrim.h>
#include <unordered_map>
#include <memory>

namespace vrui
{
    class VRUIButton;

    enum class MagicFilterMode {
        All,
        Destruction,
        Conjuration,
        Restoration,
        Illusion,
        Alteration,
        Powers,
        Passive
    };

    /**
     * @brief A dynamic container that displays the player's learned spells.
     */
    class VRUIMagicContainer : public VRUIDynamicContainer
    {
    public:
        explicit VRUIMagicContainer(const std::string& name, 
                                  ContainerLayout layout = ContainerLayout::Grid,
                                  float spacing = 3.6f, 
                                  float scale = 1.0f);

        void refresh() override;
        void updateEquippedStates() override;

        // Maps spell FormID → button
        std::unordered_map<uint32_t, std::weak_ptr<VRUIButton>> _formToButton;
        
        MagicFilterMode _currentFilter = MagicFilterMode::All;
        void setFilter(MagicFilterMode mode) { _currentFilter = mode; _currentPage = 0; }
    };
}
