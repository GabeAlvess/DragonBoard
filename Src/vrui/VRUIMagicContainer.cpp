#include "VRUIMagicContainer.h"
#include "VRUIItemUtils.h"
#include "VRUIButton.h"
#include "VRMenuManager.h"
#include <RE/M/MagicFavorites.h>
#include <RE/N/NiNode.h>
#include "VRUIItemEditPanel.h"
#include <RE/B/BSModelDB.h>
#include <RE/T/TESIcon.h>
#include <RE/S/SpellItem.h>

namespace vrui
{
    VRUIMagicContainer::VRUIMagicContainer(const std::string& name, 
                                         ContainerLayout layout,
                                         float spacing, 
                                         float scale)
        : VRUIDynamicContainer(name, layout, spacing, scale)
    {
    }

    void VRUIMagicContainer::refresh()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        clearElements();

        // Visitor class to collect spells
        class SpellCollector : public RE::Actor::ForEachSpellVisitor
        {
        public:
            SpellCollector(VRUIMagicContainer* container) : _container(container) {}

            RE::BSContainer::ForEachResult Visit(RE::SpellItem* a_spell) override
            {
                if (a_spell) {
                    auto spellType = a_spell->GetSpellType();
                    bool isPower = (spellType == RE::MagicSystem::SpellType::kPower || 
                                    spellType == RE::MagicSystem::SpellType::kLesserPower || 
                                    spellType == RE::MagicSystem::SpellType::kVoicePower);
                    bool isPassive = (spellType == RE::MagicSystem::SpellType::kAbility ||
                                      spellType == RE::MagicSystem::SpellType::kDisease ||
                                      spellType == RE::MagicSystem::SpellType::kAddiction);
                    bool isSpell = (spellType == RE::MagicSystem::SpellType::kSpell);

                    if (!isPower && !isSpell && !isPassive) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }

                    auto filter = _container->_currentFilter;

                    if (filter == MagicFilterMode::Passive && !isPassive) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    } else if (filter == MagicFilterMode::Powers && !isPower) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    } else if (filter != MagicFilterMode::All && filter != MagicFilterMode::Passive && filter != MagicFilterMode::Powers) {
                        if (!isSpell) return RE::BSContainer::ForEachResult::kContinue;
                        RE::ActorValue associatedSkill = a_spell->GetAssociatedSkill();
                        if (filter == MagicFilterMode::Destruction && associatedSkill != RE::ActorValue::kDestruction) return RE::BSContainer::ForEachResult::kContinue;
                        if (filter == MagicFilterMode::Conjuration && associatedSkill != RE::ActorValue::kConjuration) return RE::BSContainer::ForEachResult::kContinue;
                        if (filter == MagicFilterMode::Restoration && associatedSkill != RE::ActorValue::kRestoration) return RE::BSContainer::ForEachResult::kContinue;
                        if (filter == MagicFilterMode::Illusion    && associatedSkill != RE::ActorValue::kIllusion)    return RE::BSContainer::ForEachResult::kContinue;
                        if (filter == MagicFilterMode::Alteration  && associatedSkill != RE::ActorValue::kAlteration)  return RE::BSContainer::ForEachResult::kContinue;
                    }
                    
                    const char* name = a_spell->GetName();
                    std::string label = name ? name : "Unknown Spell";

                    bool isFavorited = false;
                    auto* favs = RE::MagicFavorites::GetSingleton();
                    if (favs) {
                        if (std::find(favs->spells.begin(), favs->spells.end(), a_spell) != favs->spells.end() ||
                            std::find(favs->hotkeys.begin(), favs->hotkeys.end(), a_spell) != favs->hotkeys.end()) {
                            isFavorited = true;
                            label = "* " + label; // Visual indicator
                        }
                    }

                    // --- Resolve spell model (NIF) and rotation (via shared ItemUtils) ---
                    std::string modelPath = ItemUtils::getModelPath(a_spell);
                    
                    float rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult;
                    ItemUtils::getItemOverrides(a_spell, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);

                    // --- Resolve spell icon (DDS) via TESIcon ---
                    std::string iconPath;
                    auto* icon = a_spell->As<RE::TESIcon>();
                    if (icon && !icon->textureName.empty()) {
                        iconPath = "textures\\" + std::string(icon->textureName.c_str());
                    }

                    auto button = std::make_shared<VRUIButton>(label, modelPath, iconPath,
                        2.0f, 2.0f, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult, true);
                    button->setNoPopAnimation(true); // prevent render glitch with large models
                    button->setUseDynamicLabelOffset(true); // push label forward of 3D model
                    button->setShowLabelsOnHoverOnly(true);
                    button->setItemRotationPersistence(a_spell->formID, xOff, yOff, zOff, scaleMult, rotX, rotY, rotZ);

                    // Register in formID→button map for per-button equipped refresh
                    _container->_formToButton[a_spell->formID] = button;

