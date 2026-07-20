#include "VRUIInventoryContainer.h"
#include "ui/equipment/EquipInteractionController.h"
#include "runtime/vr/ReferencePlacement.h"
#include "VRUIItemUtils.h"
#include "VRUIItemEditPanel.h"
#include "VRUIButton.h"
#include "VRUILayoutManager.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESDescription.h>
#include <RE/T/TESObjectBOOK.h>
#include <RE/B/BSString.h>
#include <RE/E/EffectSetting.h>
#include <RE/M/MagicFavorites.h>
#include <RE/M/MagicItem.h>
#include <RE/I/InventoryChanges.h>
#include <RE/I/InventoryEntryData.h>
#include <RE/E/ExtraDataList.h>
#include <RE/E/ExtraDataTypes.h>
#include <RE/M/MemoryManager.h>
#include <RE/B/BSAudioManager.h>
#include "VRMenuManager.h"
#include "higgsinterface001.h"
#include <SKSE/API.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

extern HiggsPluginAPI::IHiggsInterface001* g_higgsInterface;

namespace vrui
{
    namespace
    {
        constexpr std::size_t kInitialInventoryBuildCount = 4;
        constexpr std::size_t kInventoryBuildsPerFrame = 3;
        constexpr RE::FormID kGoldFormID = 0x0000000F;

        bool isSupportedInventoryItem(const RE::TESBoundObject& item)
        {
            switch (item.GetFormType()) {
            case RE::FormType::Weapon:
            case RE::FormType::Armor:
            case RE::FormType::Ammo:
            case RE::FormType::AlchemyItem:
            case RE::FormType::Ingredient:
            case RE::FormType::SoulGem:
            case RE::FormType::Scroll:
            case RE::FormType::Light:
            case RE::FormType::KeyMaster:
            case RE::FormType::Book:
            case RE::FormType::Misc:
                return true;
            default:
                return false;
            }
        }

        std::string resolveInventoryCategory(RE::TESBoundObject* item)
        {
            if (!item) return "Item";
            if (item->Is(RE::FormType::Weapon)) return "Weapon";
            if (item->Is(RE::FormType::Armor)) return "Armor";
            if (item->Is(RE::FormType::Ammo)) return "Ammunition";
            if (item->Is(RE::FormType::AlchemyItem)) {
                if (auto* alchemy = item->As<RE::AlchemyItem>()) {
                    if (alchemy->IsPoison()) return "Poison";
                    if (alchemy->IsFood()) return "Food";
                }
                return "Potion";
            }
            if (item->Is(RE::FormType::Ingredient)) return "Ingredient";
            if (item->Is(RE::FormType::SoulGem)) return "Soul Gem";
            if (item->Is(RE::FormType::Scroll)) return "Scroll";
            if (item->Is(RE::FormType::Light)) return "Light";
            if (item->Is(RE::FormType::KeyMaster)) return "Key";
            if (item->Is(RE::FormType::Book)) return "Book";
            return "Miscellaneous";
        }

        std::string cleanInventoryDescription(std::string_view raw)
        {
            std::string result;
            result.reserve(std::min<std::size_t>(raw.size(), 280));
            bool insideTag = false;
            bool pendingSpace = false;

            for (const unsigned char character : raw) {
                if (character == '<') {
                    insideTag = true;
                    pendingSpace = !result.empty();
                    continue;
                }
                if (character == '>') {
                    insideTag = false;
                    continue;
                }
                if (insideTag) continue;
                if (std::isspace(character)) {
                    pendingSpace = !result.empty();
                    continue;
                }
                if (pendingSpace) {
                    result.push_back(' ');
                    pendingSpace = false;
                }
                result.push_back(static_cast<char>(character));
                if (result.size() >= 280) break;
            }
            return result;
        }

        std::string formatEffectMagnitude(float value)
        {
            if (!std::isfinite(value)) return "0";
            const auto rounded = std::llround(value);
            if (std::abs(value - static_cast<float>(rounded)) < 0.05f) {
                return std::to_string(rounded);
            }

            std::ostringstream stream;
            stream << std::fixed << std::setprecision(1) << value;
            return stream.str();
        }

        void replaceEffectToken(
            std::string& text,
            std::string_view token,
            std::string_view replacement)
        {
            std::string lowerToken(token);
            std::transform(
                lowerToken.begin(), lowerToken.end(), lowerToken.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });

            std::size_t searchFrom = 0;
            while (searchFrom < text.size()) {
                std::string lowerText(text);
                std::transform(
                    lowerText.begin(), lowerText.end(), lowerText.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
                const auto position = lowerText.find(lowerToken, searchFrom);
                if (position == std::string::npos) break;
                text.replace(position, token.size(), replacement);
                searchFrom = position + replacement.size();
            }
        }

        std::string resolveMagicItemEffectDescription(RE::MagicItem* magicItem)
        {
            if (!magicItem) return {};

            std::string description;
            for (auto* effect : magicItem->effects) {
                if (!effect || !effect->baseEffect) continue;
                const auto* baseEffect = effect->baseEffect;
                if (baseEffect->data.flags.all(
                        RE::EffectSetting::EffectSettingData::Flag::kHideInUI)) {
                    continue;
                }

                const char* rawTemplate = baseEffect->magicItemDescription.c_str();
                if (!rawTemplate || !*rawTemplate) continue;

                std::string resolved(rawTemplate);
                replaceEffectToken(
                    resolved, "<mag>", formatEffectMagnitude(effect->effectItem.magnitude));
                replaceEffectToken(
                    resolved, "<dur>", std::to_string(effect->effectItem.duration));
                replaceEffectToken(
                    resolved, "<area>", std::to_string(effect->effectItem.area));

                auto cleaned = cleanInventoryDescription(resolved);
                if (cleaned.empty()) continue;
                if (!description.empty()) description.push_back(' ');
                description += cleaned;
                if (description.size() >= 280) {
                    description.resize(280);
                    break;
                }
            }
            return description;
        }

