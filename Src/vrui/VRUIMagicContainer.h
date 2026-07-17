#pragma once

#include "VRUIDynamicContainer.h"
#include <RE/Skyrim.h>
#include <memory>
#include <unordered_map>
#include <vector>

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
        struct RmlMagicItemData
        {
            RE::FormID formID = 0;
            std::string name;
            std::string category;
            std::string description;
            std::string modelPath;
            std::string iconPath;
            std::string castingType;
            std::string delivery;
            std::string skillLevel;
            std::string duration;
            std::string range;
            float magickaCost = 0.0f;
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float xOff = 0.0f;
            float yOff = 0.0f;
            float zOff = 0.0f;
            float scaleMult = 1.0f;
            bool equipped = false;
            bool equippedLeft = false;
            bool equippedRight = false;
            bool favorited = false;
            bool canEquip = false;
            bool isPower = false;
            bool isPassive = false;
        };

        struct RmlMagicSnapshot
        {
            std::vector<RmlMagicItemData> items;
            std::string playerName;
            std::uint16_t playerLevel = 1;
            float currentMagicka = 0.0f;
            float maximumMagicka = 0.0f;
        };

        explicit VRUIMagicContainer(const std::string& name, 
                                  ContainerLayout layout = ContainerLayout::Grid,
                                  float spacing = 3.6f, 
                                  float scale = 1.0f);

        void refresh() override;
        void updateEquippedStates() override;

        [[nodiscard]] RmlMagicSnapshot buildRmlMagicSnapshot() const;
        [[nodiscard]] std::uint64_t buildRmlMagicSignature() const;
        bool activateSpell(RE::FormID formID, EquipHand hand);
        bool toggleFavorite(RE::FormID formID);

        // Maps spell FormID → button
        std::unordered_map<uint32_t, std::weak_ptr<VRUIButton>> _formToButton;
        
        MagicFilterMode _currentFilter = MagicFilterMode::All;
        void setFilter(MagicFilterMode mode) { _currentFilter = mode; _currentPage = 0; }
        [[nodiscard]] MagicFilterMode getFilter() const { return _currentFilter; }
    };
}