                    // Check if spell is equipped and mark it with indicator
                    {
                        auto* pl = RE::PlayerCharacter::GetSingleton();
                        if (pl) {
                            auto* leftEq  = pl->GetEquippedObject(true);
                            auto* rightEq = pl->GetEquippedObject(false);
                            bool isEq = (leftEq  && leftEq->formID  == a_spell->formID) ||
                                        (rightEq && rightEq->formID == a_spell->formID);
                            if (isEq) button->setEquipped(true);
                        }
                    }

                    auto weakSelf = std::weak_ptr<VRUIMagicContainer>(
                        std::static_pointer_cast<VRUIMagicContainer>(_container->shared_from_this()));

                    // Set interaction: Toggle equip/unequip the spell
                    button->setOnPressHandler([a_spell, weakSelf](VRUIButton*, EquipHand hand) {
                        if (!VRMenuManager::get().canEquip()) return;

                        auto* player = RE::PlayerCharacter::GetSingleton();
                        if (player && a_spell) {
                            if (!player->Get3D() || !player->Is3DLoaded()) {
                                logger::warn("DragonBoardVR: Player 3D not loaded, skipping equip.");
                                return;
                            }

                            auto* actorEquipManager = RE::ActorEquipManager::GetSingleton();
                            if (actorEquipManager) {
                                bool isLeft = (hand == EquipHand::kLeft);

                                // Toggle: if already equipped on requested hand, unequip instead
                                auto* alreadyEquipped = player->GetEquippedObject(isLeft);
                                if (alreadyEquipped && alreadyEquipped->formID == a_spell->formID) {
                                    // Use native Papyrus UnequipSpell to safely unequip and dismiss visual casting effects
                                    ItemUtils::PapyrusUnequipSpell(player, a_spell, isLeft ? 0 : 1);
                                    VRMenuManager::get().notifyEquip();
                                    logger::trace("DragonBoardVR: Unequipped Spell: {}", a_spell->GetName());
                                    VRMenuManager::get().scheduleEquipRefresh(0.15f);
                                    return;
                                }

                                auto slotFormID = (hand == EquipHand::kLeft) ? 0x13F43 : 0x13F42;
                                auto slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID);
                                actorEquipManager->EquipSpell(player, a_spell, slot);
                                if (!player->IsOnMount()) {
                                    player->DrawWeaponMagicHands(true);
                                }
                                VRMenuManager::get().notifyEquip();
                                logger::trace("DragonBoardVR: Equipped Spell: {}", a_spell->GetName());
                                VRMenuManager::get().scheduleEquipRefresh(0.15f);
                            }
                        }
                    });

                    uint32_t fID = a_spell->formID;
                    button->setOnSecondaryPressHandler([label, modelPath, fID, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult](VRUIButton*, EquipHand) {
                        auto& settings = VRUISettings::get();
                        if (!settings.editModeEnabled) return;

                        std::string category = "Magic";
                        if (auto panel = VRMenuManager::get().findPanelByName("ItemEditPanel")) {
                            if (auto* editContainer = dynamic_cast<VRUIItemEditPanel*>(panel->findWidgetByName("ItemEditContainer"))) {
                                editContainer->setTargetItem(category, label, modelPath, fID, rotX, rotY, rotZ, xOff, yOff, zOff, scaleMult);
                            }
                            VRMenuManager::get().switchToPanel("ItemEditPanel");
                        }
                    });

                    auto weakSelfForLong = std::weak_ptr<VRUIMagicContainer>(
                        std::static_pointer_cast<VRUIMagicContainer>(_container->shared_from_this()));
                    button->setOnLongPressHandler([a_spell](VRUIButton* btn, EquipHand) {
                        auto* magicFavs = RE::MagicFavorites::GetSingleton();
                        if (magicFavs && a_spell) {
                            bool isFav = false;
                            if (std::find(magicFavs->spells.begin(), magicFavs->spells.end(), a_spell) != magicFavs->spells.end() ||
                                std::find(magicFavs->hotkeys.begin(), magicFavs->hotkeys.end(), a_spell) != magicFavs->hotkeys.end()) {
                                isFav = true;
                            }
                            if (isFav) {
                                magicFavs->RemoveFavorite(a_spell);
                                if (btn) btn->setLabel(a_spell->GetName());
                                // RE::DebugNotification((std::string("Removed Favorite: ") + a_spell->GetName()).c_str());
                            } else {
                                magicFavs->SetFavorite(a_spell);
                                if (btn) btn->setLabel(std::string("* ") + a_spell->GetName());
                                // RE::DebugNotification((std::string("Added Favorite: ") + a_spell->GetName()).c_str());
                            }
                        }
                    });

                    _container->addElement(std::move(button));
                }
                return RE::BSContainer::ForEachResult::kContinue;
            }

        private:
            VRUIMagicContainer* _container;
        };

        SpellCollector collector(this);
        player->VisitSpells(collector);

        recalculateLayout();
        logger::trace("DragonBoardVR: Magic container '{}' refreshed with {} spells.", getName(), getChildren().size());
        
        if (_parent) {
            _parent->onChildLayoutChanged(this);
        }
    }

    void VRUIMagicContainer::updateEquippedStates()
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
            btn->setEquipped(isNowEquipped);
        }
    }
}
