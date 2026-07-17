#include "VRUIItemUtils.h"
#include "VRUISettings.h"
#include <RE/T/TESForm.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESModel.h>
#include <RE/T/TESBipedModelForm.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESObjectLIGH.h>
#include <RE/S/SpellItem.h>
#include <RE/B/BGSMenuDisplayObject.h>
#include <REL/Relocation.h>
#include <RE/A/Actor.h>
#include <RE/T/TESFile.h>
#include <algorithm>
#include <format>

namespace vrui::ItemUtils
{
    namespace
    {
        std::string resolveOverrideCategory(RE::TESForm* form)
        {
            if (!form) return "Misc";

            const auto formType = form->GetFormType();
            if (formType == RE::FormType::Book) return "Books";
            if (formType == RE::FormType::Spell) return "Magic";
            if (formType == RE::FormType::Weapon || formType == RE::FormType::Ammo) return "Weapons";
            if (formType == RE::FormType::Armor) return "Armor";

            if (auto* alchemy = form->As<RE::AlchemyItem>()) {
                return alchemy->IsFood() ? "Food" : "Potions";
            }
            if (formType == RE::FormType::Ingredient) return "Food";
            if (formType == RE::FormType::Misc) return "Misc";

            std::string lowerPath = getModelPath(form);
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (lowerPath.find("weapon") != std::string::npos || lowerPath.find("shield") != std::string::npos) return "Weapons";
            if (lowerPath.find("armor") != std::string::npos || lowerPath.find("clothes") != std::string::npos) return "Armor";
            if (lowerPath.find("alchemy") != std::string::npos || lowerPath.find("potion") != std::string::npos) return "Potions";
            if (lowerPath.find("food") != std::string::npos || lowerPath.find("ingredient") != std::string::npos) return "Food";
            return "Misc";
        }
    }

    void PapyrusUnequipSpell(RE::Actor* actor, RE::SpellItem* spell, int source)
    {
        if (!actor || !spell) return;
        // 0x0984D00 is the raw SkyrimVR 1.4.15 offset for native Actor.UnequipSpell implementation
        using _UnequipSpell = void(*)(void* registry, std::uint32_t stackId, RE::Actor* akActor, RE::SpellItem* akSpell, int aiSource);
        REL::Relocation<_UnequipSpell> func{ REL::Offset(0x0984D00) };
        if (func.get()) {
            func(nullptr, 0, actor, spell, source);
        }
    }

    std::string getModelPath(RE::TESForm* form)
    {
        auto& settings = vrui::VRUISettings::get();
        if (!form) return settings.unknownNifPath;

        // 1. Try BGSMenuDisplayObject for Spells
        auto* spell = form->As<RE::SpellItem>();
        if (spell) {
            auto* menuDispObj = spell->As<RE::BGSMenuDisplayObject>();
            if (menuDispObj) {
                auto* dispObject = menuDispObj->GetMenuDisplayObject();
                if (dispObject) {
                    auto* dispModel = dispObject->As<RE::TESModel>();
                    if (dispModel && dispModel->model.c_str() && dispModel->model.c_str()[0] != '\0') {
                        return dispModel->model.c_str();
                    }
                }
            }

            // 1b. Fallback: If spell has no visual model, use the "Unknown" placeholder (Unknow.nif)
            return settings.unknownNifPath;
        }

        // 2. Try TESObjectLIGH (Specific unlit torch override)
        auto* light = form->As<RE::TESObjectLIGH>();
        if (light) {
            std::string originalModel = light->model.c_str();
            for (auto& c : originalModel) c = std::tolower(c);
            if (form->formID == 0x0001D4EC || originalModel.find("torch") != std::string::npos) {
                return "vrik\\torchoff.nif";
            }
        }
        
        // 3. Try TESModel (Weapons, Potions, etc.)
        auto* model = form->As<RE::TESModel>();
        if (model && model->model.c_str() && model->model.c_str()[0] != '\0')
            return model->model.c_str();

        // 3. Try TESBipedModelForm for Armor and Clothes
        auto* biped = form->As<RE::TESBipedModelForm>();
        if (biped) {
            const auto& wm = biped->worldModels[RE::TESBipedModelForm::Sexes::kMale];
            if (wm.model.c_str() && wm.model.c_str()[0] != '\0')
                return wm.model.c_str();
        }

        return settings.unknownNifPath;
    }

