#include "VRUIMagicContainer.h"

#include "VRUIButton.h"
#include "VRUIItemEditPanel.h"
#include "VRUIItemUtils.h"
#include "VRMenuManager.h"

#include <RE/B/BSString.h>
#include <RE/E/Effect.h>
#include <RE/E/EffectSetting.h>
#include <RE/M/MagicFavorites.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESIcon.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string_view>

namespace vrui
{
    namespace
    {
        class SpellCollector final : public RE::Actor::ForEachSpellVisitor
        {
        public:
            explicit SpellCollector(std::vector<RE::SpellItem*>& spells) :
                _spells(spells)
            {
            }

            RE::BSContainer::ForEachResult Visit(RE::SpellItem* spell) override
            {
                if (spell) _spells.push_back(spell);
                return RE::BSContainer::ForEachResult::kContinue;
            }

        private:
            std::vector<RE::SpellItem*>& _spells;
        };

        std::vector<RE::SpellItem*> collectPlayerSpells(RE::PlayerCharacter* player)
        {
            std::vector<RE::SpellItem*> spells;
            if (!player) return spells;

            SpellCollector collector(spells);
            player->VisitSpells(collector);
            std::sort(spells.begin(), spells.end(), [](const auto* left, const auto* right) {
                return left->formID < right->formID;
            });
            spells.erase(
                std::unique(spells.begin(), spells.end(), [](const auto* left, const auto* right) {
                    return left->formID == right->formID;
                }),
                spells.end());
            return spells;
        }

        bool isPower(const RE::SpellItem* spell)
        {
            if (!spell) return false;
            const auto type = spell->GetSpellType();
            return type == RE::MagicSystem::SpellType::kPower ||
                type == RE::MagicSystem::SpellType::kLesserPower ||
                type == RE::MagicSystem::SpellType::kVoicePower;
        }

        bool isPassive(const RE::SpellItem* spell)
        {
            if (!spell) return false;
            const auto type = spell->GetSpellType();
            return type == RE::MagicSystem::SpellType::kAbility ||
                type == RE::MagicSystem::SpellType::kDisease ||
                type == RE::MagicSystem::SpellType::kAddiction;
        }

        bool isSupportedSpell(const RE::SpellItem* spell)
        {
            return spell &&
                (spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell ||
                 isPower(spell) ||
                 isPassive(spell));
        }

        bool passesFilter(const RE::SpellItem* spell, MagicFilterMode filter)
        {
            if (!isSupportedSpell(spell)) return false;
            if (filter == MagicFilterMode::All) return true;
            if (filter == MagicFilterMode::Powers) return isPower(spell);
            if (filter == MagicFilterMode::Passive) return isPassive(spell);
            if (spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) return false;

            const auto skill = spell->GetAssociatedSkill();
            switch (filter) {
            case MagicFilterMode::Destruction:
                return skill == RE::ActorValue::kDestruction;
            case MagicFilterMode::Conjuration:
                return skill == RE::ActorValue::kConjuration;
            case MagicFilterMode::Restoration:
                return skill == RE::ActorValue::kRestoration;
            case MagicFilterMode::Illusion:
                return skill == RE::ActorValue::kIllusion;
            case MagicFilterMode::Alteration:
                return skill == RE::ActorValue::kAlteration;
            default:
                return true;
            }
        }

        bool isFavorited(const RE::SpellItem* spell)
        {
            auto* favorites = RE::MagicFavorites::GetSingleton();
            if (!favorites || !spell) return false;
            return std::find(favorites->spells.begin(), favorites->spells.end(), spell) !=
                    favorites->spells.end() ||
                std::find(favorites->hotkeys.begin(), favorites->hotkeys.end(), spell) !=
                    favorites->hotkeys.end();
        }

        std::string lowerCopy(std::string_view value)
        {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return result;
        }

        std::string cleanDescription(std::string_view raw)
        {
            std::string result;
            result.reserve(std::min<std::size_t>(raw.size(), 320));
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
                if (result.size() >= 320) break;
            }
            return result;
        }

