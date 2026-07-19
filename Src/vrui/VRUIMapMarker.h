#pragma once
#include "VRUIWidget.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiAVObject.h>
#include <memory>
#include <array>
#include <string>
#include <vector>

namespace RE
{
    class BSTriShape;
}

namespace vrui
{
    enum class MapMarkerSource
    {
        Player,
        QuestObjective
    };

    /**
     * @brief A widget that displays the player's real-time position on the tablet map.
     */
    class VRUIMapMarker : public VRUIWidget
    {
    public:
        explicit VRUIMapMarker(
            const std::string& nifPath,
            MapMarkerSource source = MapMarkerSource::Player);

        static void SetQuestObjectivePosition(
            RE::FormID questFormId,
            RE::FormID targetFormId,
            const RE::NiPoint3& worldPosition);
        static void ClearQuestObjectivePosition();

        // VRUIWidget interface
        void initializeVisuals() override;
        void update(float deltaTime) override;

    private:
        struct MapSurfaceTriangle
        {
            RE::BSTriShape* geometry = nullptr;
            std::array<std::uint16_t, 3> indices{};
            std::array<RE::NiPoint2, 3> textureUv{};
        };

        bool rebuildMapSurfaceCache(RE::NiNode* backgroundNode);
        bool textureUvToPanelLocal(
            RE::NiNode* panelNode,
            float mapU,
            float mapV,
            RE::NiPoint3& position,
            RE::NiPoint3& normal);

        std::string _nifPath;
        MapMarkerSource _source = MapMarkerSource::Player;
        RE::NiNode* _cachedBackgroundNode = nullptr;
        std::vector<MapSurfaceTriangle> _mapSurfaceTriangles;
        bool _surfaceFailureLogged = false;
        bool _surfacePlacementLogged = false;
    };
}
