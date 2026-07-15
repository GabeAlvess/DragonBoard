#include "VRUIInventoryContainer.h"
#include "runtime/vr/ReferencePlacement.h"
#include "VRUIItemUtils.h"
#include "VRUIItemEditPanel.h"
#include "VRUIButton.h"
#include "VRUILayoutManager.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESObjectBOOK.h>
#include <RE/M/MagicFavorites.h>
#include <RE/I/InventoryChanges.h>
#include <RE/I/InventoryEntryData.h>
#include <RE/E/ExtraDataList.h>
#include <RE/E/ExtraDataTypes.h>
#include <RE/M/MemoryManager.h>
#include <RE/B/BSAudioManager.h>
#include "VRMenuManager.h"
#include "higgsinterface001.h"
#include <SKSE/API.h>

extern HiggsPluginAPI::IHiggsInterface001* g_higgsInterface;

namespace vrui
{
    namespace
    {
        constexpr std::size_t kInitialInventoryBuildCount = 4;
        constexpr std::size_t kInventoryBuildsPerFrame = 3;
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
            || type == RE::FormType::Scroll;      // Scrolls (usable / equippable)
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

    void VRUIInventoryContainer::appendInventoryButton(const PendingInventoryItem& pending)
    {
        auto* form = RE::TESForm::LookupByID(pending.formID);
        auto* item = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!item) {
            return;
        }

        auto button = std::make_shared<VRUIButton>(pending.label, pending.modelPath, "",
            2.0f, 2.0f, pending.rotX, pending.rotY, pending.rotZ,
            pending.xOff, pending.yOff, pending.zOff, pending.scaleMult, true);
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
        const int count = pending.count;

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