    std::string sanitizeName(const std::string& name)
    {
        std::string cleaned = name;
        // Remove all '*' characters (common favorite prefix)
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '*'), cleaned.end());

        // Trim leading/trailing whitespace
        auto start = cleaned.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        auto end = cleaned.find_last_not_of(" \t\r\n");
        return cleaned.substr(start, end - start + 1);
    }

    std::string getStableItemOverrideKey(RE::TESForm* form)
    {
        if (!form || form->IsDynamicForm()) return {};
        auto* file = form->GetFile(0);
        if (!file || file->GetFilename().empty()) return {};
        return std::format("{}|{:08X}", file->GetFilename(), form->GetLocalFormID());
    }

    bool findItemOverride(RE::TESForm* form, ItemOffsetData& data)
    {
        if (!form) return false;
        auto& settings = VRUISettings::get();
        const auto stableKey = getStableItemOverrideKey(form);
        if (!stableKey.empty()) {
            if (const auto it = settings.stableItemOverrides.find(stableKey);
                it != settings.stableItemOverrides.end()) {
                data = it->second;
                return true;
            }
        }

        if (const auto it = settings.itemOverrides.find(form->GetFormID());
            it != settings.itemOverrides.end()) {
            data = it->second;
            return true;
        }
        return false;
    }

    void setItemOverride(RE::TESForm* form, const ItemOffsetData& data)
    {
        if (!form) return;
        auto& settings = VRUISettings::get();
        const auto stableKey = getStableItemOverrideKey(form);
        if (!stableKey.empty()) {
            settings.stableItemOverrides[stableKey] = data;
            settings.itemOverrides.erase(form->GetFormID());
            logger::info(
                "DragonBoardVR: stored stable item override key='{}' runtimeFormID={:08X}",
                stableKey, form->GetFormID());
        } else {
            settings.itemOverrides[form->GetFormID()] = data;
            logger::warn(
                "DragonBoardVR: item {:08X} has no stable plugin key; using runtime FormID override",
                form->GetFormID());
        }
    }

    void eraseItemOverride(RE::TESForm* form)
    {
        if (!form) return;
        auto& settings = VRUISettings::get();
        const auto stableKey = getStableItemOverrideKey(form);
        if (!stableKey.empty()) settings.stableItemOverrides.erase(stableKey);
        settings.itemOverrides.erase(form->GetFormID());
    }

    bool isExplicitOverride(ItemTransformSource source)
    {
        return source == ItemTransformSource::ItemOverride ||
               source == ItemTransformSource::CategoryOverride;
    }

    ItemTransformSource getItemTransformSource(RE::TESForm* form)
    {
        float rotX, rotY, rotZ, xOff, yOff, zOff, scale;
        return getItemOverrides(form, rotX, rotY, rotZ, xOff, yOff, zOff, scale);
    }

    ItemTransformSource getItemOverrides(RE::TESForm* form,
                                         float& rotX, float& rotY, float& rotZ,
                                         float& xOff, float& yOff, float& zOff,
                                         float& scaleMult)
    {
        const float kNaN = std::numeric_limits<float>::quiet_NaN();
        rotX = kNaN; rotY = kNaN; rotZ = kNaN;
        xOff = 0.0f;
        yOff = 0.05f;
        zOff = 0.0f;
        scaleMult = 1.0f;

        if (!form) return ItemTransformSource::Default;

        uint32_t formID = form->GetFormID();
        std::string rawName = form->GetName();
        std::string name = sanitizeName(rawName);
        auto fType = form->GetFormType();

        auto& settings = vrui::VRUISettings::get();

        // Player-authored per-item overrides are always the highest authority.
        ItemOffsetData itemData;
        if (findItemOverride(form, itemData)) {
            const auto& data = itemData;
            rotX = data.rotX; rotY = data.rotY; rotZ = data.rotZ;
            xOff = data.posX; yOff = data.posY; zOff = data.posZ;
            scaleMult = data.scale;
            return ItemTransformSource::ItemOverride;
        }

        const std::string category = resolveOverrideCategory(form);

        if (settings.categoryOverrides.contains(category)) {
            const auto& data = settings.categoryOverrides[category];
            rotX = data.rotX; rotY = data.rotY; rotZ = data.rotZ;
            xOff = data.posX; yOff = data.posY; zOff = data.posZ;
            scaleMult = data.scale;
            return ItemTransformSource::CategoryOverride;
        }

        ItemTransformSource resolvedSource = ItemTransformSource::Default;

        // 1. Specific Item Overrides (by FormID or Name)
        
        // Quivers / Bolts (Checking by FormID or general Ammo type)
        // Note: The user mentioned "aljava" (quiver). In Skyrim, quivers are Ammo.
        if (fType == RE::FormType::Ammo) {
            rotX = 90.0f; rotY = 0.0f; rotZ = 0.0f;
            yOff = -1.0f;
            zOff = -1.0f;
            return ItemTransformSource::TypeFallback;
        }

        // Skooma Variants (Checking by FormID base to handle DLC load order, and Name)
        // IDs: 00057A7A (Skooma), 0003F4BD (Double-Distilled), 00057A7B (Kordir's), xx01391D (Redwater)
        uint32_t baseID = formID & 0x00FFFFFF;
        if (baseID == 0x57A7A || baseID == 0x3F4BD || baseID == 0x57A7B || baseID == 0x1391D ||
            name.find("Skooma") != std::string::npos || name.find("skooma") != std::string::npos) {
            rotX = 0.0f; rotY = 0.0f; rotZ = 90.0f; // Stand up
            scaleMult = 0.5f;
            yOff = -0.75f; // Adjusted from -1.0 to -0.75 (+0.25)
            return ItemTransformSource::TypeFallback;
        }

        // Cicero's Clothes (ID: 0x0006492C and others in the set)
        // 0x6492C is Cicero's Outfit. Usually Cicero's set includes:
        // 0006492C (Body), 0006492A (Boots), 0006492D (Gloves), 0006492E (Hat)
        if (baseID == 0x6492C || baseID == 0x6492A || baseID == 0x6492D || baseID == 0x6492E || 
            name.find("Cicero") != std::string::npos || name.find("cicero") != std::string::npos) {
            scaleMult = 0.5f;
            // Fall through to Armor type logic for rotation
        }

        // 2. Type-based Overrides
        if (fType == RE::FormType::Weapon) {
            resolvedSource = ItemTransformSource::TypeFallback;
            rotX = -90.0f; rotY = -35.0f; rotZ = 35.0f;
            auto* weap = form->As<RE::TESObjectWEAP>();
            if (weap && (weap->IsBow() || weap->IsCrossbow()))
                scaleMult = 0.5f;

        } else if (fType == RE::FormType::AlchemyItem || fType == RE::FormType::Ingredient
                || fType == RE::FormType::SoulGem || fType == RE::FormType::Scroll
                 || fType == RE::FormType::Spell) {
            resolvedSource = ItemTransformSource::TypeFallback;
            
            // 1. Resolve and normalize model path
            std::string modelPath = getModelPath(form);
            std::string modelLower = modelPath;
            std::replace(modelLower.begin(), modelLower.end(), '/', '\\');
            std::transform(modelLower.begin(), modelLower.end(), modelLower.begin(), ::tolower);

            // 2. Combined detection for solid consumables (Ingredients, Flora, Food)
            bool isFood = false;
            if (auto* alch = form->As<RE::AlchemyItem>()) {
                isFood = alch->IsFood();
            }

            bool isFlora = (modelLower.find("plants\\") != std::string::npos || 
                            modelLower.find("flowers\\") != std::string::npos ||
                            modelLower.find("flora\\") != std::string::npos ||
                            modelLower.find("landscape\\trees\\") != std::string::npos ||
                            (modelLower.find("ingredient\\") != std::string::npos && 
                             (modelLower.find("leaf") != std::string::npos || modelLower.find("flower") != std::string::npos)));

            // 3. Apply rotation and offsets
            if (fType == RE::FormType::Ingredient || isFlora || isFood) {
                // All Ingredients, Flora, and Food follow Weapon rotation
                rotX = -90.0f; rotY = -35.0f; rotZ = 35.0f;
                scaleMult = 1.0f;
                yOff = 0.05f;
            } else {
                // Potions and other liquid/misc items in this category
                rotX = 0.0f; rotY = 0.0f; rotZ = 90.0f; // Bottles stand up (Z 90)
                
                if (fType == RE::FormType::AlchemyItem) {
                    scaleMult = 0.75f;
                    yOff = -0.5f; // Only apply this specific offset to real potions
                }
            }

        } else if (fType == RE::FormType::Armor) {
            resolvedSource = ItemTransformSource::TypeFallback;
            auto* armo = form->As<RE::TESObjectARMO>();
            if (armo) {
                yOff = -0.25f; // All armors/equipment moved down slightly
                using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
                if (armo->HasPartOf(Slot::kHands)) {
                    yOff = -0.75f;
                    rotX = 90.0f; rotY = 0.0f; rotZ = 0.0f;
                } else if (armo->HasPartOf(Slot::kHead) || armo->HasPartOf(Slot::kCirclet) || armo->HasPartOf(Slot::kHair)) {
                    rotX = 0.0f; rotY = 0.0f; rotZ = 0.0f;
                } else if (armo->HasPartOf(Slot::kFeet) || armo->HasPartOf(Slot::kCalves)) {
                    rotX = 0.0f; rotY = 0.0f; rotZ = 0.0f;
                } else if (armo->HasPartOf(Slot::kAmulet) || armo->HasPartOf(Slot::kRing)
                        || armo->HasPartOf(Slot::kModFaceJewelry)) {
                    rotX = 90.0f; rotY = 0.0f; rotZ = 0.0f;
                } else {
                    rotX = 90.0f; rotY = 0.0f; rotZ = 0.0f;
                }
            }
        }
        return resolvedSource;
    }
}
