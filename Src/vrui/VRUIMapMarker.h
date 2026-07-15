#pragma once
#include "VRUIWidget.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiAVObject.h>
#include <memory>
#include <string>

namespace vrui
{
    /**
     * @brief A widget that displays the player's real-time position on the tablet map.
     */
    class VRUIMapMarker : public VRUIWidget
    {
    public:
        explicit VRUIMapMarker(const std::string& nifPath);

        // VRUIWidget interface
        void initializeVisuals() override;
        void update(float deltaTime) override;

    private:
        std::string _nifPath;
    };
}