        const bool isBookOrMisc = item->Is(RE::FormType::Book) || item->Is(RE::FormType::Misc);
        if (isBookOrMisc) {
            button->setOnPressHandler([item](VRUIButton*, EquipHand hand) {
                auto* p = RE::PlayerCharacter::GetSingleton();
                if (!p || !item) return;

                auto liveInv = p->GetInventory([&](RE::TESBoundObject& obj) { return obj.formID == item->formID; });
                auto liveIt = liveInv.find(item);
                if (liveIt == liveInv.end() || liveIt->second.first <= 0) {
                    logger::warn("DragonBoardVR: Item {:X} ({}) no longer in inventory.", item->formID, item->GetName());
                    return;
                }

                auto* book = item->As<RE::TESObjectBOOK>();
                auto* ti = SKSE::GetTaskInterface();
                if (!ti) return;

                const bool isTome = book ? (book->TeachesSpell() || book->TeachesSkill()) : false;
                ti->AddTask([item, book, p, isTome, hand]() {
                    if (isTome) {
                        if (auto* spell = book->GetSpell()) {
                            p->AddSpell(spell);
                            std::string msg = "Learned: " + std::string(spell->GetName());
                            RE::DebugNotification(msg.c_str());
                            auto* learnSound = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x01ADC3);
                            if (learnSound) {
                                RE::BSSoundHandle handle;
                                if (RE::BSAudioManager::GetSingleton()->BuildSoundDataFromDescriptor(handle, learnSound))
                                    handle.Play();
                            }
                            p->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                        } else if (book->TeachesSkill()) {
                            book->Read(p);
                            p->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                        }
                        return;
                    }

                    p->RemoveItem(item, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                    auto refPointer = p->PlaceObjectAtMe(item, false);
                    if (refPointer) {
                        auto* ref = refPointer.get();
                        RE::NiNode* handNode = nullptr;
                        auto* root = p->Get3D(false);
                        if (!root) root = p->Get3D(true);
                        if (root) {
                            bool isLeft = (hand == vrui::EquipHand::kLeft);
                            auto* obj = root->GetObjectByName(isLeft ? "NPC L MagicNode [LMag]" : "NPC R MagicNode [RMag]");
                            if (!obj) obj = root->GetObjectByName(isLeft ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]");
                            if (obj) handNode = obj->AsNode();
                        }
                        if (handNode) {
                            float pr, yr, rr;
                            vrui::VRUILayoutManager::getMatrixEuler(handNode->world.rotate, pr, yr, rr);
                            (void)dragonboard::runtime::vr::SetReferenceTransform(
                                ref,
                                handNode->world.translate,
                                { pr, yr, rr });
                        }
                        if (g_higgsInterface) {
                            SKSE::GetTaskInterface()->AddTask([refPointer, hand]() {
                                if (refPointer && g_higgsInterface) {
                                    bool isLeft = (hand == vrui::EquipHand::kLeft);
                                    if (g_higgsInterface->CanGrabObject(refPointer.get(), isLeft))
                                        g_higgsInterface->GrabObject(refPointer.get(), isLeft);
                                }
                            });
                        }
                    }
                });
            });
        } else {
            button->setOnPressHandler([item, count, weakSelf](VRUIButton*, EquipHand hand) {
                if (!VRMenuManager::get().canEquip()) return;

                auto* p = RE::PlayerCharacter::GetSingleton();
                if (p && item) {
                    auto liveInv = p->GetInventory([&](RE::TESBoundObject& obj) { return obj.formID == item->formID; });
                    auto it = liveInv.find(item);
                    if (it == liveInv.end() || it->second.first <= 0) {
                        logger::warn("DragonBoardVR: Item {:X} ({}) no longer in inventory. Skipping equip.", item->formID, item->GetName());
                        if (auto self = weakSelf.lock()) self->scheduleRefresh();
                        return;
                    }

                    if (!p->Get3D() || !p->Is3DLoaded()) {
                        logger::warn("DragonBoardVR: Player 3D not loaded, skipping equip.");
                        return;
                    }

                    auto* actorEquipManager = RE::ActorEquipManager::GetSingleton();
                    if (actorEquipManager) {
                        bool isLeft = (hand == EquipHand::kLeft);
                        bool isArmor = item->Is(RE::FormType::Armor);
                        bool isWeapon = item->Is(RE::FormType::Weapon);
                        bool isSpell = item->Is(RE::FormType::Spell) || item->Is(RE::FormType::Scroll);
                        bool isLight = item->Is(RE::FormType::Light);

                        bool isActuallyEquipped = false;
                        if (isArmor) {
                            auto* worn = p->GetWornArmor(item->As<RE::TESObjectARMO>()->GetSlotMask());
                            if (worn && worn->formID == item->formID) isActuallyEquipped = true;
                        } else if (isWeapon || isSpell || isLight) {
                            auto* alreadyEquipped = p->GetEquippedObject(isLeft);
                            if (alreadyEquipped && alreadyEquipped->formID == item->formID) isActuallyEquipped = true;
                        }

                        RE::ExtraDataList* xList = nullptr;
                        auto* changes = p->GetInventoryChanges();
                        if (changes && changes->entryList) {
                            for (auto* ed : *changes->entryList) {
                                if (ed && ed->object == item) {
                                    if (ed->extraLists && !ed->extraLists->empty()) {
                                        xList = ed->extraLists->front();
                                    }
                                    break;
                                }
                            }
                        }

                        if (isActuallyEquipped) {
                            if (isArmor) {
                                VRMenuManager::get().performArmorChangeSafely([p, item, xList]() {
                                    if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
                                        mgr->UnequipObject(p, item, xList, 1, nullptr);
                                        VRMenuManager::get().notifyEquip();
                                        VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                    }
                                });
                                return;
                            }
                            if (isSpell) {
                                ItemUtils::PapyrusUnequipSpell(p, item->As<RE::SpellItem>(), isLeft ? 0 : 1);
                            } else {
                                auto slotFormID = isLeft ? 0x13F43 : 0x13F42;
                                auto slot = (isWeapon || isSpell || isLight) ? RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID) : nullptr;
                                actorEquipManager->UnequipObject(p, item, xList, 1, slot);
                            }
                            VRMenuManager::get().notifyEquip();
                            logger::trace("DragonBoardVR: Unequipped: {}", item->GetName());
                            VRMenuManager::get().scheduleEquipRefresh(0.15f);
                            return;
                        }

                        if (count < 2 && isWeapon) {
                            auto* equippedOther = p->GetEquippedObject(!isLeft);
                            if (equippedOther && equippedOther->formID == item->formID) {
                                auto otherSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(!isLeft ? 0x13F43 : 0x13F42);
                                actorEquipManager->UnequipObject(p, item, nullptr, 1, otherSlot);
                            }
                        }

                        auto slotFormID = isLeft ? 0x13F43 : 0x13F42;
                        auto slot = (isWeapon || isSpell || isLight) ? RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID) : nullptr;
                        if (isArmor) {
                            VRMenuManager::get().performArmorChangeSafely([p, item, xList]() {
                                if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
                                    mgr->EquipObject(p, item, xList, 1, nullptr);
                                    if (!p->IsOnMount()) p->DrawWeaponMagicHands(true);
                                    VRMenuManager::get().notifyEquip();
                                    VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                }
                            });
                            return;
                        }
                        actorEquipManager->EquipObject(p, item, xList, 1, slot);

                        if (isWeapon || isSpell || isArmor || isLight) {
                            if (!p->IsOnMount()) {
                                p->DrawWeaponMagicHands(true);
                            }
                        }

                        VRMenuManager::get().notifyEquip();
                        logger::trace("DragonBoardVR: Equipped: {}", item->GetName());
                        VRMenuManager::get().scheduleEquipRefresh(0.15f);
                    }
                }
            });
        }

        button->setOnGripDragHandler([item, weakSelf](VRUIButton*, EquipHand) {
            auto* p = RE::PlayerCharacter::GetSingleton();
            if (p && item) {
                p->DropObject(item, nullptr, 1, nullptr, nullptr);
                if (auto self = weakSelf.lock()) {
                    self->scheduleRefresh(0.1f);
                }
            }
        });

        button->setOnLongPressHandler([item](VRUIButton* btn, EquipHand) {
            auto* p = RE::PlayerCharacter::GetSingleton();
            if (!p || !item) return;

            auto* changes = p->GetInventoryChanges();
            if (!changes || !changes->entryList) return;

            RE::InventoryEntryData* theEntry = nullptr;
            for (auto* ed : *changes->entryList) {
                if (ed && ed->object == item) { theEntry = ed; break; }
            }
            if (!theEntry) return;

            const bool wasFavorited = theEntry->IsFavorited();
            if (!wasFavorited) {
                RE::ExtraDataList* xList = (theEntry->extraLists && !theEntry->extraLists->empty())
                    ? theEntry->extraLists->front() : nullptr;

                if (!xList) {
                    xList = createExtraDataList();
                    if (!theEntry->extraLists) {
                        theEntry->extraLists = new RE::BSSimpleList<RE::ExtraDataList*>();
                    }
                    theEntry->extraLists->push_front(xList);
                }

                if (xList) {
                    changes->SetFavorite(theEntry, xList);
                    if (btn) btn->setLabel(std::string("* ") + item->GetName());
                }
            } else {
                if (theEntry->extraLists) {
                    for (auto* xList : *theEntry->extraLists) {
                        if (xList && xList->HasType(RE::ExtraDataType::kHotkey)) {
                            changes->RemoveFavorite(theEntry, xList);
                            break;
                        }
                    }
                }
                if (btn) btn->setLabel(item->GetName());
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
