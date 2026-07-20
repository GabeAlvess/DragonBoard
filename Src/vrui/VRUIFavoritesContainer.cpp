#include "VRUIFavoritesContainer.h"
#include "ui/equipment/EquipInteractionController.h"
#include "runtime/vr/ReferencePlacement.h"
#include "VRUIItemUtils.h"
#include "VRUIButton.h"
#include "VRUIItemEditPanel.h"
#include <RE/F/FavoritesMenu.h>
#include <RE/M/MagicFavorites.h>
#include <RE/T/TESBoundObject.h>
#include <RE/E/ExtraDataList.h>
#include <RE/E/ExtraDataTypes.h>
#include "VRMenuManager.h"
#include "VRUILayoutManager.h"
#include "../higgsinterface001.h"

namespace
{
    bool IsFavoriteFormEquipped(RE::PlayerCharacter* player, RE::TESForm* form)
    {
        if (!player || !form) return false;

        auto* leftEquipped = player->GetEquippedObject(true);
        auto* rightEquipped = player->GetEquippedObject(false);
        if ((leftEquipped && leftEquipped->formID == form->formID) ||
            (rightEquipped && rightEquipped->formID == form->formID)) {
            return true;
        }

        if (auto* armor = form->As<RE::TESObjectARMO>()) {
            auto* worn = player->GetWornArmor(armor->GetSlotMask());
            return worn && worn->formID == form->formID;
        }

        return false;
    }

    RE::BGSEquipSlot* GetFavoriteEquipSlot(RE::TESBoundObject* item, bool isLeft)
    {
        if (!item || item->Is(RE::FormType::Armor)) return nullptr;
        return RE::TESForm::LookupByID<RE::BGSEquipSlot>(isLeft ? 0x13F43 : 0x13F42);
    }

    RE::ExtraDataList* FindFavoriteExtraList(
        RE::PlayerCharacter* player,
        RE::TESBoundObject* item)
    {
        if (!player || !item) return nullptr;

        auto* changes = player->GetInventoryChanges();
        if (!changes || !changes->entryList) return nullptr;

        for (auto* entry : *changes->entryList) {
            if (entry && entry->object == item && entry->extraLists &&
                !entry->extraLists->empty()) {
                return entry->extraLists->front();
            }
        }

        return nullptr;
    }
}

namespace vrui
{
    VRUIFavoritesContainer::VRUIFavoritesContainer(const std::string& name, 
                                                 ContainerLayout layout,
                                                 float spacing, 
                                                 float scale)
        : VRUIDynamicContainer(name, layout, spacing, scale)
    {
    }

    void VRUIFavoritesContainer::refresh()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        clearElements();
        _formToButton.clear();