        std::string fallbackInventoryDescription(RE::TESBoundObject* item)
        {
            if (!item) return "No description available.";
            if (item->Is(RE::FormType::Weapon)) return "A weapon carried in your inventory.";
            if (item->Is(RE::FormType::Armor)) return "Protective equipment carried in your inventory.";
            if (item->Is(RE::FormType::Ammo)) return "Ammunition used by ranged weapons.";
            if (auto* alchemy = item->As<RE::AlchemyItem>()) {
                if (alchemy->IsPoison()) return "A poison that can be applied to a weapon.";
                if (alchemy->IsFood()) return "Food that can be consumed.";
                return "A potion that can be consumed.";
            }
            if (item->Is(RE::FormType::Ingredient)) return "An ingredient used in alchemy.";
            if (item->Is(RE::FormType::SoulGem)) return "A soul gem used for enchanting.";
            if (item->Is(RE::FormType::Scroll)) return "A scroll that casts a spell when used.";
            if (item->Is(RE::FormType::Light)) return "A portable light source.";
            if (item->Is(RE::FormType::KeyMaster)) return "A key carried in your inventory.";
            if (item->Is(RE::FormType::Book)) return "A book or note.";
            return "A miscellaneous item.";
        }

        std::string resolveInventoryDescription(RE::TESBoundObject* item)
        {
            if (!item) return fallbackInventoryDescription(item);

            if (item->Is(RE::FormType::AlchemyItem) ||
                item->Is(RE::FormType::Scroll)) {
                if (auto* magicItem = item->As<RE::MagicItem>()) {
                    auto effectDescription =
                        resolveMagicItemEffectDescription(magicItem);
                    if (!effectDescription.empty()) return effectDescription;
                }
            }

            RE::BSString rawDescription;
            if (auto* weapon = item->As<RE::TESObjectWEAP>()) {
                static_cast<RE::TESDescription*>(weapon)->GetDescription(rawDescription, weapon);
            } else if (auto* armor = item->As<RE::TESObjectARMO>()) {
                static_cast<RE::TESDescription*>(armor)->GetDescription(rawDescription, armor);
            } else if (auto* ammo = item->As<RE::TESAmmo>()) {
                static_cast<RE::TESDescription*>(ammo)->GetDescription(rawDescription, ammo);
            } else if (auto* book = item->As<RE::TESObjectBOOK>()) {
                book->itemCardDescription.GetDescription(rawDescription, book, 'MANC');
            }

            const char* rawText = rawDescription.c_str();
            if (rawText && *rawText) {
                auto cleaned = cleanInventoryDescription(rawText);
                if (!cleaned.empty()) return cleaned;
            }
            return fallbackInventoryDescription(item);
        }

        std::string resolveItemEditCategory(RE::TESBoundObject* item)
        {
            if (!item) return "Misc";
            if (item->Is(RE::FormType::Weapon) || item->Is(RE::FormType::Ammo) ||
                item->Is(RE::FormType::Light)) {
                return "Weapons";
            }
            if (item->Is(RE::FormType::Armor)) return "Armor";
            if (item->Is(RE::FormType::Ingredient)) return "Food";
            if (auto* alchemy = item->As<RE::AlchemyItem>()) {
                return alchemy->IsFood() ? "Food" : "Potions";
            }
            return "Misc";
        }

        struct InventoryEquipmentInfo
        {
            bool equipped = false;
            bool equippedLeft = false;
            bool equippedRight = false;
            std::string marker;
            std::string state = "NOT EQUIPPED";
        };

        InventoryEquipmentInfo resolveInventoryEquipment(
            RE::PlayerCharacter* player,
            RE::TESBoundObject* item,
            RE::InventoryEntryData* inventoryEntry)
        {
            InventoryEquipmentInfo result;
            if (!player || !item) return result;

            if (item->Is(RE::FormType::Armor)) {
                bool worn = inventoryEntry && inventoryEntry->IsWorn();
                if (!worn) {
                    auto* armor = item->As<RE::TESObjectARMO>();
                    auto* equippedArmor =
                        armor ? player->GetWornArmor(armor->GetSlotMask()) : nullptr;
                    worn = equippedArmor && equippedArmor->formID == item->formID;
                }
                if (worn) {
                    result.equipped = true;
                    result.marker = "[W]";
                    result.state = "EQUIPPED - WORN";
                }
                return result;
            }

            const auto* left = player->GetEquippedObject(true);
            const auto* right = player->GetEquippedObject(false);
            const bool leftEquipped = left && left->formID == item->formID;
            const bool rightEquipped = right && right->formID == item->formID;

            result.equipped = leftEquipped || rightEquipped;
            result.equippedLeft = leftEquipped;
            result.equippedRight = rightEquipped;
            if (leftEquipped && rightEquipped) {
                result.marker = "[L/R]";
                result.state = "EQUIPPED - BOTH HANDS";
            } else if (leftEquipped) {
                result.marker = "[L]";
                result.state = "EQUIPPED - LEFT HAND";
            } else if (rightEquipped) {
                result.marker = "[R]";
                result.state = "EQUIPPED - RIGHT HAND";
            }
            return result;
        }
    }

    // Helper to safely allocate ExtraDataList without calling the unresolved external constructor
    static RE::ExtraDataList* createExtraDataList() {
        auto* mm = RE::MemoryManager::GetSingleton();
        if (!mm) {
            logger::error("DragonBoardVR: MemoryManager unavailable while creating ExtraDataList.");
            return nullptr;
        }

        void* mem = mm->Allocate(sizeof(RE::ExtraDataList), 0, false);
        if (!mem) {
            logger::error("DragonBoardVR: Failed to allocate memory for ExtraDataList.");
            return nullptr;
        }

        std::memset(mem, 0, sizeof(RE::ExtraDataList));
        auto* list = reinterpret_cast<RE::ExtraDataList*>(mem);
        
        // Restore the vtable by copying it from the player's existing ExtraDataList
        auto* p = RE::PlayerCharacter::GetSingleton();
        if (!p) {
            logger::error("DragonBoardVR: Player unavailable while creating ExtraDataList.");
            mm->Deallocate(mem, false);
            return nullptr;
        }

        *reinterpret_cast<void**>(list) = *reinterpret_cast<void**>(&p->extraList);
        return list;
    }
    
    VRUIInventoryContainer::VRUIInventoryContainer(const std::string& name, 
                                                 ContainerLayout layout,
                                                 float spacing, 
                                                 float scale)
        : VRUIDynamicContainer(name, layout, spacing, scale)
    {
    }

