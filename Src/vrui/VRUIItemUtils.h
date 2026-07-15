#pragma once
#include <string>
#include <limits>
#include <RE/F/FormTypes.h>

namespace RE { class TESForm; }

namespace vrui
{
    /**
     * @brief Shared item utilities used by all dynamic containers (Inventory, Favorites, etc.).
     */
    namespace ItemUtils
    {
        /**
         * @brief Returns the best available 3D model path for a game form (item or spell).
         */
        std::string getModelPath(RE::TESForm* form);
        std::string sanitizeName(const std::string& name);

        /**
         * @brief Fills rotX/Y/Z and scaleMult with the per-type visual overrides for an item/spell.
         * NaN values mean "use INI default".
         */
        void getItemOverrides(RE::TESForm* form,
                              float& rotX, float& rotY, float& rotZ,
                              float& xOff, float& yOff, float& zOff,
                              float& scaleMult);

        /**
         * @brief Safe spell unequip leveraging the native Papyrus Actor.UnequipSpell method.
         * This natively manages dismissing the casting effects (glow/particles) correctly.
         * source: 0 = Left Hand, 1 = Right Hand, 2 = Voice/Power
         */
        void PapyrusUnequipSpell(RE::Actor* actor, RE::SpellItem* spell, int source);
    }
}
