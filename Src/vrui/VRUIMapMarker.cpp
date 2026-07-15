#include "pch.h"
#include <RE/Skyrim.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiAVObject.h>
#include "VRUIMapMarker.h"
#include "VRUISettings.h"

namespace vrui
{
    VRUIMapMarker::VRUIMapMarker(const std::string& nifPath) :
        VRUIWidget("MapMarker", 0.5f, 0.5f), _nifPath(nifPath)
    {
        initializeVisuals();
    }

    void VRUIMapMarker::initializeVisuals()
    {
        if (!_node) return;

        RE::NiPointer<RE::NiNode> modelNode;
        if (!_nifPath.empty()) {
            modelNode = loadModelFromNif(_nifPath);
        }

        if (modelNode) {
            sanitizeModel(modelNode.get());
            _node->AttachChild(modelNode.get(), true);
        } else {
            // Fallback: create a small colored quad if NIF fails
            auto fallback = createQuadNode("MapMarkerFallback", 0.3f, 0.3f, { 1.0f, 0.0f, 0.0f, 1.0f });
            _node->AttachChild(fallback.get(), true);
        }
    }

    void VRUIMapMarker::update(float deltaTime)
    {
        VRUIWidget::update(deltaTime);

        auto& settings = VRUISettings::get();
        if (!settings.bEnableMapMarker) {
            setVisible(false);
            return;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        RE::NiPoint3 worldPos = player->GetPosition();

        // Use member access and basic virtuals for stability
        RE::TESObjectCELL* currentCell = player->parentCell;
        
        bool isInterior = false;
        if (currentCell) {
             // Use any() for const-correct flag checking
             isInterior = currentCell->cellFlags.any(RE::TESObjectCELL::Flag::kIsInteriorCell);
        }

        if (isInterior) {
            setVisible(false);
            return;
        }

        setVisible(true);

        // Map world coordinates to tablet surface coordinates (0.0 to 1.0)
        float tX = (worldPos.x - settings.mapWorldMinX) / (settings.mapWorldMaxX - settings.mapWorldMinX);
        float tY = (worldPos.y - settings.mapWorldMinY) / (settings.mapWorldMaxY - settings.mapWorldMinY);

        // Convert to local widget position on the tablet
        float localX = (tX - 0.5f) * settings.mapWidth + settings.mapMarkerOffsetX;
        float localY = (tY - 0.5f) * settings.mapHeight + settings.mapMarkerOffsetY;

        setLocalPosition({ localX, settings.mapMarkerOffsetZ, localY });
        setLocalScale(settings.mapMarkerScale);

        // Apply rotation
        RE::NiMatrix3 meshRot;
        meshRot.SetEulerAnglesXYZ(
            settings.mapMarkerRotX * kDegToRad,
            settings.mapMarkerRotY * kDegToRad,
            settings.mapMarkerRotZ * kDegToRad
        );

        if (settings.bMapMarkerDynamicRotation) {
            // The player's heading (yaw) is around the Z axis in world space.
            // The tablet's surface normal is the Y axis (local space X-Z plane).
            float heading = player->GetAngle().z + (settings.mapMarkerRotOffset * kDegToRad);
            
            RE::NiMatrix3 dynRot;
            dynRot.SetEulerAnglesXYZ(0.0f, heading, 0.0f); // Removida a inversão de sinal (de -heading para heading)

            // Multiply: dynRot * meshRot applies mesh orientation first, then spins it around the tablet's Y axis
            setLocalRotation(dynRot * meshRot);
        } else {
            setLocalRotation(meshRot);
        }
        
        RE::NiUpdateData updateData;
        _node->Update(updateData);
    }
}
