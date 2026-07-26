#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dragonboard::ui::rml
{
    class DragonBoardRmlUi;

    class RmlInventoryPresenter
    {
    public:
        struct Entry
        {
            std::string name;
            std::string category;
            std::string description;
            std::string equipmentMarker;
            std::string equipmentState;
            std::string editCategory;
            std::string modelPath;
            std::uint32_t formID = 0;
            std::int32_t count = 0;
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

        struct PlayerInfo
        {
            std::string name;
            std::uint16_t level = 1;
            std::int32_t gold = 0;
            float currentWeight = 0.0f;
            float carryWeight = 0.0f;
            float currentHealth = 0.0f;
            float maximumHealth = 0.0f;
            float currentStamina = 0.0f;
            float maximumStamina = 0.0f;
            float currentMagicka = 0.0f;
            float maximumMagicka = 0.0f;
        };

        struct Selection
        {
            std::size_t index = 0;
            std::uint32_t formID = 0;
        };

        void ResetForOpen();
        [[nodiscard]] std::size_t ItemCount() const;
        [[nodiscard]] std::string SearchQuery() const;
        [[nodiscard]] Selection GetSelection(bool preserveSelection) const;
        [[nodiscard]] std::uint64_t StateSignature() const;
        [[nodiscard]] bool CanReuseSnapshot(
            std::uint64_t stateSignature,
            std::string_view activeFilter) const;
        void ResetSelectionForUnpreservedSnapshot();
        void ReplaceSnapshot(
            std::vector<Entry> entries,
            std::size_t selectedIndex,
            std::string activeFilter,
            PlayerInfo player,
            std::uint64_t stateSignature);
        void SetSearchQuery(std::string query);
        bool UpdateVitals(
            float currentHealth,
            float maximumHealth,
            float currentStamina,
            float maximumStamina,
            float currentMagicka,
            float maximumMagicka);
        [[nodiscard]] bool TryMapVisibleIndex(
            std::size_t visibleIndex,
            std::size_t& inventoryIndex) const;
        [[nodiscard]] std::optional<Entry> SelectedEntry() const;
        [[nodiscard]] std::optional<Entry> Select(std::size_t inventoryIndex);
        [[nodiscard]] std::uint32_t FormIDAt(std::size_t inventoryIndex) const;
        void Sync(DragonBoardRmlUi& rmlUi);

    private:
        [[nodiscard]] static bool EntryMatchesSearch(
            const Entry& entry,
            std::string_view query);
        bool ReconcileSelectionForSearchLocked();

        mutable std::mutex _mutex;
        std::vector<Entry> _items;
        std::vector<std::size_t> _visibleIndices;
        std::size_t _selectedIndex = 0;
        std::uint32_t _selectedFormID = 0;
        std::string _activeFilter;
        std::string _searchQuery;
        PlayerInfo _player;
        std::uint64_t _stateSignature = 0;
        bool _snapshotValid = false;
    };
}