        std::string formatNumber(float value)
        {
            if (!std::isfinite(value)) return "0";
            const auto rounded = std::llround(value);
            if (std::abs(value - static_cast<float>(rounded)) < 0.05f) {
                return std::to_string(rounded);
            }
            std::ostringstream stream;
            stream.setf(std::ios::fixed);
            stream.precision(1);
            stream << value;
            return stream.str();
        }

        void replaceEffectToken(
            std::string& text,
            std::string_view token,
            std::string_view replacement)
        {
            const auto lowerToken = lowerCopy(token);
            std::size_t searchFrom = 0;
            while (searchFrom < text.size()) {
                const auto lowerText = lowerCopy(text);
                const auto position = lowerText.find(lowerToken, searchFrom);
                if (position == std::string::npos) break;
                text.replace(position, token.size(), replacement);
                searchFrom = position + replacement.size();
            }
        }

        std::string resolveSpellDescription(const RE::SpellItem* spell)
        {
            if (!spell) return "No description available.";

            std::string description;
            for (auto* effect : spell->effects) {
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
                    resolved, "<mag>", formatNumber(effect->effectItem.magnitude));
                replaceEffectToken(
                    resolved, "<dur>", std::to_string(effect->effectItem.duration));
                replaceEffectToken(
                    resolved, "<area>", std::to_string(effect->effectItem.area));

                auto cleaned = cleanDescription(resolved);
                if (cleaned.empty()) continue;
                if (!description.empty()) description.push_back(' ');
                description += cleaned;
                if (description.size() >= 320) {
                    description.resize(320);
                    break;
                }
            }

