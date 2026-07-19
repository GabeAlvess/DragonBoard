#include "pch.h"

#include "ui/rml/RmlMagicPresenter.h"

#include "ui/rml/DragonBoardRmlUi.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace dragonboard::ui::rml
{
    void RmlMagicPresenter::ResetForOpen()
    {
        std::scoped_lock lock(_mutex);
        _searchQuery.clear();
        _visibleIndices.clear();
    }

    std::size_t RmlMagicPresenter::ItemCount() const
    {
        std::scoped_lock lock(_mutex);
        return _items.size();
    }

    std::string RmlMagicPresenter::SearchQuery() const
    {
        std::scoped_lock lock(_mutex);
        return _searchQuery;
    }

    RmlMagicPresenter::Selection RmlMagicPresenter::GetSelection(
        bool preserveSelection) const
    {
        std::scoped_lock lock(_mutex);
        return { _selectedIndex, preserveSelection ? _selectedFormID : 0 };
    }

    std::uint64_t RmlMagicPresenter::StateSignature() const
    {
        std::scoped_lock lock(_mutex);
        return _stateSignature;
    }

    bool RmlMagicPresenter::CanReuseSnapshot(
        std::uint64_t stateSignature,
        std::string_view activeFilter) const
    {
        std::scoped_lock lock(_mutex);
        return _snapshotValid &&
            _stateSignature == stateSignature &&
            _activeFilter == activeFilter;
    }

    void RmlMagicPresenter::ResetSelectionForUnpreservedSnapshot()
    {
        std::scoped_lock lock(_mutex);
        _selectedIndex = 0;
        _selectedFormID = _items.empty() ? 0 : _items.front().formID;
        ReconcileSelectionForSearchLocked();
    }

    void RmlMagicPresenter::ReplaceSnapshot(
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

    void RmlMagicPresenter::SetSearchQuery(std::string query)
    {
        std::scoped_lock lock(_mutex);
        _searchQuery = std::move(query);
        _visibleIndices.clear();
        ReconcileSelectionForSearchLocked();
    }

    bool RmlMagicPresenter::TryMapVisibleIndex(
        std::size_t visibleIndex,
        std::size_t& magicIndex) const
    {
        std::scoped_lock lock(_mutex);
        if (visibleIndex >= _visibleIndices.size()) return false;
        magicIndex = _visibleIndices[visibleIndex];
        return magicIndex < _items.size();
    }

    std::optional<RmlMagicPresenter::Entry> RmlMagicPresenter::SelectedEntry() const
    {
        std::scoped_lock lock(_mutex);
        if (_selectedIndex >= _items.size()) return std::nullopt;
        return _items[_selectedIndex];
    }

    std::optional<RmlMagicPresenter::Entry> RmlMagicPresenter::Select(
        std::size_t magicIndex)
    {
        std::scoped_lock lock(_mutex);
        if (magicIndex < _items.size()) {
            _selectedIndex = magicIndex;
            _selectedFormID = _items[magicIndex].formID;
        }
        if (_selectedIndex >= _items.size()) return std::nullopt;
        return _items[_selectedIndex];
    }

    std::uint32_t RmlMagicPresenter::FormIDAt(std::size_t magicIndex) const
    {
        std::scoped_lock lock(_mutex);
        return magicIndex < _items.size() ? _items[magicIndex].formID : 0;
    }

    void RmlMagicPresenter::Sync(
        DragonBoardRmlUi& rmlUi,
        bool editModeEnabled)
    {
        DragonBoardRmlUi::MagicInfo info;
        {
            std::scoped_lock lock(_mutex);
            info.playerName = _player.name;
            info.playerLevel = _player.level;
            info.currentMagicka = _player.currentMagicka;
            info.maximumMagicka = _player.maximumMagicka;
            info.activeFilter = _activeFilter;
            info.searchQuery = _searchQuery;
            info.editModeEnabled = editModeEnabled;
            _visibleIndices.clear();
            info.items.reserve(_items.size());
            bool selectedVisible = false;
            for (std::size_t magicIndex = 0; magicIndex < _items.size(); ++magicIndex) {
                const auto& entry = _items[magicIndex];
                if (!EntryMatchesSearch(entry, _searchQuery)) continue;
                const auto visibleIndex = info.items.size();
                _visibleIndices.push_back(magicIndex);
                if (magicIndex == _selectedIndex) {
                    info.selectedIndex = visibleIndex;
                    selectedVisible = true;
                }
                DragonBoardRmlUi::MagicItemInfo item;
                item.name = entry.name;
                item.category = entry.category;
                item.description = entry.description;
                item.iconPath = entry.iconPath;
                item.castingType = entry.castingType;
                item.delivery = entry.delivery;
                item.skillLevel = entry.skillLevel;
                item.duration = entry.duration;
                item.range = entry.range;
                item.formID = entry.formID;
                item.magickaCost = entry.magickaCost;
                item.equipped = entry.equipped;
                item.equippedLeft = entry.equippedLeft;
                item.equippedRight = entry.equippedRight;
                item.favorited = entry.favorited;
                item.canEquip = entry.canEquip;
                item.hasModelPreview = !entry.modelPath.empty();
                info.items.push_back(std::move(item));
            }
            if (!selectedVisible) info.selectedIndex = 0;
        }
        rmlUi.SetMagic(std::move(info));
    }

    bool RmlMagicPresenter::EntryMatchesSearch(
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

    bool RmlMagicPresenter::ReconcileSelectionForSearchLocked()
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
