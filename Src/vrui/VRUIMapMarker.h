#pragma once
#include "VRUIWidget.h"
#include <RE/P/PlayerCharacter.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiAVObject.h>
#include <memory>
#include <array>
#include <cstddef>
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
        static constexpr std::size_t kQuestMarkerSlotCount = 3;

        explicit VRUIMapMarker(
            const std::string& nifPath,
            MapMarkerSource source = MapMarkerSource::Player,
            std::size_t questSlot = 0,
            std::string texturePath = {});

        static void SetQuestObjectivePosition(
            std::size_t questSlot,
            RE::FormID questFormId,
            RE::FormID targetFormId,
            const RE::NiPoint3& worldPosition);
        static void ClearQuestObjectivePosition(std::size_t questSlot);
        static void ClearAllQuestObjectivePositions();

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
        std::string _texturePath;
        MapMarkerSource _source = MapMarkerSource::Player;
        std::size_t _questSlot = 0;
        RE::NiNode* _cachedBackgroundNode = nullptr;
        std::vector<MapSurfaceTriangle> _mapSurfaceTriangles;
        bool _surfaceFailureLogged = false;
        bool _surfacePlacementLogged = false;
    };
}
