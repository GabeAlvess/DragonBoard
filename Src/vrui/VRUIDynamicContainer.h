#pragma once

#include "VRUIContainer.h"

namespace vrui
{
    /**
     * @brief Base class for containers that populate their content dynamically from game data.
     */
    class VRUIDynamicContainer : public VRUIContainer
    {
    public:
        explicit VRUIDynamicContainer(const std::string& name, 
                                    ContainerLayout layout = ContainerLayout::Grid,
                                    float spacing = 3.6f, 
                                    float scale = 1.0f);

        /**
         * @brief Clears current elements and pulls fresh data from the game engine.
         * Derived classes must implement this to populate with Inventory, Spells, etc.
         */
        virtual void refresh() = 0;

        /**
         * @brief Schedule a refresh after a short delay instead of immediately.
         * Avoids frame hitches from synchronous bulk NIF loading on panel open.
         */
        void scheduleRefresh(float delaySeconds = 0.5f);

        /**
         * @brief Updates only the equipped indicator (setEquipped) on existing buttons
         * without doing a full refresh. Much cheaper: only toggles NIF visibility.
         * Derived classes override this to check current game equip state per-item.
         */
        virtual void updateEquippedStates() {}
        virtual void invalidateRefreshCache() {}

        void update(float deltaTime) override;

    protected:
        float _pendingRefreshTimer = -1.0f; // -1 = no pending refresh
    };
}
