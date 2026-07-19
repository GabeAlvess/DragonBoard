#include "pch.h"

#include "ui/rml/RmlInventoryPresenter.h"

#include "ui/rml/DragonBoardRmlUi.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace dragonboard::ui::rml
{
    void RmlInventoryPresenter::ResetForOpen()
    {
        std::scoped_lock lock(_mutex);
        _searchQuery.clear();
        _visibleIndices.clear();
    }

    std::size_t RmlInventoryPresenter::ItemCount() const
    {
        std::scoped_lock lock(_mutex);
        return _items.size();
    }

    std::string RmlInventoryPresenter::SearchQuery() const
    {
        std::scoped_lock lock(_mutex);
        return _searchQuery;
    }

    RmlInventoryPresenter::Selection RmlInventoryPresenter::GetSelection(
        bool preserveSelection) const
    {
        std::scoped_lock lock(_mutex);
        return { _selectedIndex, preserveSelection ? _selectedFormID : 0 };
    }

    std::uint64_t RmlInventoryPresenter::StateSignature() const
    {
        std::scoped_lock lock(_mutex);
        return _stateSignature;
    }

    bool RmlInventoryPresenter::CanReuseSnapshot(
        std::uint64_t stateSignature,
        std::string_view activeFilter) const
    {
        std::scoped_lock lock(_mutex);
        return _snapshotValid &&
            _stateSignature == stateSignature &&
            _activeFilter == activeFilter;
    }

    void RmlInventoryPresenter::ResetSelectionForUnpreservedSnapshot()
    {
        std::scoped_lock lock(_mutex);
        _selectedIndex = 0;
        _selectedFormID = _items.empty() ? 0 : _items.front().formID;
        ReconcileSelectionForSearchLocked();
    }

    void RmlInventoryPresenter::ReplaceSnapshot(
        std::vector<Entry> entries,
        std::size_t selectedIndex,
        std::string activeFilter,
        PlayerInfo player,
        std::uint64_t stateSignature)
    {
        std::scoped_lock lock(_mutex);
        _items = std::move(entries);
        _selectedIndex = selectedIndex;
        _selectedFormID = _items.empty() ? 0 : _items[_selectedIndex].formID;
        _visibleIndices.clear();
        _activeFilter = std::move(activeFilter);
        _player = std::move(player);
        _stateSignature = stateSignature;
        _snapshotValid = true;
        ReconcileSelectionForSearchLocked();
    }

    void RmlInventoryPresenter::SetSearchQuery(std::string query)
    {
        std::scoped_lock lock(_mutex);
        _searchQuery = std::move(query);
        _visibleIndices.clear();
        ReconcileSelectionForSearchLocked();
    }

    bool RmlInventoryPresenter::TryMapVisibleIndex(
        std::size_t visibleIndex,
        std::size_t& inventoryIndex) const
    {
        std::scoped_lock lock(_mutex);
        if (visibleIndex >= _visibleIndices.size()) return false;
        inventoryIndex = _visibleIndices[visibleIndex];
        return inventoryIndex < _items.size();
    }

    std::optional<RmlInventoryPresenter::Entry>
    RmlInventoryPresenter::SelectedEntry() const
    {
        std::scoped_lock lock(_mutex);
        if (_selectedIndex >= _items.size()) return std::nullopt;
        return _items[_selectedIndex];
    }

    std::optional<RmlInventoryPresenter::Entry> RmlInventoryPresenter::Select(
        std::size_t inventoryIndex)
    {
        std::scoped_lock lock(_mutex);
        if (inventoryIndex < _items.size()) {
            _selectedIndex = inventoryIndex;
            _selectedFormID = _items[inventoryIndex].formID;
        }
        if (_selectedIndex >= _items.size()) return std::nullopt;
        return _items[_selectedIndex];
    }

    std::uint32_t RmlInventoryPresenter::FormIDAt(
        std::size_t inventoryIndex) const
    {
        std::scoped_lock lock(_mutex);
        return inventoryIndex < _items.size() ? _items[inventoryIndex].formID : 0;
    }

    void RmlInventoryPresenter::Sync(DragonBoardRmlUi& rmlUi)
    {
        DragonBoardRmlUi::InventoryInfo info;
        {
            std::scoped_lock lock(_mutex);
            info.playerName = _player.name;
            info.playerLevel = _player.level;
            info.gold = _player.gold;
            info.currentWeight = _player.currentWeight;
            info.carryWeight = _player.carryWeight;
            info.activeFilter = _activeFilter;
            info.searchQuery = _searchQuery;
            _visibleIndices.clear();
            info.items.reserve(_items.size());
            bool selectedVisible = false;
            for (std::size_t inventoryIndex = 0;
                 inventoryIndex < _items.size();
                 ++inventoryIndex) {
                const auto& entry = _items[inventoryIndex];
                if (!EntryMatchesSearch(entry, _searchQuery)) continue;
                const auto visibleIndex = info.items.size();
                _visibleIndices.push_back(inventoryIndex);
                if (inventoryIndex == _selectedIndex) {
                    info.selectedIndex = visibleIndex;
                    selectedVisible = true;
                }
                DragonBoardRmlUi::InventoryItemInfo item;
                item.name = entry.name;
                item.category = entry.category;
                item.description = entry.description;
                item.equipmentMarker = entry.equipmentMarker;
                item.equipmentState = entry.equipmentState;
                item.formID = entry.formID;
                item.count = entry.count;
                item.attack = entry.attack;
                item.defense = entry.defense;
                item.weight = entry.weight;
                item.value = entry.value;
                item.hasAttack = entry.hasAttack;
                item.hasDefense = entry.hasDefense;
                item.equipped = entry.equipped;
                item.equippedLeft = entry.equippedLeft;
                item.equippedRight = entry.equippedRight;
                item.favorited = entry.favorited;
                item.canEquip = entry.canEquip;
                info.items.push_back(std::move(item));
            }
            if (!selectedVisible) info.selectedIndex = 0;
        }
        rmlUi.SetInventory(std::move(info));
    }

    bool RmlInventoryPresenter::EntryMatchesSearch(
        const Entry& entry,
        std::string_view query)
    {
        if (query.empty()) return true;
        const auto equalIgnoreCase = [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                std::tolower(static_cast<unsigned char>(right));
        };
        return std::search(
            entry.name.begin(), entry.name.end(),
            query.begin(), query.end(),
            equalIgnoreCase) != entry.name.end();
    }

    bool RmlInventoryPresenter::ReconcileSelectionForSearchLocked()
    {
        const auto previousFormID = _selectedFormID;
        if (_selectedIndex < _items.size() &&
            EntryMatchesSearch(_items[_selectedIndex], _searchQuery)) {
            _selectedFormID = _items[_selectedIndex].formID;
            return _selectedFormID != previousFormID;
        }

        const auto firstMatch = std::find_if(
            _items.begin(), _items.end(),
            [this](const Entry& entry) {
                return EntryMatchesSearch(entry, _searchQuery);
            });
        if (firstMatch == _items.end()) {
            _selectedIndex = 0;
            _selectedFormID = 0;
        } else {
            _selectedIndex = static_cast<std::size_t>(
                std::distance(_items.begin(), firstMatch));
            _selectedFormID = firstMatch->formID;
        }
        return _selectedFormID != previousFormID;
    }
}