    // Returns true if the item type can be equipped or used by the player
    static bool isEquippableItem(RE::TESBoundObject* item)
    {
        if (!item) return false;
        auto type = item->GetFormType();
        return type == RE::FormType::Weapon       // Weapons (swords, bows, etc.)
            || type == RE::FormType::Armor        // Armor, clothing, shields
            || type == RE::FormType::Ammo         // Arrows, bolts
            || type == RE::FormType::AlchemyItem  // Potions, poisons
            || type == RE::FormType::Ingredient   // Alchemy ingredients (usable)
            || type == RE::FormType::SoulGem      // Soul gems (usable)
            || type == RE::FormType::Scroll       // Scrolls (usable / equippable)
            || type == RE::FormType::Light;       // Torches and portable lights
    }


    // Checks if an item passes the current sub-category filter
    bool VRUIInventoryContainer::passesFilter(RE::TESBoundObject* item, InventoryFilterMode filter)
    {
        using FM = InventoryFilterMode;
        if (!item) return false;
        if (filter == FM::All)       return true;
        if (filter == FM::QuestItems) return (item->formFlags & 0x400u) != 0;
        if (filter == FM::Ammo)      return item->Is(RE::FormType::Ammo);
        if (filter == FM::Keys)      return item->Is(RE::FormType::KeyMaster);
        if (filter == FM::SoulGems)  return item->Is(RE::FormType::SoulGem);
        if (filter == FM::Scrolls)   return item->Is(RE::FormType::Scroll);
        if (filter == FM::Torches)   return item->Is(RE::FormType::Light);

        // Weapons
        if (filter == FM::WeaponsAll) return item->Is(RE::FormType::Weapon);
        if (item->Is(RE::FormType::Weapon)) {
            auto* weap = item->As<RE::TESObjectWEAP>();
            if (!weap) return false;
            uint8_t wt = static_cast<uint8_t>(weap->weaponData.animationType.get());
            static RE::BGSKeyword* s_kwWarhammer = nullptr;
            if (!s_kwWarhammer)
                s_kwWarhammer = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("WeapTypeWarhammer");
            switch (filter) {
            case FM::Swords:      return wt == 1;
            case FM::Daggers:     return wt == 2;
            case FM::WarAxes:     return wt == 3;
            case FM::Maces:       return wt == 4;
            case FM::Greatswords: return wt == 5;
            case FM::Battleaxes:  return wt == 6 && !(s_kwWarhammer && weap->HasKeyword(s_kwWarhammer));
            case FM::Warhammers:  return wt == 6 &&   s_kwWarhammer && weap->HasKeyword(s_kwWarhammer);
            case FM::Bows:        return wt == 7;
            case FM::Staves:      return wt == 8;
            case FM::Crossbows:   return wt == 9;
            default: break;
            }
            return false;
        }

        // Armor
        if (filter == FM::ArmorAll) return item->Is(RE::FormType::Armor);
        if (item->Is(RE::FormType::Armor)) {
            auto* armo = item->As<RE::TESObjectARMO>();
            if (!armo) return false;
            uint32_t mask = static_cast<uint32_t>(armo->GetSlotMask());
            switch (filter) {
            case FM::Helmets:   return (mask & 0x001u) != 0; // slot 30
            case FM::Cuirasses: return (mask & 0x004u) != 0; // slot 32
            case FM::Gauntlets: return (mask & 0x008u) != 0; // slot 33
            case FM::Boots:     return (mask & 0x080u) != 0; // slot 37
            case FM::Shields:   return (mask & 0x200u) != 0; // slot 39
            case FM::Amulets:   return (mask & 0x020u) != 0; // slot 35
            case FM::Rings:     return (mask & 0x040u) != 0; // slot 36
            default: break;
            }
            return false;
        }

        // Consumables
        if (filter == FM::ConsumablesAll)
            return item->Is(RE::FormType::AlchemyItem) || item->Is(RE::FormType::Ingredient);
        if (item->Is(RE::FormType::AlchemyItem)) {
            auto* alch = item->As<RE::AlchemyItem>();
            if (!alch) return false;
            switch (filter) {
            case FM::Potions: return !alch->IsPoison() && !alch->IsFood();
            case FM::Poisons: return  alch->IsPoison();
            case FM::Food:    return !alch->IsPoison() &&  alch->IsFood();
            default: break;
            }
            return false;
        }
        if (filter == FM::Ingredients) return item->Is(RE::FormType::Ingredient);

        if (filter == FM::BooksAll) return item->Is(RE::FormType::Book);
        if (filter == FM::MiscAll)
            return item->Is(RE::FormType::Misc) || item->Is(RE::FormType::Light);

        return false;
    }

    void VRUIInventoryContainer::refresh()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto inventory = player->GetInventory([this](RE::TESBoundObject& a_obj) {
            if (_filter == InventoryFilterMode::QuestItems) return true;
            return a_obj.Is(RE::FormType::Weapon)
                || a_obj.Is(RE::FormType::Armor)
                || a_obj.Is(RE::FormType::Ammo)
                || a_obj.Is(RE::FormType::AlchemyItem)
                || a_obj.Is(RE::FormType::Ingredient)
                || a_obj.Is(RE::FormType::SoulGem)
                || a_obj.Is(RE::FormType::Scroll)
                || a_obj.Is(RE::FormType::Light)
                || a_obj.Is(RE::FormType::KeyMaster)
                || a_obj.Is(RE::FormType::Book)
                || a_obj.Is(RE::FormType::Misc);
        });

        std::vector<std::pair<RE::TESBoundObject*, std::pair<int32_t, RE::InventoryEntryData*>>> validItems;
        for (auto& [item, data] : inventory) {
            if (!item || data.first <= 0) continue;
            if (!passesFilter(item, _filter)) continue;
            validItems.push_back({ item, {data.first, data.second.get()} });
        }

        _totalValidItems = static_cast<int>(validItems.size());

        int startIndex = 0;
        int endIndex = _totalValidItems;
        if (_pageSize > 0) {
            startIndex = std::clamp(_currentPage * _pageSize, 0, std::max(0, _totalValidItems));
            endIndex = std::clamp(startIndex + _pageSize, 0, std::max(0, _totalValidItems));
        }

        std::vector<CachedPageItem> pageSnapshot;
        pageSnapshot.reserve(std::max(0, endIndex - startIndex));
        for (int i = startIndex; i < endIndex; ++i) {
            auto& req = validItems[i];
            auto* item = req.first;
            auto* entryData = req.second.second;
            if (!item) {
                continue;
            }

            pageSnapshot.push_back({
                item->formID,
                req.second.first,
                entryData ? entryData->IsFavorited() : false
            });
        }

        const bool pageUnchanged =
            _cachedPage == _currentPage &&
            _cachedFilter == _filter &&
            _cachedPageItems == pageSnapshot;

