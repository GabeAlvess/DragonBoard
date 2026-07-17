#pragma once
#include <string>
#include <limits>
#include <cstdint>
#include <RE/F/FormTypes.h>

namespace RE { class TESForm; }

namespace vrui
{
    struct ItemOffsetData;

    /**
     * @brief Shared item utilities used by all dynamic containers (Inventory, Favorites, etc.).
     */
    namespace ItemUtils
    {
        enum class ItemTransformSource : std::uint8_t
        {
            Default,
            TypeFallback,
            CategoryOverride,
            ItemOverride
        };

        /**
         * @brief Returns the best available 3D model path for a game form (item or spell).
         */
        std::string getModelPath(RE::TESForm* form);
        std::string sanitizeName(const std::string& name);

        /**
         * @brief Fills rotX/Y/Z and scaleMult with the per-type visual overrides for an item/spell.
         * NaN values mean "use INI default".
         */
        ItemTransformSource getItemOverrides(RE::TESForm* form,
                                             float& rotX, float& rotY, float& rotZ,
                                             float& xOff, float& yOff, float& zOff,
                                             float& scaleMult);

        ItemTransformSource getItemTransformSource(RE::TESForm* form);
        bool isExplicitOverride(ItemTransformSource source);
        std::string getStableItemOverrideKey(RE::TESForm* form);
        bool findItemOverride(RE::TESForm* form, ItemOffsetData& data);
        void setItemOverride(RE::TESForm* form, const ItemOffsetData& data);
        void eraseItemOverride(RE::TESForm* form);

        /**
         * @brief Safe spell unequip leveraging the native Papyrus Actor.UnequipSpell method.
         * This natively manages dismissing the casting effects (glow/particles) correctly.
         * source: 0 = Left Hand, 1 = Right Hand, 2 = Voice/Power
         */
        void PapyrusUnequipSpell(RE::Actor* actor, RE::SpellItem* spell, int source);
    }
}