        // ----------------------------------------------------------------
        // 1. Favorited SPELLS, POWERS and SHOUTS (via MagicFavorites)
        // ----------------------------------------------------------------
        auto* magicFavs = RE::MagicFavorites::GetSingleton();
        if (magicFavs && (_currentFilter == InventoryFilterMode::All || _currentFilter == InventoryFilterMode::MagicAll)) {
            for (auto* form : magicFavs->spells) {
                if (!form) continue;
                auto* spell = form->As<RE::SpellItem>();
                if (!spell) continue;

                const char* spellName = spell->GetName();
                std::string label = (spellName && spellName[0] != '\0') ? spellName : "Favorite Spell";

                std::string modelPath = ItemUtils::getModelPath(spell);
                float rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult;
                const auto transformSource = ItemUtils::getItemOverrides(
                    spell, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);

                auto button = std::make_shared<VRUIButton>(label, modelPath, "",
                    2.0f, 2.0f, rotX, rotY, rotZ, xOff, yOff, zOff,
                    scaleMult, false, transformSource);
                button->setNoPopAnimation(true); // prevent render glitch with large models
                button->setUseDynamicLabelOffset(true); // push label forward of 3D model
                button->setShowLabelsOnHoverOnly(true);
                button->setItemRotationPersistence(spell->formID, xOff, yOff, zOff, scaleMult, rotX, rotY, rotZ);

                // Register in formID→button map for per-button equipped refresh
                _formToButton[spell->formID] = button;

                // Check if spell is equipped and mark it with indicator (use formID)
                {
                    auto* pl = RE::PlayerCharacter::GetSingleton();
                    if (pl) {
                        bool isEq = IsFavoriteFormEquipped(pl, spell);
                        if (isEq) button->setEquipped(true);
                    }
                }

                // Press: toggle equip/unequip spell
                button->setOnPressHandler([spell](VRUIButton*, EquipHand hand) {
                    if (!VRMenuManager::get().canEquip()) return;
                    auto* p = RE::PlayerCharacter::GetSingleton();
                    if (p && p->Get3D() && p->Is3DLoaded()) {
                        auto* mgr = RE::ActorEquipManager::GetSingleton();
                        if (mgr) {
                            bool isLeft = (hand == EquipHand::kLeft);
                            // Toggle unequip (use formID comparison for spell safety)
                            auto* curEq = p->GetEquippedObject(isLeft);
                            if (curEq && curEq->formID == spell->formID) {
                                // Use native Papyrus UnequipSpell to safely unequip and dismiss visual casting effects
                                ItemUtils::PapyrusUnequipSpell(p, spell, isLeft ? 0 : 1);
                                VRMenuManager::get().notifyEquip();
                                logger::trace("DragonBoardVR: Unequipped Spell: {}", spell->GetName());
                                VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                return;
                            }
                            auto slotFormID = (hand == EquipHand::kLeft) ? 0x13F43 : 0x13F42;
                            auto slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID);
                            mgr->EquipSpell(p, spell, slot);
                            if (!p->IsOnMount()) {
                                p->DrawWeaponMagicHands(true);
                            }
                            VRMenuManager::get().notifyEquip();
                            VRMenuManager::get().scheduleEquipRefresh(0.15f);
                        }
                    }
                });

                // Long-press: unfavorite spell, then deferred refresh
                auto weakSelf = std::weak_ptr<VRUIFavoritesContainer>(
                    std::static_pointer_cast<VRUIFavoritesContainer>(shared_from_this()));
                button->setOnLongPressHandler([spell, btnWeak = std::weak_ptr<VRUIButton>(button), weakSelf](VRUIButton*, EquipHand) {
                    auto* favs = RE::MagicFavorites::GetSingleton();
                    if (favs && spell) {
                        favs->RemoveFavorite(spell);
                        // RE::DebugNotification((std::string("Removed Favorite: ") + spell->GetName()).c_str());
                        logger::trace("DragonBoardVR: Unfavorited spell '{}'", spell->GetName());
                    }
                    if (auto btn = btnWeak.lock()) btn->setVisible(false);
                    VRMenuManager::get().clearHover();
                });

                addElement(std::move(button));
            }
        }

        // ----------------------------------------------------------------
        // 2. Favorited INVENTORY ITEMS
        // ----------------------------------------------------------------
        if (_currentFilter != InventoryFilterMode::MagicAll) {
            auto inventory = player->GetInventory([](RE::TESBoundObject& a_obj) {
                return a_obj.Is(RE::FormType::Weapon)      ||
                       a_obj.Is(RE::FormType::Armor)       ||
                       a_obj.Is(RE::FormType::Ammo)        ||
                       a_obj.Is(RE::FormType::AlchemyItem) ||
                       a_obj.Is(RE::FormType::Ingredient)  ||
                       a_obj.Is(RE::FormType::Misc)        ||
                       a_obj.Is(RE::FormType::KeyMaster)   ||
                       a_obj.Is(RE::FormType::Book);
            });

            for (auto& [item, data] : inventory) {
                if (!item || data.first <= 0) continue;
                if (!data.second || !data.second->IsFavorited()) continue;
                if (_currentFilter != InventoryFilterMode::All && !VRUIInventoryContainer::passesFilter(item, _currentFilter)) continue;

            const char* itemName = item->GetName();
            std::string label = (itemName && itemName[0] != '\0') ? itemName : "Favorite Item";
            
            if (data.first > 1) {
                label += " (" + std::to_string(data.first) + ")";
            }

            std::string modelPath = ItemUtils::getModelPath(item);

            logger::trace("DragonBoardVR: Favorite item: '{}' (count: {}, model: '{}')",
                label, data.first, modelPath);

            // Per-type rotation/scale via shared utility
            float rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult;
            const auto transformSource = ItemUtils::getItemOverrides(
                item, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);

            auto button = std::make_shared<VRUIButton>(label, modelPath, "",
                2.0f, 2.0f, rotX, rotY, rotZ, xOff, yOff, zOff,
                scaleMult, true, transformSource);
            button->setNoPopAnimation(true); // prevent render glitch with large models
            button->setUseDynamicLabelOffset(true); // push label forward of 3D model
            button->setShowLabelsOnHoverOnly(true);
            button->setItemRotationPersistence(item->formID, xOff, yOff, zOff, scaleMult, rotX, rotY, rotZ);

            // Register in formID→button map for per-button equipped refresh
            _formToButton[item->formID] = button;

            // Check if item is equipped and mark it with indicator (use formID)
            if (player) {
                bool isEquipped = IsFavoriteFormEquipped(player, item);
                if (isEquipped) button->setEquipped(true);
            }

            int32_t count = data.first;
            // Press: toggle equip/unequip item
            button->setOnPressHandler([item, count](VRUIButton*, EquipHand hand) {
                auto* p = RE::PlayerCharacter::GetSingleton();
                if (!p) return;

                // Guard: verify item is still in inventory before acting to prevent duplication
                auto liveInv = p->GetInventory([&](RE::TESBoundObject& obj) { return obj.formID == item->formID; });
                auto liveIt = liveInv.find(item);
                if (liveIt == liveInv.end() || liveIt->second.first <= 0) {
                    logger::warn("DragonBoardVR: Favorite item {:X} ({}) no longer in inventory. Skipping.", item->formID, item->GetName());
                    return;
                }

                if (item->Is(RE::FormType::Book)) {
                    auto* book = item->As<RE::TESObjectBOOK>();
                    if (!book) return;

                    logger::info("DragonBoardVR: Favorite Book interaction (HIGGS Grab): {:X} ({})", item->formID, item->GetName());
                    auto* ti = SKSE::GetTaskInterface();
                    if (!ti) return;

                    const bool isTome = book->TeachesSpell() || book->TeachesSkill();

                    ti->AddTask([item, book, p, isTome, ti, hand]() {
                        auto& manager = VRMenuManager::get();
                        
                        if (isTome) {
                            if (auto* spell = book->GetSpell()) {
                                p->AddSpell(spell);
                                std::string msg = "Aprendido: " + std::string(spell->GetName());
                                RE::DebugNotification(msg.c_str());
                                auto* learnSound = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x01ADC3);
                                if (learnSound) {
                                    RE::BSSoundHandle handle;
                                    if (RE::BSAudioManager::GetSingleton()->BuildSoundDataFromDescriptor(handle, learnSound)) {
                                        handle.Play();
                                    }
                                }
                                p->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                            } else if (book->TeachesSkill()) {
                                book->Read(p);
                                p->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                            }
                            return;
                        }

                        p->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                        auto refPointer = p->PlaceObjectAtMe(book, false);
                        if (refPointer) {
                            RE::NiNode* handNode = nullptr;
                            auto* root = p->Get3D(false);
                            if (!root) root = p->Get3D(true);
                            if (root) {
                                bool isLeft = (hand == vrui::EquipHand::kLeft);
                                auto* obj = root->GetObjectByName(isLeft ? "NPC L MagicNode [LMag]" : "NPC R MagicNode [RMag]");
                                if (!obj) obj = root->GetObjectByName(isLeft ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]");
                                if (!obj) obj = root->GetObjectByName(isLeft ? "Left Wand Node" : "Right Wand Node");
                                if (obj) handNode = obj->AsNode();
                            }

                            if (handNode) {
                                float pr, yr, rr;
                                vrui::VRUILayoutManager::getMatrixEuler(handNode->world.rotate, pr, yr, rr);
                                (void)dragonboard::runtime::vr::SetReferenceTransform(
                                    refPointer.get(),
                                    handNode->world.translate,
                                    { pr, yr, rr });
                            }
                            
                            if (g_higgsInterface) {
                                SKSE::GetTaskInterface()->AddTask([refPointer, hand]() {
                                    if (refPointer && g_higgsInterface) {
                                        auto* rawRef = refPointer.get();
                                        bool isLeft = (hand == vrui::EquipHand::kLeft);
                                        if (g_higgsInterface->CanGrabObject(rawRef, isLeft)) {
                                            g_higgsInterface->GrabObject(rawRef, isLeft);
                                            logger::info("DragonBoardVR: Favorite book spawned and HIGGS Grab triggered in next frame.");
                                        } else {
                                            logger::warn("DragonBoardVR: HIGGS hand not in grabbable state in next frame.");
                                        }
                                    }
                                });
                            }
                        }
                    });
                    return;
                }

                if (!VRMenuManager::get().canEquip()) return;
                if (p->Get3D() && p->Is3DLoaded()) {
                    auto* mgr = RE::ActorEquipManager::GetSingleton();
                    if (mgr) {
                        bool isLeft = (hand == EquipHand::kLeft);
                        auto* extraList = FindFavoriteExtraList(p, item);

                        if (IsFavoriteFormEquipped(p, item)) {
                            if (dragonboard::ui::equipment::EquipInteractionController::
                                    RequiresSkeletonBridge(item)) {
                                auto* protectedSlot = item->Is(RE::FormType::Weapon) ?
                                    GetFavoriteEquipSlot(item, isLeft) : nullptr;
                                VRMenuManager::get().performSkeletonChangeSafely([p, item, extraList, protectedSlot]() {
                                    if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
                                        mgr->UnequipObject(p, item, extraList, 1, protectedSlot);
                                        VRMenuManager::get().notifyEquip();
                                        VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                    }
                                });
                                return;
                            }
                            if (item->Is(RE::FormType::Spell)) {
                                ItemUtils::PapyrusUnequipSpell(p, item->As<RE::SpellItem>(), isLeft ? 0 : 1);
                            } else {
                                auto* slot = GetFavoriteEquipSlot(item, isLeft);
                                mgr->UnequipObject(p, item, extraList, 1, slot);
                            }
                            VRMenuManager::get().notifyEquip();
                            VRMenuManager::get().scheduleEquipRefresh(0.15f);
                            logger::trace("DragonBoardVR: Unequipped: {}", item->GetName());
                            return;
                        }

                        auto* slot = GetFavoriteEquipSlot(item, isLeft);
                        if (item->Is(RE::FormType::Weapon) &&
                            dragonboard::ui::equipment::EquipInteractionController::
                                RequiresSkeletonBridge(item)) {
                            VRMenuManager::get().performSkeletonChangeSafely(
                                [p, item, extraList, slot, count, isLeft]() {
                                    auto* equipManager = RE::ActorEquipManager::GetSingleton();
                                    if (!equipManager) return;

                                    if (count < 2) {
                                        auto* equippedOther = p->GetEquippedObject(!isLeft);
                                        if (equippedOther && equippedOther->formID == item->formID) {
                                            auto* otherSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(
                                                !isLeft ? 0x13F43 : 0x13F42);
                                            equipManager->UnequipObject(p, item, extraList, 1, otherSlot);
                                        }
                                    }
                                    equipManager->EquipObject(p, item, extraList, 1, slot);
                                    if (!p->IsOnMount()) p->DrawWeaponMagicHands(true);
                                    VRMenuManager::get().notifyEquip();
                                    VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                });
                            return;
                        }

                        // Prevent duping
                        if (count < 2 &&
                            (item->Is(RE::FormType::Weapon) || item->Is(RE::FormType::Armor))) {
                            auto* equippedOther = p->GetEquippedObject(!isLeft);
                            if (equippedOther && equippedOther->formID == item->formID) {
                                auto otherSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(!isLeft ? 0x13F43 : 0x13F42);
                                mgr->UnequipObject(p, item, extraList, 1, otherSlot);
                            }
                        }

                        if (item->Is(RE::FormType::Armor)) {
                            VRMenuManager::get().performSkeletonChangeSafely([p, item, extraList]() {
                                if (auto* mgr = RE::ActorEquipManager::GetSingleton()) {
                                    mgr->EquipObject(p, item, extraList, 1, nullptr);
                                    if (!p->IsOnMount()) p->DrawWeaponMagicHands(true);
                                    VRMenuManager::get().notifyEquip();
                                    VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                }
                            });
                            return;
                        }
                        mgr->EquipObject(p, item, extraList, 1, slot);
                        if (!p->IsOnMount()) {
                            p->DrawWeaponMagicHands(true);
                        }
                        VRMenuManager::get().notifyEquip();
                        VRMenuManager::get().scheduleEquipRefresh(0.15f);
                        logger::trace("DragonBoardVR: Equipped: {}", item->GetName());
                    }
                }
            });

            // Drag-to-Drop
            auto btnWeakDrag = std::weak_ptr<VRUIButton>(button);
            auto weakSelfDrag = std::weak_ptr<VRUIFavoritesContainer>(
                std::static_pointer_cast<VRUIFavoritesContainer>(shared_from_this()));

            button->setOnGripDragHandler([item, btnWeakDrag, weakSelfDrag](VRUIButton*, EquipHand) {
                auto* p = RE::PlayerCharacter::GetSingleton();
                if (p && item) {
                    // Drop 1 count of the item
                    p->DropObject(item, nullptr, 1, nullptr, nullptr);
                    // RE::DebugNotification((std::string("Dropped: ") + item->GetName()).c_str());

                    // Find our dynamic container instance and tell it to refresh
                    if (auto self = weakSelfDrag.lock()) {
                        self->scheduleRefresh(0.1f);
                    }
                }
            });

            // Long-press: unfavorite item de forma segura via InventoryChanges
            auto weakSelf = std::weak_ptr<VRUIFavoritesContainer>(
                std::static_pointer_cast<VRUIFavoritesContainer>(shared_from_this()));
            button->setOnLongPressHandler([item, btnWeak = std::weak_ptr<VRUIButton>(button), weakSelf](VRUIButton*, EquipHand) {
                auto* p = RE::PlayerCharacter::GetSingleton();
                if (p && item) {
                    auto* changes = p->GetInventoryChanges();
                    if (changes && changes->entryList) {
                        for (auto* entryData : *changes->entryList) {
                            if (entryData && entryData->object == item) {
                                if (entryData->extraLists) {
                                    for (auto* xList : *entryData->extraLists) {
                                        if (xList && xList->HasType(RE::ExtraDataType::kHotkey)) {
                                            changes->RemoveFavorite(entryData, xList);
                                            break; // Removido com segurança pela engine
                                        }
                                    }
                                }
                                break; // Item encontrado, podemos parar a busca
                            }
                        }
                    }
                    // RE::DebugNotification((std::string("Removed from Favorites: ") + item->GetName()).c_str());
                    logger::trace("DragonBoardVR: Unfavorited item '{}'", item->GetName());
                }
                
                if (auto btn = btnWeak.lock()) btn->setVisible(false);
                VRMenuManager::get().clearHover();
            });

            uint32_t fID = item->formID;
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
            addElement(std::move(button));
        }
        } // end if (!MagicAll)

        // ----------------------------------------------------------------
        // 3. MagicFavorites hotkeys
        // ----------------------------------------------------------------
        if (magicFavs) {
            for (auto* form : magicFavs->hotkeys) {
                if (!form) continue;

                auto* spell = form->As<RE::SpellItem>();
                if (spell) {
                    if (_currentFilter != InventoryFilterMode::All && _currentFilter != InventoryFilterMode::MagicAll) continue;
                    if (_formToButton.contains(spell->formID)) continue;
                    const char* spellName = spell->GetName();
                    std::string label = (spellName && spellName[0] != '\0') ? spellName : "";

                    // Spells in favorites also get the default rotation/offset
                    float rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult;
                    const auto transformSource = ItemUtils::getItemOverrides(
                        spell, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);

                    auto button = std::make_shared<VRUIButton>(label, "DragonBoardVR\\IconPlane.nif", "",
                        2.0f, 2.0f, rotX, rotY, rotZ, xOff, yOff, zOff,
                        scaleMult, false, transformSource);
                    button->setShowLabelsOnHoverOnly(true);
                    button->setItemRotationPersistence(spell->formID, xOff, yOff, zOff, scaleMult, rotX, rotY, rotZ);
                    _formToButton[spell->formID] = button;
                    button->setEquipped(IsFavoriteFormEquipped(player, spell));
                    button->setOnPressHandler([spell](VRUIButton*, EquipHand hand) {
                        if (!VRMenuManager::get().canEquip()) return;
                        auto* p = RE::PlayerCharacter::GetSingleton();
                        if (p && p->Get3D() && p->Is3DLoaded()) {
                            auto* mgr = RE::ActorEquipManager::GetSingleton();
                            if (mgr) {
                                const bool isLeft = hand == EquipHand::kLeft;
                                auto* equipped = p->GetEquippedObject(isLeft);
                                if (equipped && equipped->formID == spell->formID) {
                                    ItemUtils::PapyrusUnequipSpell(p, spell, isLeft ? 0 : 1);
                                    VRMenuManager::get().notifyEquip();
                                    VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                    logger::trace("DragonBoardVR: Unequipped hotkey spell: {}", spell->GetName());
                                    return;
                                }
                                auto slotFormID = (hand == EquipHand::kLeft) ? 0x13F43 : 0x13F42;
                                auto slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID);
                                mgr->EquipSpell(p, spell, slot);
                                if (!p->IsOnMount()) {
                                    p->DrawWeaponMagicHands(true);
                                }
                                VRMenuManager::get().notifyEquip();
                                VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                logger::trace("DragonBoardVR: Equipped hotkey spell: {}", spell->GetName());
                            }
                        }
                    });

                    uint32_t fID = spell->formID;
                    button->setOnSecondaryPressHandler([label, fID, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult](VRUIButton*, EquipHand) {
                        auto& settings = VRUISettings::get();
                        if (!settings.editModeEnabled) return;
                        std::string category = "Magic";
                        if (auto panel = VRMenuManager::get().findPanelByName("ItemEditPanel")) {
                            if (auto* editContainer = dynamic_cast<VRUIItemEditPanel*>(panel->findWidgetByName("ItemEditContainer"))) {
                                editContainer->setTargetItem(category, label, "", fID, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);
                            }
                            VRMenuManager::get().switchToPanel("ItemEditPanel");
                        }
                    });

                    addElement(std::move(button));
                    continue;
                }

                auto* obj = form->As<RE::TESBoundObject>();
                if (!obj) continue;
                if (_currentFilter == InventoryFilterMode::MagicAll) continue;
                if (_currentFilter != InventoryFilterMode::All && !VRUIInventoryContainer::passesFilter(obj, _currentFilter)) continue;
                if (_formToButton.contains(obj->formID)) continue;
                const char* itemName = obj->GetName();
                std::string label = (itemName && itemName[0] != '\0') ? itemName : "Hotkey Item";
                std::string modelPath = ItemUtils::getModelPath(obj);

                // Per-type rotation/scale via shared utility
                float rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult;
                const auto transformSource = ItemUtils::getItemOverrides(
                    obj, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);

                auto button = std::make_shared<VRUIButton>(label, modelPath, "",
                    2.0f, 2.0f, rotX, rotY, rotZ, xOff, yOff, zOff,
                    scaleMult, false, transformSource);
                button->setShowLabelsOnHoverOnly(true);
                button->setItemRotationPersistence(obj->formID, xOff, yOff, zOff, scaleMult, rotX, rotY, rotZ);
                _formToButton[obj->formID] = button;
                button->setEquipped(IsFavoriteFormEquipped(player, obj));
                button->setOnPressHandler([obj](VRUIButton*, EquipHand hand) {
                    if (!VRMenuManager::get().canEquip()) return;
                    auto* p = RE::PlayerCharacter::GetSingleton();
                    if (p && p->Get3D() && p->Is3DLoaded()) {
                        auto* mgr = RE::ActorEquipManager::GetSingleton();
                        if (mgr) {
                            const bool isLeft = hand == EquipHand::kLeft;
                            auto* extraList = FindFavoriteExtraList(p, obj);
                            auto* slot = GetFavoriteEquipSlot(obj, isLeft);
                            if (dragonboard::ui::equipment::EquipInteractionController::
                                    RequiresSkeletonBridge(obj)) {
                                const bool equipped = IsFavoriteFormEquipped(p, obj);
                                VRMenuManager::get().performSkeletonChangeSafely(
                                    [p, obj, extraList, equipped, slot]() {
                                        auto* equipManager = RE::ActorEquipManager::GetSingleton();
                                        if (!equipManager) return;

                                        if (equipped) {
                                            equipManager->UnequipObject(
                                                p,
                                                obj,
                                                extraList,
                                                1,
                                                obj->Is(RE::FormType::Weapon) ? slot : nullptr);
                                        } else {
                                            equipManager->EquipObject(
                                                p,
                                                obj,
                                                extraList,
                                                1,
                                                obj->Is(RE::FormType::Weapon) ? slot : nullptr);
                                            if (!p->IsOnMount()) {
                                                p->DrawWeaponMagicHands(true);
                                            }
                                        }
                                        VRMenuManager::get().notifyEquip();
                                        VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                    });
                                return;
                            }
                            if (IsFavoriteFormEquipped(p, obj)) {
                                mgr->UnequipObject(p, obj, extraList, 1, slot);
                                VRMenuManager::get().notifyEquip();
                                VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                logger::trace("DragonBoardVR: Unequipped hotkey item: {}", obj->GetName());
                                return;
                            }
                            mgr->EquipObject(p, obj, extraList, 1, slot);
                            if (!p->IsOnMount()) {
                                p->DrawWeaponMagicHands(true);
                            }
                            VRMenuManager::get().notifyEquip();
                            VRMenuManager::get().scheduleEquipRefresh(0.15f);
                            logger::trace("DragonBoardVR: Equipped hotkey item: {}", obj->GetName());
                        }
                    }
                });

                uint32_t fID = obj->formID;
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

                addElement(std::move(button));
            }
        }

        recalculateLayout();
        logger::trace("DragonBoardVR: Favorites container '{}' refreshed with {} entries.",
            getName(), getChildren().size());

        if (_parent) {
            _parent->onChildLayoutChanged(this);
        }
    }

    void VRUIFavoritesContainer::updateEquippedStates()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        for (auto& [formID, weakBtn] : _formToButton) {
            auto btn = weakBtn.lock();
            if (!btn) continue;
            auto* form = RE::TESForm::LookupByID(formID);
            btn->setEquipped(IsFavoriteFormEquipped(player, form));
        }
    }
}