        if (pageUnchanged) {
            updateEquippedStates();
            logger::trace("DragonBoardVR: Inventory container '{}' reused cached page {} ({} items).",
                getName(), _currentPage, static_cast<int>(pageSnapshot.size()));
            return;
        }

        clearElements();
        _formToButton.clear();
        _pendingBuildItems.clear();
        _pendingBuildIndex = 0;
        _pendingLayoutCommit = false;

        for (int i = startIndex; i < endIndex; ++i) {
            auto req = validItems[i];
            auto item = req.first;
            auto count = req.second.first;
            auto* entryData = req.second.second;

            const char* name = item->GetName();
            std::string label = (name && name[0] != '\0') ? name
                : "Unknown " + std::string(item->As<RE::TESBoundObject>() ? "Item" : "Object");

            if (count > 1) {
                label += " (" + std::to_string(count) + ")";
            }

            std::string modelPath = ItemUtils::getModelPath(item);

            bool isFavorited = entryData ? entryData->IsFavorited() : false;
            if (isFavorited) {
                label = "* " + label; // Visual indicator
            }

            float rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult;
            ItemUtils::getItemOverrides(item, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);
            _pendingBuildItems.push_back({
                item->formID,
                count,
                std::move(label),
                std::move(modelPath),
                rotX, rotY, rotZ,
                xOff, yOff, zOff,
                scaleMult
            });
        }

        const std::size_t initialBuildCount = std::min(kInitialInventoryBuildCount, _pendingBuildItems.size());
        for (; _pendingBuildIndex < initialBuildCount; ++_pendingBuildIndex) {
            appendInventoryButton(_pendingBuildItems[_pendingBuildIndex]);
        }

        _pendingLayoutCommit = true;
        recalculateLayout();
        updateEquippedStates();

        logger::trace("DragonBoardVR: Inventory container '{}' created {} abstract buttons (Page {}).",
            getName(), static_cast<int>(initialBuildCount), _currentPage);

        _cachedPageItems = std::move(pageSnapshot);
        _cachedFilter = _filter;
        _cachedPage = _currentPage;

