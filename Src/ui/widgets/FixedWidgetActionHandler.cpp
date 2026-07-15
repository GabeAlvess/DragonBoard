#include "ui/widgets/FixedWidgetActionHandler.h"

#include "game/actions/ActionExecutor.h"
#include "higgsinterface001.h"
#include "runtime/vr/ReferencePlacement.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIItemUtils.h"
#include "vrui/VRUILayoutManager.h"

#include <RE/A/ActorEquipManager.h>
#include <RE/B/BGSSoundDescriptorForm.h>
#include <RE/B/BSSoundHandle.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESObjectBOOK.h>

namespace dragonboard::ui::widgets
{
    namespace actions = dragonboard::game::actions;

    void FixedWidgetActionHandler::Execute(const vrui::FixedWidgetItem& data, vrui::EquipHand hand)
    {
        if (data.formID == 0 && data.actionFunc.empty()) {
            return;
        }

        if (data.formID == 0) {
            const auto action = actions::Parse(data.actionFunc);
            const auto side = hand == vrui::EquipHand::kLeft ?
                actions::EquipSide::kLeft : actions::EquipSide::kRight;
            (void)actions::Execute(action, side, actions::ExecutionContext::kPinnedWidget);
            return;
        }

        auto& menuManager = vrui::VRMenuManager::get();
        if (!menuManager.canEquip()) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* form = RE::TESForm::LookupByID(data.formID);
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!player || !form || !equipManager) {
            return;
        }

        const bool isLeft = hand == vrui::EquipHand::kLeft;
        const auto slotFormID = isLeft ? 0x13F43 : 0x13F42;
        auto* slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID);

        if (data.category == "Magic") {
            auto* spell = form->As<RE::SpellItem>();
            if (!spell || !player->HasSpell(spell)) {
                return;
            }

            auto* equipped = player->GetEquippedObject(isLeft);
            if (equipped && equipped->formID == spell->formID) {
                vrui::ItemUtils::PapyrusUnequipSpell(player, spell, isLeft ? 0 : 1);
                menuManager.notifyEquip();
                menuManager.scheduleEquipRefresh(0.15f);
                return;
            }

            equipManager->EquipSpell(player, spell, slot);
        } else {
            auto* bound = form->As<RE::TESBoundObject>();
            if (!bound || player->GetItemCount(bound) <= 0) {
                return;
            }

            if (bound->Is(RE::FormType::Book)) {
                auto* book = bound->As<RE::TESObjectBOOK>();
                if (!book) {
                    return;
                }

                logger::info("DragonBoardVR: Pinned Book interaction (HIGGS Grab): {:X} ({})", form->formID, form->GetName());
                auto* taskInterface = SKSE::GetTaskInterface();
                if (!taskInterface) {
                    return;
                }

                const bool isTome = book->TeachesSpell() || book->TeachesSkill();
                taskInterface->AddTask([form, book, player, isTome, hand]() {
                    if (isTome) {
                        if (auto* spell = book->GetSpell()) {
                            player->AddSpell(spell);
                            const std::string message = "Aprendido: " + std::string(spell->GetName());
                            RE::DebugNotification(message.c_str());
                            auto* learnSound = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x01ADC3);
                            if (learnSound) {
                                RE::BSSoundHandle handle;
                                if (RE::BSAudioManager::GetSingleton()->BuildSoundDataFromDescriptor(handle, learnSound)) {
                                    handle.Play();
                                }
                            }
                            player->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                        } else if (book->TeachesSkill()) {
                            book->Read(player);
                            player->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                        }
                        return;
                    }

                    player->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
                    auto reference = player->PlaceObjectAtMe(book, false);
                    if (!reference) {
                        return;
                    }

                    RE::NiNode* handNode = nullptr;
                    auto* root = player->Get3D(false);
                    if (!root) {
                        root = player->Get3D(true);
                    }
                    if (root) {
                        const bool useLeftHand = hand == vrui::EquipHand::kLeft;
                        auto* object = root->GetObjectByName(useLeftHand ? "NPC L MagicNode [LMag]" : "NPC R MagicNode [RMag]");
                        if (!object) {
                            object = root->GetObjectByName(useLeftHand ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]");
                        }
                        if (!object) {
                            object = root->GetObjectByName(useLeftHand ? "Left Wand Node" : "Right Wand Node");
                        }
                        if (object) {
                            handNode = object->AsNode();
                        }
                    }

                    if (handNode) {
                        float pitch = 0.0f;
                        float yaw = 0.0f;
                        float roll = 0.0f;
                        vrui::VRUILayoutManager::getMatrixEuler(handNode->world.rotate, pitch, yaw, roll);
                        (void)dragonboard::runtime::vr::SetReferenceTransform(
                            reference.get(),
                            handNode->world.translate,
                            { pitch, yaw, roll });
                    }

                    if (g_higgsInterface) {
                        SKSE::GetTaskInterface()->AddTask([reference, hand]() {
                            if (!reference || !g_higgsInterface) {
                                return;
                            }

                            auto* rawReference = reference.get();
                            const bool useLeftHand = hand == vrui::EquipHand::kLeft;
                            if (g_higgsInterface->CanGrabObject(rawReference, useLeftHand)) {
                                g_higgsInterface->GrabObject(rawReference, useLeftHand);
                                logger::info("DragonBoardVR: Pinned book spawned and HIGGS Grab triggered in next frame.");
                            } else {
                                logger::warn("DragonBoardVR: HIGGS hand not in grabbable state in next frame.");
                            }
                        });
                    }
                });
                return;
            }

            const bool isArmor = bound->Is(RE::FormType::Armor);
            const bool isWeapon = bound->Is(RE::FormType::Weapon);
            const bool isLight = bound->Is(RE::FormType::Light);

            bool isActuallyEquipped = false;
            if (isArmor) {
                if (auto* armor = bound->As<RE::TESObjectARMO>()) {
                    auto* worn = player->GetWornArmor(armor->GetSlotMask());
                    isActuallyEquipped = worn && worn->formID == bound->formID;
                }
            } else if (isWeapon || isLight) {
                auto* equippedHand = player->GetEquippedObject(isLeft);
                isActuallyEquipped = equippedHand && equippedHand->formID == bound->formID;
            }

            RE::ExtraDataList* extraData = nullptr;
            auto* changes = player->GetInventoryChanges();
            if (changes && changes->entryList) {
                for (auto* entry : *changes->entryList) {
                    if (entry && entry->object == bound) {
                        if (entry->extraLists && !entry->extraLists->empty()) {
                            extraData = entry->extraLists->front();
                        }
                        break;
                    }
                }
            }

            if (isActuallyEquipped) {
                auto* unequipSlot = (isWeapon || isLight) ? slot : nullptr;
                equipManager->UnequipObject(player, bound, extraData, 1, unequipSlot);
                menuManager.notifyEquip();
                menuManager.scheduleEquipRefresh(0.15f);
                return;
            }

            if (isWeapon) {
                auto* otherEquipped = player->GetEquippedObject(!isLeft);
                if (otherEquipped && otherEquipped->formID == bound->formID) {
                    auto* otherSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(!isLeft ? 0x13F43 : 0x13F42);
                    equipManager->UnequipObject(player, bound, nullptr, 1, otherSlot);
                }
            }

            auto* equipSlot = (isWeapon || isLight) ? slot : nullptr;
            equipManager->EquipObject(player, bound, extraData, 1, equipSlot);
        }

        if (!player->IsOnMount()) {
            player->DrawWeaponMagicHands(true);
        }
        menuManager.notifyEquip();
    }
}