            if (!description.empty()) return description;
            if (isPassive(spell)) return "A passive magical effect currently known by the player.";
            if (isPower(spell)) return "A magical power available to the player.";
            return "A learned spell available for casting.";
        }

        std::string resolveCategory(const RE::SpellItem* spell)
        {
            if (isPassive(spell)) return "PASSIVE";
            if (isPower(spell)) return "POWER";
            if (!spell) return "MAGIC";
            switch (spell->GetAssociatedSkill()) {
            case RE::ActorValue::kDestruction:
                return "DESTRUCTION";
            case RE::ActorValue::kConjuration:
                return "CONJURATION";
            case RE::ActorValue::kRestoration:
                return "RESTORATION";
            case RE::ActorValue::kIllusion:
                return "ILLUSION";
            case RE::ActorValue::kAlteration:
                return "ALTERATION";
            default:
                return "MAGIC";
            }
        }

        std::string resolveIconPath(const RE::SpellItem* spell)
        {
            const auto category = resolveCategory(spell);
            if (category == "DESTRUCTION") return "assets/destructionicon.png";
            if (category == "CONJURATION") return "assets/conjurationicon.png";
            if (category == "RESTORATION") return "assets/restorationicon.png";
            if (category == "ILLUSION") return "assets/illusionicon.png";
            if (category == "ALTERATION") return "assets/alterationicon.png";
            if (category == "POWER") return "assets/powericon.png";
            return "assets/passiveicon.png";
        }

        std::string resolveCastingType(const RE::SpellItem* spell)
        {
            if (!spell) return "--";
            switch (spell->GetCastingType()) {
            case RE::MagicSystem::CastingType::kConstantEffect:
                return "CONSTANT EFFECT";
            case RE::MagicSystem::CastingType::kFireAndForget:
                return "FIRE AND FORGET";
            case RE::MagicSystem::CastingType::kConcentration:
                return "CONCENTRATION";
            case RE::MagicSystem::CastingType::kScroll:
                return "SCROLL";
            default:
                return "--";
            }
        }

        std::string resolveDelivery(const RE::SpellItem* spell)
        {
            if (!spell) return "--";
            switch (spell->GetDelivery()) {
            case RE::MagicSystem::Delivery::kSelf:
                return "SELF";
            case RE::MagicSystem::Delivery::kTouch:
                return "TOUCH";
            case RE::MagicSystem::Delivery::kAimed:
                return "AIMED";
            case RE::MagicSystem::Delivery::kTargetActor:
                return "TARGET ACTOR";
            case RE::MagicSystem::Delivery::kTargetLocation:
                return "TARGET LOCATION";
            default:
                return "--";
            }
        }

        std::string resolveSkillLevel(const RE::SpellItem* spell)
        {
            if (!spell || isPower(spell) || isPassive(spell)) return "--";
            const auto* effect = spell->GetCostliestEffectItem();
            if (!effect || !effect->baseEffect) return "--";
            const auto level = effect->baseEffect->GetMinimumSkillLevel();
            if (level >= 100) return "MASTER";
            if (level >= 75) return "EXPERT";
            if (level >= 50) return "ADEPT";
            if (level >= 25) return "APPRENTICE";
            return "NOVICE";
        }

        std::string resolveDuration(const RE::SpellItem* spell)
        {
            if (!spell) return "--";
            if (spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
                return "CONSTANT";
            }
            if (spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration) {
                return "CONTINUOUS";
            }
            const auto duration = spell->GetLongestDuration();
            return duration > 0 ? std::to_string(duration) + " SEC" : "INSTANT";
        }

        std::string resolveRange(const RE::SpellItem* spell)
        {
            if (!spell) return "--";
            if (spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf) return "SELF";
            if (spell->GetDelivery() == RE::MagicSystem::Delivery::kTouch) return "TOUCH";
            const float range = spell->GetRange();
            return range > 0.0f ? formatNumber(range) : "--";
        }

        struct EquipmentInfo
        {
            bool left = false;
            bool right = false;
        };

        EquipmentInfo resolveEquipment(
            const RE::PlayerCharacter* player,
            const RE::SpellItem* spell)
        {
            EquipmentInfo result;
            if (!player || !spell) return result;
            const auto* left = player->GetEquippedObject(true);
            const auto* right = player->GetEquippedObject(false);
            result.left = left && left->formID == spell->formID;
            result.right = right && right->formID == spell->formID;
            return result;
        }

        float resolveMaximumMagicka(RE::PlayerCharacter* player)
        {
            if (!player) return 0.0f;
            const float current =
                player->GetActorValue(RE::ActorValue::kMagicka);
            const float maximum =
                player->GetPermanentActorValue(RE::ActorValue::kMagicka) +
                player->GetActorValueModifier(
                    RE::ACTOR_VALUE_MODIFIER::kTemporary,
                    RE::ActorValue::kMagicka);
            return std::max(current, maximum);
        }
    }

    VRUIMagicContainer::VRUIMagicContainer(
        const std::string& name,
        ContainerLayout layout,
        float spacing,
        float scale) :
        VRUIDynamicContainer(name, layout, spacing, scale)
    {
    }

    VRUIMagicContainer::RmlMagicSnapshot
    VRUIMagicContainer::buildRmlMagicSnapshot() const
    {
        RmlMagicSnapshot snapshot;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return snapshot;

        const char* playerName = player->GetName();
        snapshot.playerName =
            playerName && *playerName ? playerName : "Dragonborn";
        snapshot.playerLevel = player->GetLevel();
        snapshot.currentMagicka = player->GetActorValue(RE::ActorValue::kMagicka);
        snapshot.maximumMagicka = resolveMaximumMagicka(player);

        for (auto* spell : collectPlayerSpells(player)) {
            if (!passesFilter(spell, _currentFilter)) continue;

            RmlMagicItemData entry;
            entry.formID = spell->formID;
            const char* rawName = spell->GetName();
            entry.name = rawName && *rawName ? rawName : "Unknown spell";
            entry.category = resolveCategory(spell);
            entry.description = resolveSpellDescription(spell);
            entry.modelPath = ItemUtils::getModelPath(spell);
            entry.iconPath = resolveIconPath(spell);
            entry.castingType = resolveCastingType(spell);
            entry.delivery = resolveDelivery(spell);
            entry.skillLevel = resolveSkillLevel(spell);
            entry.duration = resolveDuration(spell);
            entry.range = resolveRange(spell);
            entry.isPower = isPower(spell);
            entry.isPassive = isPassive(spell);
            entry.canEquip = !entry.isPassive;
            entry.favorited = isFavorited(spell);
            const auto equipment = resolveEquipment(player, spell);
            entry.equippedLeft = equipment.left;
            entry.equippedRight = equipment.right;
            entry.equipped = equipment.left || equipment.right;
            if (entry.canEquip) {
                entry.magickaCost =
                    std::max(0.0f, spell->CalculateMagickaCost(player));
            }

            ItemUtils::getItemOverrides(
                spell,
                entry.rotX, entry.rotY, entry.rotZ,
                entry.xOff, entry.yOff, entry.zOff,
                entry.scaleMult);
            snapshot.items.push_back(std::move(entry));
        }

        std::sort(
            snapshot.items.begin(), snapshot.items.end(),
            [](const RmlMagicItemData& left, const RmlMagicItemData& right) {
                if (left.equipped != right.equipped) return left.equipped;
                const auto leftName = lowerCopy(left.name);
                const auto rightName = lowerCopy(right.name);
                if (leftName == rightName) return left.formID < right.formID;
                return leftName < rightName;
            });
        return snapshot;
    }

    std::uint64_t VRUIMagicContainer::buildRmlMagicSignature() const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;

        std::vector<std::uint64_t> rows;
        for (auto* spell : collectPlayerSpells(player)) {
            if (!isSupportedSpell(spell)) continue;
            std::uint64_t row = static_cast<std::uint64_t>(spell->formID) << 16;
            if (isFavorited(spell)) row ^= 0x8000000000000000ull;
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
        append(static_cast<std::uint64_t>(_currentFilter));
        append(static_cast<std::uint64_t>(std::llround(
            player->GetActorValue(RE::ActorValue::kMagicka) * 100.0f)));
        append(static_cast<std::uint64_t>(std::llround(
            resolveMaximumMagicka(player) * 100.0f)));
        append(player->GetLevel());
        if (const char* name = player->GetName()) {
            for (const unsigned char character : std::string_view(name)) {
                append(character);
            }
        }
        return signature;
    }

    bool VRUIMagicContainer::activateSpell(RE::FormID formID, EquipHand hand)
    {
        if (!VRMenuManager::get().canEquip()) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID);
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!player || !spell || !equipManager ||
            !player->HasSpell(spell) || isPassive(spell)) {
            return false;
        }
        if (!player->Get3D() || !player->Is3DLoaded()) {
            logger::warn("DragonBoardVR: Player 3D not loaded, skipping spell equip.");
            return false;
        }

        const bool isLeft = hand == EquipHand::kLeft;
        auto* equipped = player->GetEquippedObject(isLeft);
        if (equipped && equipped->formID == spell->formID) {
            ItemUtils::PapyrusUnequipSpell(player, spell, isLeft ? 0 : 1);
            VRMenuManager::get().notifyEquip();
            VRMenuManager::get().scheduleEquipRefresh(0.15f);
            logger::trace(
                "DragonBoardVR: Unequipped RmlUi spell '{}' from {} hand.",
                spell->GetName(), isLeft ? "left" : "right");
            return true;
        }

        const auto slotFormID = isLeft ? 0x13F43 : 0x13F42;
        auto* slot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(slotFormID);
        if (!slot) return false;

        equipManager->EquipSpell(player, spell, slot);
        if (!player->IsOnMount()) player->DrawWeaponMagicHands(true);
        VRMenuManager::get().notifyEquip();
        VRMenuManager::get().scheduleEquipRefresh(0.15f);
        logger::trace(
            "DragonBoardVR: Equipped RmlUi spell '{}' to {} hand.",
            spell->GetName(), isLeft ? "left" : "right");
        return true;
    }

    bool VRUIMagicContainer::toggleFavorite(RE::FormID formID)
    {
        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(formID);
        auto* favorites = RE::MagicFavorites::GetSingleton();
        if (!spell || !favorites) return false;

        const bool favorited = isFavorited(spell);
        if (favorited) favorites->RemoveFavorite(spell);
        else favorites->SetFavorite(spell);

        scheduleRefresh(0.05f);
        logger::info(
            "DragonBoardVR: {} magic favorite {:08X} '{}'.",
            favorited ? "removed" : "added",
            formID,
            spell->GetName());
        return true;
    }

    void VRUIMagicContainer::refresh()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        clearElements();
        _formToButton.clear();

        for (auto* spell : collectPlayerSpells(player)) {
            if (!passesFilter(spell, _currentFilter)) continue;

            const char* rawName = spell->GetName();
            const std::string name =
                rawName && *rawName ? rawName : "Unknown Spell";
            const std::string label =
                isFavorited(spell) ? "* " + name : name;
            const std::string modelPath = ItemUtils::getModelPath(spell);

            float rotX = 0.0f;
            float rotY = 0.0f;
            float rotZ = 0.0f;
            float xOff = 0.0f;
            float yOff = 0.0f;
            float zOff = 0.0f;
            float scaleMult = 1.0f;
            const auto transformSource = ItemUtils::getItemOverrides(
                spell,
                rotX, rotY, rotZ,
                xOff, yOff, zOff,
                scaleMult);

            std::string iconPath;
            if (auto* icon = spell->As<RE::TESIcon>();
                icon && !icon->textureName.empty()) {
                iconPath = "textures\\" + std::string(icon->textureName.c_str());
            }

            auto button = std::make_shared<VRUIButton>(
                label, modelPath, iconPath,
                2.0f, 2.0f,
                rotX, rotY, rotZ,
                xOff, yOff, zOff,
                scaleMult, true, transformSource);
            button->setNoPopAnimation(true);
            button->setUseDynamicLabelOffset(true);
            button->setShowLabelsOnHoverOnly(true);
            button->setItemRotationPersistence(
                spell->formID,
                xOff, yOff, zOff,
                scaleMult,
                rotX, rotY, rotZ);

            const auto equipment = resolveEquipment(player, spell);
            button->setEquipped(equipment.left || equipment.right);
            _formToButton[spell->formID] = button;

            auto weakSelf = std::weak_ptr<VRUIMagicContainer>(
                std::static_pointer_cast<VRUIMagicContainer>(shared_from_this()));
            const auto formID = spell->formID;
            button->setOnPressHandler([weakSelf, formID](VRUIButton*, EquipHand hand) {
                if (auto self = weakSelf.lock()) {
                    self->activateSpell(formID, hand);
                }
            });
            button->setOnLongPressHandler(
                [weakSelf, formID](VRUIButton* pressed, EquipHand) {
                    if (auto self = weakSelf.lock();
                        self && self->toggleFavorite(formID)) {
                        if (pressed) {
                            auto* liveSpell =
                                RE::TESForm::LookupByID<RE::SpellItem>(formID);
                            if (liveSpell) {
                                const std::string liveName = liveSpell->GetName();
                                pressed->setLabel(
                                    isFavorited(liveSpell) ?
                                        "* " + liveName : liveName);
                            }
                        }
                    }
                });
            button->setOnSecondaryPressHandler(
                [name, modelPath, formID,
                 rotX, rotY, rotZ,
                 xOff, yOff, zOff,
                 scaleMult](VRUIButton*, EquipHand) {
                    if (!VRUISettings::get().editModeEnabled) return;
                    if (auto panel =
                            VRMenuManager::get().findPanelByName("ItemEditPanel")) {
                        if (auto* editor = dynamic_cast<VRUIItemEditPanel*>(
                                panel->findWidgetByName("ItemEditContainer"))) {
                            editor->setTargetItem(
                                "Magic", name, modelPath, formID,
                                rotX, rotY, rotZ,
                                xOff, yOff, zOff,
                                scaleMult,
                                "MagicPanel");
                        }
                        VRMenuManager::get().switchToPanel("ItemEditPanel");
                    }
                });

            addElement(std::move(button));
        }

        recalculateLayout();
        logger::trace(
            "DragonBoardVR: Magic container '{}' refreshed with {} spells.",
            getName(), getChildren().size());
        if (_parent) _parent->onChildLayoutChanged(this);
    }

    void VRUIMagicContainer::updateEquippedStates()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        const auto* left = player->GetEquippedObject(true);
        const auto* right = player->GetEquippedObject(false);
        const auto leftFormID = left ? left->formID : 0u;
        const auto rightFormID = right ? right->formID : 0u;
        for (auto& [formID, weakButton] : _formToButton) {
            if (auto button = weakButton.lock()) {
                button->setEquipped(
                    formID == leftFormID || formID == rightFormID);
            }
        }
    }
}