        if (_parent) {
            _parent->onChildLayoutChanged(this);
        }
    }

    void VRUIInventoryContainer::update(float deltaTime)
    {
        VRUIDynamicContainer::update(deltaTime);

        if (_pendingBuildIndex >= _pendingBuildItems.size()) {
            return;
        }

        const std::size_t endIndex = std::min(_pendingBuildIndex + kInventoryBuildsPerFrame, _pendingBuildItems.size());
        for (; _pendingBuildIndex < endIndex; ++_pendingBuildIndex) {
            appendInventoryButton(_pendingBuildItems[_pendingBuildIndex]);
        }

        recalculateLayout();
        updateEquippedStates();

        if (auto* node = getNode()) {
            RE::NiUpdateData updateData;
            updateData.flags = RE::NiUpdateData::Flag::kDirty;
            node->Update(updateData);
            node->UpdateWorldBound();
        }

        if (_pendingBuildIndex >= _pendingBuildItems.size()) {
            logger::trace("DragonBoardVR: Inventory container '{}' finished chunked build for page {} ({} items).",
                getName(), _currentPage, static_cast<int>(_pendingBuildItems.size()));
        }
    }

    void VRUIInventoryContainer::invalidateRefreshCache()
    {
        _cachedPage = -1;
        _cachedPageItems.clear();
    }

    VRUIInventoryContainer::RmlInventorySnapshot
    VRUIInventoryContainer::buildRmlInventorySnapshot() const
    {
        RmlInventorySnapshot snapshot;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return snapshot;

        const char* playerName = player->GetName();
        snapshot.playerName =
            playerName && *playerName ? playerName : "Dragonborn";
        snapshot.playerLevel = player->GetLevel();

        if (auto* changes = player->GetInventoryChanges()) {
            snapshot.currentWeight = changes->GetInventoryWeight();
        } else {
            snapshot.currentWeight = player->GetActorValue(RE::ActorValue::kInventoryWeight);
        }
        snapshot.carryWeight = player->GetActorValue(RE::ActorValue::kCarryWeight);

        auto inventory = player->GetInventory([this](RE::TESBoundObject& item) {
            return item.formID == kGoldFormID ||
                (_filter == InventoryFilterMode::QuestItems ? true : isSupportedInventoryItem(item));
        });
        snapshot.items.reserve(inventory.size());

        for (auto& [item, data] : inventory) {
            if (!item || data.first <= 0) continue;
            if (item->formID == kGoldFormID) {
                snapshot.gold = data.first;
                continue;
            }
            if (!passesFilter(item, _filter)) continue;

            RmlItemData entry;
            entry.formID = item->formID;
            entry.count = data.first;
            const char* rawName = item->GetName();
            entry.name = rawName && *rawName ? rawName : "Unknown item";
            entry.category = resolveInventoryCategory(item);
            entry.description = resolveInventoryDescription(item);
            entry.editCategory = resolveItemEditCategory(item);
            entry.modelPath = ItemUtils::getModelPath(item);
            entry.weight = item->GetWeight();
            entry.value = item->GetGoldValue();
            const auto equipment =
                resolveInventoryEquipment(player, item, data.second.get());
            entry.equipped = equipment.equipped;
            entry.equippedLeft = equipment.equippedLeft;
            entry.equippedRight = equipment.equippedRight;
            entry.equipmentMarker = equipment.marker;
            entry.equipmentState = equipment.state;
            entry.favorited = data.second && data.second->IsFavorited();
            entry.canEquip = isEquippableItem(item);

            if (auto* weapon = item->As<RE::TESObjectWEAP>()) {
                entry.attack = static_cast<float>(weapon->GetAttackDamage());
                entry.hasAttack = true;
            }
            if (auto* armor = item->As<RE::TESObjectARMO>()) {
                entry.defense = armor->GetArmorRating();
                entry.hasDefense = true;
            }

            ItemUtils::getItemOverrides(
                item,
                entry.rotX, entry.rotY, entry.rotZ,
                entry.xOff, entry.yOff, entry.zOff,
                entry.scaleMult);
            snapshot.items.push_back(std::move(entry));
        }

        std::sort(snapshot.items.begin(), snapshot.items.end(),
            [](const RmlItemData& lhs, const RmlItemData& rhs) {
                if (lhs.equipped != rhs.equipped) {
                    return lhs.equipped;
                }
                std::string left = lhs.name;
                std::string right = rhs.name;
                std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (left == right) return lhs.formID < rhs.formID;
                return left < right;
            });
        return snapshot;
    }

    std::uint64_t VRUIInventoryContainer::buildRmlInventorySignature() const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;

        auto inventory = player->GetInventory([](RE::TESBoundObject& item) {
            return item.formID == kGoldFormID ||
                isSupportedInventoryItem(item) ||
                (item.formFlags & 0x400u) != 0;
        });

        std::vector<std::uint64_t> rows;
        rows.reserve(inventory.size());
        for (const auto& [item, data] : inventory) {
            if (!item || data.first <= 0) continue;
            std::uint64_t row =
                (static_cast<std::uint64_t>(item->formID) << 32) |
                static_cast<std::uint32_t>(data.first);
            if (data.second && data.second->IsWorn()) {
                row ^= 0x8000000000000000ull;
            }
            if (data.second && data.second->IsFavorited()) {
                row ^= 0x4000000000000000ull;
            }
            rows.push_back(row);
        }
        std::sort(rows.begin(), rows.end());

        std::uint64_t signature = 1469598103934665603ull;
        const auto append = [&signature](std::uint64_t value) {
            for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
                signature ^= value & 0xFFu;
                signature *= 1099511628211ull;
                value >>= 8;
            }
        };
        for (const auto row : rows) append(row);

        const auto* left = player->GetEquippedObject(true);
        const auto* right = player->GetEquippedObject(false);
        append(0x4C00000000000000ull | (left ? left->formID : 0));
        append(0x5200000000000000ull | (right ? right->formID : 0));

        float currentWeight = player->GetActorValue(RE::ActorValue::kInventoryWeight);
        if (auto* changes = player->GetInventoryChanges()) {
            currentWeight = changes->GetInventoryWeight();
        }
        append(static_cast<std::uint64_t>(std::llround(currentWeight * 100.0f)));
        append(static_cast<std::uint64_t>(std::llround(
            player->GetActorValue(RE::ActorValue::kCarryWeight) * 100.0f)));
        append(player->GetLevel());
        if (const char* playerName = player->GetName()) {
            for (const unsigned char character : std::string_view(playerName)) {
                append(character);
            }
        }
        return signature;
    }

    bool VRUIInventoryContainer::interactWithItem(RE::FormID formID, EquipHand hand)
    {
        auto* form = RE::TESForm::LookupByID(formID);
        auto* item = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!item) return false;

        return isEquippableItem(item) ?
            activateItem(formID, hand) :
            spawnItemInHand(formID, hand);
    }

    bool VRUIInventoryContainer::toggleFavorite(RE::FormID formID)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* form = RE::TESForm::LookupByID(formID);
        auto* item = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!player || !item) return false;

        auto* changes = player->GetInventoryChanges();
        if (!changes || !changes->entryList) return false;

        RE::InventoryEntryData* entry = nullptr;
        for (auto* candidate : *changes->entryList) {
            if (candidate && candidate->object == item) {
                entry = candidate;
                break;
            }
        }
        if (!entry) return false;

        const bool wasFavorited = entry->IsFavorited();
        if (!wasFavorited) {
            RE::ExtraDataList* extraList =
                entry->extraLists && !entry->extraLists->empty() ?
                entry->extraLists->front() : nullptr;
            if (!extraList) {
                extraList = createExtraDataList();
                if (!entry->extraLists) {
                    entry->extraLists =
                        new RE::BSSimpleList<RE::ExtraDataList*>();
                }
                if (extraList) {
                    entry->extraLists->push_front(extraList);
                }
            }
            if (!extraList) return false;
            changes->SetFavorite(entry, extraList);
        } else {
            RE::ExtraDataList* favoriteList = nullptr;
            if (entry->extraLists) {
                for (auto* extraList : *entry->extraLists) {
                    if (extraList &&
                        extraList->HasType(RE::ExtraDataType::kHotkey)) {
                        favoriteList = extraList;
                        break;
                    }
                }
            }
            if (!favoriteList) return false;
            changes->RemoveFavorite(entry, favoriteList);
        }

        invalidateRefreshCache();
        scheduleRefresh(0.05f);
        logger::info(
            "DragonBoardVR: {} inventory favorite {:08X} '{}'.",
            wasFavorited ? "removed" : "added",
            formID,
            item->GetName());
        return true;
    }

    bool VRUIInventoryContainer::activateItem(RE::FormID formID, EquipHand hand)
    {
        if (!VRMenuManager::get().canEquip()) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* form = RE::TESForm::LookupByID(formID);
        auto* item = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!player || !item || !isEquippableItem(item)) return false;

        auto liveInventory = player->GetInventory(
            [formID](RE::TESBoundObject& object) { return object.formID == formID; });
        const auto inventoryIt = liveInventory.find(item);
        if (inventoryIt == liveInventory.end() || inventoryIt->second.first <= 0) {
            logger::warn(
                "DragonBoardVR: Item {:08X} ({}) no longer in inventory. Skipping activation.",
                formID, item->GetName());
            scheduleRefresh(0.05f);
            return false;
        }

        if (!player->Get3D() || !player->Is3DLoaded()) {
            logger::warn("DragonBoardVR: Player 3D not loaded, skipping inventory activation.");
            return false;
        }

        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!equipManager) return false;

        const bool isLeft = hand == EquipHand::kLeft;
        const bool isArmor = item->Is(RE::FormType::Armor);
        const bool isWeapon = item->Is(RE::FormType::Weapon);
        const bool isSpell = item->Is(RE::FormType::Spell) || item->Is(RE::FormType::Scroll);
        const bool isLight = item->Is(RE::FormType::Light);
        const std::int32_t count = inventoryIt->second.first;
        auto* inventoryEntry = inventoryIt->second.second.get();

        bool equipped = false;
        if (isArmor) {
            equipped = inventoryEntry && inventoryEntry->IsWorn();
            if (!equipped) {
                auto* armor = item->As<RE::TESObjectARMO>();
                auto* worn = armor ? player->GetWornArmor(armor->GetSlotMask()) : nullptr;
                equipped = worn && worn->formID == formID;
            }
        } else if (isWeapon || isSpell || isLight) {
            auto* current = player->GetEquippedObject(isLeft);
            equipped = current && current->formID == formID;
        }

        RE::ExtraDataList* extraList = nullptr;
        RE::InventoryEntryData* liveInventoryEntry = nullptr;
        auto* changes = player->GetInventoryChanges();
        if (changes && changes->entryList) {
            for (auto* entry : *changes->entryList) {
                if (entry && entry->object == item) {
                    liveInventoryEntry = entry;
                    if (entry->extraLists && !entry->extraLists->empty()) {
                        extraList = entry->extraLists->front();
                    }
                    break;
                }
            }
        }

        if (equipped && (isWeapon || isLight)) {
            // When identical items occupy both hands, the equip slot alone is
            // not sufficient: Skyrim may remove whichever inventory instance
            // matches extraList. Resolve the instance marked for the clicked
            // hand so left trigger always toggles left and right toggles right.
            extraList = nullptr;
            const auto wornType = isLeft ?
                RE::ExtraDataType::kWornLeft : RE::ExtraDataType::kWorn;
            if (liveInventoryEntry && liveInventoryEntry->extraLists) {
                for (auto* candidate : *liveInventoryEntry->extraLists) {
                    if (candidate && candidate->HasType(wornType)) {
                        extraList = candidate;
                        break;
                    }
                }
            }
        }

        if (equipped) {
            if (isArmor) {
                VRMenuManager::get().performSkeletonChangeSafely([player, item, extraList]() {
                    if (auto* manager = RE::ActorEquipManager::GetSingleton()) {
                        manager->UnequipObject(player, item, extraList, 1, nullptr);
                        VRMenuManager::get().notifyEquip();
                        VRMenuManager::get().scheduleEquipRefresh(0.15f);
                    }
                });
                return true;
            }
            if (isWeapon && dragonboard::ui::equipment::EquipInteractionController::
                    RequiresSkeletonBridge(item)) {
                const auto slotFormID = isLeft ? 0x13F43 : 0x13F42;
                auto* slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID);
                VRMenuManager::get().performSkeletonChangeSafely(
                    [player, item, extraList, slot]() {
                        if (auto* manager = RE::ActorEquipManager::GetSingleton()) {
                            manager->UnequipObject(player, item, extraList, 1, slot);
                            VRMenuManager::get().notifyEquip();
                            VRMenuManager::get().scheduleEquipRefresh(0.15f);
                        }
                    });
                return true;
            }
            if (isSpell) {
                ItemUtils::PapyrusUnequipSpell(
                    player, item->As<RE::SpellItem>(), isLeft ? 0 : 1);
            } else {
                const auto slotFormID = isLeft ? 0x13F43 : 0x13F42;
                auto* slot = (isWeapon || isLight) ?
                    RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID) : nullptr;
                equipManager->UnequipObject(player, item, extraList, 1, slot);
            }
            VRMenuManager::get().notifyEquip();
            VRMenuManager::get().scheduleEquipRefresh(0.15f);
            logger::info(
                "DragonBoardVR: unequipped {:08X} '{}' from {} hand "
                "(matchedExtraList={}).",
                formID,
                item->GetName(),
                isLeft ? "left" : "right",
                extraList != nullptr);
            return true;
        }

        auto* equippedOther = player->GetEquippedObject(!isLeft);
        const bool sameWeaponInOtherHand =
            isWeapon && equippedOther && equippedOther->formID == formID;

        if (sameWeaponInOtherHand && count >= 2) {
            // Each equipped copy must use a different inventory instance. Reusing
            // the ExtraDataList marked as worn makes Skyrim move that instance
            // between hands instead of equipping the second item from the stack.
            // Prefer an unworn extra list (enchanted/tempered copies); nullptr
            // intentionally selects an unmodified item from the base stack.
            extraList = nullptr;
            if (liveInventoryEntry && liveInventoryEntry->extraLists) {
                for (auto* candidate : *liveInventoryEntry->extraLists) {
                    if (!candidate ||
                        candidate->HasType(RE::ExtraDataType::kWorn) ||
                        candidate->HasType(RE::ExtraDataType::kWornLeft)) {
                        continue;
                    }
                    extraList = candidate;
                    break;
                }
            }
            logger::info(
                "DragonBoardVR: equipping second copy of {:08X} '{}' in {} hand "
                "(count={}, separateExtraList={}).",
                formID,
                item->GetName(),
                isLeft ? "left" : "right",
                count,
                extraList != nullptr);
        }

        const auto slotFormID = isLeft ? 0x13F43 : 0x13F42;
        auto* slot = (isWeapon || isSpell || isLight) ?
            RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID) : nullptr;
        if (isArmor) {
            VRMenuManager::get().performSkeletonChangeSafely([player, item, extraList]() {
                if (auto* manager = RE::ActorEquipManager::GetSingleton()) {
                    manager->EquipObject(player, item, extraList, 1, nullptr);
                    if (!player->IsOnMount()) player->DrawWeaponMagicHands(true);
                    VRMenuManager::get().notifyEquip();
                    VRMenuManager::get().scheduleEquipRefresh(0.15f);
                }
            });
            return true;
        }
        if (isWeapon && dragonboard::ui::equipment::EquipInteractionController::
                RequiresSkeletonBridge(item)) {
            VRMenuManager::get().performSkeletonChangeSafely(
                [player, item, extraList, slot, sameWeaponInOtherHand, count, isLeft]() {
                    auto* manager = RE::ActorEquipManager::GetSingleton();
                    if (!manager) return;

                    if (count < 2 && sameWeaponInOtherHand) {
                        auto* otherSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(
                            !isLeft ? 0x13F43 : 0x13F42);
                        manager->UnequipObject(player, item, nullptr, 1, otherSlot);
                    }
                    manager->EquipObject(player, item, extraList, 1, slot);
                    if (!player->IsOnMount()) {
                        player->DrawWeaponMagicHands(true);
                    }
                    VRMenuManager::get().notifyEquip();
                    VRMenuManager::get().scheduleEquipRefresh(0.15f);
                });
            return true;
        }

        equipManager->EquipObject(player, item, extraList, 1, slot);
        if (isWeapon || isSpell || isLight) {
            if (!player->IsOnMount()) player->DrawWeaponMagicHands(true);
        }
        VRMenuManager::get().notifyEquip();
        VRMenuManager::get().scheduleEquipRefresh(0.15f);
        logger::trace("DragonBoardVR: Activated from RmlUi inventory: {}", item->GetName());
        return true;
    }

    bool VRUIInventoryContainer::spawnItemInHand(RE::FormID formID, EquipHand hand)
    {
        auto& menuManager = VRMenuManager::get();
        if (!menuManager.canEquip()) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* form = RE::TESForm::LookupByID(formID);
        auto* item = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!player || !item) return false;
        if ((item->formFlags & 0x400u) != 0) {
            logger::warn(
                "DragonBoardVR: Quest item {:08X} ({}) cannot be spawned into a HIGGS hand.",
                formID, item->GetName());
            return false;
        }

        auto inventory = player->GetInventory(
            [formID](RE::TESBoundObject& object) { return object.formID == formID; });
        const auto inventoryIt = inventory.find(item);
        if (inventoryIt == inventory.end() || inventoryIt->second.first <= 0) {
            logger::warn(
                "DragonBoardVR: Item {:08X} ({}) no longer in inventory. Skipping HIGGS interaction.",
                formID, item->GetName());
            scheduleRefresh(0.05f);
            return false;
        }

        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) return false;

        taskInterface->AddTask([formID, hand]() {
            auto* livePlayer = RE::PlayerCharacter::GetSingleton();
            auto* liveForm = RE::TESForm::LookupByID(formID);
            auto* liveItem = liveForm ? liveForm->As<RE::TESBoundObject>() : nullptr;
            if (!livePlayer || !liveItem) return;

            const auto countBefore = livePlayer->GetItemCount(liveItem);
            if (countBefore <= 0) return;

            if (auto* book = liveItem->As<RE::TESObjectBOOK>();
                book && (book->TeachesSpell() || book->TeachesSkill())) {
                if (auto* spell = book->GetSpell()) {
                    livePlayer->AddSpell(spell);
                    const std::string message = "Learned: " + std::string(spell->GetName());
                    RE::DebugNotification(message.c_str());
                    if (auto* learnSound =
                            RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x01ADC3)) {
                        RE::BSSoundHandle handle;
                        if (RE::BSAudioManager::GetSingleton()->BuildSoundDataFromDescriptor(
                                handle, learnSound)) {
                            handle.Play();
                        }
                    }
                    livePlayer->RemoveItem(
                        book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                } else if (book->TeachesSkill()) {
                    book->Read(livePlayer);
                    livePlayer->RemoveItem(
                        book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                }
                return;
            }

            livePlayer->RemoveItem(
                liveItem, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            if (livePlayer->GetItemCount(liveItem) >= countBefore) {
                logger::warn(
                    "DragonBoardVR: Skyrim rejected removal of {:08X} ({}); no world reference was spawned.",
                    formID, liveItem->GetName());
                return;
            }

            auto reference = livePlayer->PlaceObjectAtMe(liveItem, false);
            if (!reference) {
                livePlayer->AddObjectToContainer(liveItem, nullptr, 1, nullptr);
                logger::warn(
                    "DragonBoardVR: Failed to spawn {:08X} ({}) for HIGGS; item returned to inventory.",
                    formID, liveItem->GetName());
                return;
            }

            const bool isLeft = hand == EquipHand::kLeft;
            RE::NiNode* handNode = nullptr;
            auto* root = livePlayer->Get3D(false);
            if (!root) root = livePlayer->Get3D(true);
            if (root) {
                const std::array<const char*, 3> nodeNames = isLeft ?
                    std::array{
                        "NPC L MagicNode [LMag]",
                        "NPC L Hand [LHnd]",
                        "Left Wand Node" } :
                    std::array{
                        "NPC R MagicNode [RMag]",
                        "NPC R Hand [RHnd]",
                        "Right Wand Node" };
                for (const auto* nodeName : nodeNames) {
                    if (auto* object = root->GetObjectByName(nodeName)) {
                        handNode = object->AsNode();
                        if (handNode) break;
                    }
                }
            }

            if (handNode) {
                float pitch = 0.0f;
                float yaw = 0.0f;
                float roll = 0.0f;
                VRUILayoutManager::getMatrixEuler(
                    handNode->world.rotate, pitch, yaw, roll);
                (void)dragonboard::runtime::vr::SetReferenceTransform(
                    reference.get(),
                    handNode->world.translate,
                    { pitch, yaw, roll });
            }

            if (g_higgsInterface) {
                SKSE::GetTaskInterface()->AddTask([reference, isLeft, formID]() {
                    if (!reference || !g_higgsInterface) return;

                    auto* rawReference = reference.get();
                    if (g_higgsInterface->CanGrabObject(rawReference, isLeft)) {
                        g_higgsInterface->GrabObject(rawReference, isLeft);
                        logger::info(
                            "DragonBoardVR: Inventory item {:08X} spawned and grabbed with HIGGS.",
                            formID);
                    } else {
                        logger::warn(
                            "DragonBoardVR: HIGGS could not grab spawned inventory item {:08X}.",
                            formID);
                    }
                });
            }
        });

        menuManager.notifyEquip();
        scheduleRefresh(0.15f);
        return true;
    }

    bool VRUIInventoryContainer::dropItem(RE::FormID formID)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* form = RE::TESForm::LookupByID(formID);
        auto* item = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!player || !item) return false;

        auto inventory = player->GetInventory(
            [formID](RE::TESBoundObject& object) { return object.formID == formID; });
        const auto it = inventory.find(item);
        if (it == inventory.end() || it->second.first <= 0) return false;

        auto* inventoryEntry = it->second.second.get();
        const bool equipped = inventoryEntry && inventoryEntry->IsWorn();
        RE::ExtraDataList* dropExtraList = nullptr;
        if (equipped && inventoryEntry->extraLists) {
            for (auto* candidate : *inventoryEntry->extraLists) {
                if (candidate &&
                    (candidate->HasType(RE::ExtraDataType::kWorn) ||
                     candidate->HasType(RE::ExtraDataType::kWornLeft))) {
                    dropExtraList = candidate;
                    break;
                }
            }
        }

        // Skyrim VR cannot safely infer an equipped inventory instance from a
        // null ExtraDataList.  Passing nullptr for a worn item enters
        // Actor::RemoveItem with an invalid extra-data path and can crash in
        // BSExtraDataList::GetExtraDataWithoutLocking.  Refuse the operation
        // if the exact worn instance cannot be identified.
        if (equipped && !dropExtraList) {
            logger::warn(
                "DragonBoardVR: refused to drop equipped {:08X} '{}' because "
                "its worn ExtraDataList could not be resolved.",
                formID,
                item->GetName());
            scheduleRefresh(0.05f);
            return false;
        }

        logger::info(
            "DragonBoardVR: dropping inventory item {:08X} '{}' "
            "(equipped={}, matchedExtraList={}).",
            formID,
            item->GetName(),
            equipped,
            dropExtraList != nullptr);
        player->DropObject(item, dropExtraList, 1, nullptr, nullptr);
        if (equipped) {
            VRMenuManager::get().notifyEquip();
            VRMenuManager::get().scheduleEquipRefresh(0.15f);
        }
        invalidateRefreshCache();
        scheduleRefresh(0.1f);
        logger::trace("DragonBoardVR: Dropped from RmlUi inventory: {}", item->GetName());
        return true;
    }

    void VRUIInventoryContainer::appendInventoryButton(const PendingInventoryItem& pending)
    {
        auto* form = RE::TESForm::LookupByID(pending.formID);
        auto* item = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!item) {
            return;
        }

        auto button = std::make_shared<VRUIButton>(pending.label, pending.modelPath, "",
            2.0f, 2.0f, pending.rotX, pending.rotY, pending.rotZ,
            pending.xOff, pending.yOff, pending.zOff, pending.scaleMult, true,
            ItemUtils::getItemTransformSource(item));
        button->setNoPopAnimation(true);
        button->setUseDynamicLabelOffset(true);
        button->setShowLabelsOnHoverOnly(true);
        button->setItemRotationPersistence(
            item->formID,
            pending.xOff, pending.yOff, pending.zOff,
            pending.scaleMult,
            pending.rotX, pending.rotY, pending.rotZ);

        _formToButton[item->formID] = button;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            bool isEquipped = false;
            if (item->Is(RE::FormType::Armor)) {
                auto* worn = player->GetWornArmor(item->As<RE::TESObjectARMO>()->GetSlotMask());
                if (worn && worn->formID == item->formID) {
                    isEquipped = true;
                }
            } else {
                auto* leftEq = player->GetEquippedObject(true);
                auto* rightEq = player->GetEquippedObject(false);
                isEquipped = (leftEq && leftEq->formID == item->formID) ||
                    (rightEq && rightEq->formID == item->formID);
            }
            if (isEquipped) {
                button->setEquipped(true);
            }
        }

        auto weakSelf = std::weak_ptr<VRUIInventoryContainer>(
            std::static_pointer_cast<VRUIInventoryContainer>(shared_from_this()));

        const uint32_t fID = item->formID;
        const std::string label = pending.label;
        const std::string modelPath = pending.modelPath;
        const float rotX = pending.rotX;
        const float rotY = pending.rotY;
        const float rotZ = pending.rotZ;
        const float xOff = pending.xOff;
        const float yOff = pending.yOff;
        const float zOff = pending.zOff;
        const float scaleMult = pending.scaleMult;
        button->setOnSecondaryPressHandler([label, modelPath, fID, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult](VRUIButton*, EquipHand) {
            auto& settings = VRUISettings::get();
            if (!settings.editModeEnabled) return;

            std::string category = "Misc";
            std::string lowerPath = modelPath;
            for (auto& c : lowerPath) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lowerPath.find("weapon") != std::string::npos || lowerPath.find("shield") != std::string::npos) category = "Weapons";
            else if (lowerPath.find("armor") != std::string::npos || lowerPath.find("clothes") != std::string::npos) category = "Armor";
            else if (lowerPath.find("alchemy") != std::string::npos || lowerPath.find("potion") != std::string::npos) category = "Potions";
            else if (lowerPath.find("food") != std::string::npos || lowerPath.find("ingredient") != std::string::npos) category = "Food";

            if (auto panel = VRMenuManager::get().findPanelByName("ItemEditPanel")) {
                if (auto* editContainer = dynamic_cast<VRUIItemEditPanel*>(panel->findWidgetByName("ItemEditContainer"))) {
                    editContainer->setTargetItem(category, label, modelPath, fID, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);
                }
                VRMenuManager::get().switchToPanel("ItemEditPanel");
            }
        });

        button->setOnPressHandler([fID, weakSelf](VRUIButton*, EquipHand hand) {
            if (auto self = weakSelf.lock()) {
                self->interactWithItem(fID, hand);
            }
        });

        button->setOnGripDragHandler([fID, weakSelf](VRUIButton*, EquipHand) {
            if (auto self = weakSelf.lock()) {
                self->dropItem(fID);
            }
        });

        button->setOnLongPressHandler([fID, weakSelf](VRUIButton*, EquipHand) {
            if (auto self = weakSelf.lock()) {
                self->toggleFavorite(fID);
            }
        });

        addChild(button);
    }



    void VRUIInventoryContainer::updateEquippedStates()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto* leftEq  = player->GetEquippedObject(true);
        auto* rightEq = player->GetEquippedObject(false);
        uint32_t leftFormID  = leftEq  ? leftEq->formID  : 0u;
        uint32_t rightFormID = rightEq ? rightEq->formID : 0u;

        for (auto& [formID, weakBtn] : _formToButton) {
            auto btn = weakBtn.lock();
            if (!btn) continue;
            bool isNowEquipped = (formID == leftFormID || formID == rightFormID);
            
            if (!isNowEquipped) {
                auto* form = RE::TESForm::LookupByID(formID);
                if (form && form->Is(RE::FormType::Armor)) {
                    auto* armor = form->As<RE::TESObjectARMO>();
                    auto* worn = player->GetWornArmor(armor->GetSlotMask());
                    if (worn && worn->formID == formID) {
                        isNowEquipped = true;
                    }
                }
            }
            btn->setEquipped(isNowEquipped);
        }
    }

    int VRUIInventoryContainer::getTotalPages() const
    {
        if (_pageSize <= 0) return 1;
        if (_totalValidItems == 0) return 1;
        return static_cast<int>(std::ceil((float)_totalValidItems / _pageSize));
    }

    void VRUIInventoryContainer::setPage(int page)
    {
        int total = getTotalPages();
        if (total > 0) {
            _currentPage = std::clamp(page, 0, total - 1);
        } else {
            _currentPage = 0;
        }

        refresh();
    }

    void VRUIInventoryContainer::recalculateLayout()
    {
        int truePage = _currentPage;
        _currentPage = 0; // Temporarily spoof for grid layout math, because this container pre-slices its children
        VRUIDynamicContainer::recalculateLayout();
        _currentPage = truePage;
    }

    std::string VRUIInventoryContainer::getModelPath(RE::TESBoundObject* item)
    {
        return ItemUtils::getModelPath(item);
    }
}
