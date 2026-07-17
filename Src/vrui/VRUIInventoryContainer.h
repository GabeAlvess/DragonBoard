#pragma once

#include "VRUIDynamicContainer.h"
#include <RE/Skyrim.h>
#include <unordered_map>
#include <memory>
#include <vector>

namespace vrui
{
    class VRUIButton;

    enum class InventoryFilterMode {
        All,
        // Weapons
        WeaponsAll, Swords, Daggers, Maces, WarAxes,
        Greatswords, Battleaxes, Warhammers, Bows, Crossbows, Staves, Torches,
        // Armor
        ArmorAll, Helmets, Cuirasses, Gauntlets, Boots, Shields, Rings, Amulets,
        // Consumables
        ConsumablesAll, Potions, Poisons, Food, Ingredients,
        // Special
        SoulGems, Scrolls, Ammo, Keys, QuestItems,
        // Books and Misc (PlaceAtMe + HIGGS interaction)
        BooksAll, MiscAll,
        // Magic
        MagicAll,
    };

    /**
     * @brief A dynamic container that displays the player's inventory items.
     */
    class VRUIInventoryContainer : public VRUIDynamicContainer
    {
    public:
        struct RmlItemData
        {
            RE::FormID formID = 0;
            std::int32_t count = 0;
            std::string name;
            std::string category;
            std::string description;
            std::string equipmentMarker;
            std::string equipmentState;
            std::string editCategory;
            std::string modelPath;
            float attack = 0.0f;
            float defense = 0.0f;
            float weight = 0.0f;
            std::int32_t value = 0;
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float xOff = 0.0f;
            float yOff = 0.0f;
            float zOff = 0.0f;
            float scaleMult = 1.0f;
            bool hasAttack = false;
            bool hasDefense = false;
            bool equipped = false;
            bool equippedLeft = false;
            bool equippedRight = false;
            bool favorited = false;
            bool canEquip = false;
        };

        struct RmlInventorySnapshot
        {
            std::vector<RmlItemData> items;
            std::string playerName;
            std::uint16_t playerLevel = 1;
            std::int32_t gold = 0;
            float currentWeight = 0.0f;
            float carryWeight = 0.0f;
        };

        explicit VRUIInventoryContainer(const std::string& name, 
                                      ContainerLayout layout = ContainerLayout::Grid,
                                      float spacing = 3.6f, 
                                      float scale = 1.0f);

        void refresh() override;
        void update(float deltaTime) override;
        int getTotalPages() const override;
        void setPage(int page) override;
        void recalculateLayout() override;
        void updateEquippedStates() override;
        void invalidateRefreshCache() override;
        int _totalValidItems = 0;

        [[nodiscard]] RmlInventorySnapshot buildRmlInventorySnapshot() const;
        [[nodiscard]] std::uint64_t buildRmlInventorySignature() const;
        bool interactWithItem(RE::FormID formID, EquipHand hand);
        bool activateItem(RE::FormID formID, EquipHand hand);
        bool dropItem(RE::FormID formID);
        bool toggleFavorite(RE::FormID formID);

        std::string getModelPath(RE::TESBoundObject* item);

        static bool passesFilter(RE::TESBoundObject* item, InventoryFilterMode filter);

        // Maps item FormID → button (populated during refresh, updated in place)
        std::unordered_map<uint32_t, std::weak_ptr<VRUIButton>> _formToButton;

        // Sub-category filter
        InventoryFilterMode _filter = InventoryFilterMode::All;
        void setFilter(InventoryFilterMode mode) { _filter = mode; _currentPage = 0; _cachedPageItems.clear(); }
        [[nodiscard]] InventoryFilterMode getFilter() const { return _filter; }

    private:
        struct CachedPageItem
        {
            RE::FormID formID = 0;
            std::int32_t count = 0;
            bool favorited = false;

            bool operator==(const CachedPageItem&) const = default;
        };

        struct PendingInventoryItem
        {
            RE::FormID formID = 0;
            std::int32_t count = 0;
            std::string label;
            std::string modelPath;
            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float xOff = 0.0f;
            float yOff = 0.0f;
            float zOff = 0.0f;
            float scaleMult = 1.0f;
        };

        std::vector<CachedPageItem> _cachedPageItems;
        InventoryFilterMode _cachedFilter = InventoryFilterMode::All;
        int _cachedPage = -1;
        std::vector<PendingInventoryItem> _pendingBuildItems;
        std::size_t _pendingBuildIndex = 0;
        bool _pendingLayoutCommit = false;

        bool spawnItemInHand(RE::FormID formID, EquipHand hand);
        void appendInventoryButton(const PendingInventoryItem& pending);
    };
}
