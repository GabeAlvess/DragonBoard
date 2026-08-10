#include "pch.h"
#include <RE/Skyrim.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiAVObject.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSEffectShaderProperty.h>
#include <RE/B/BSVisit.h>
#include "VRUIMapMarker.h"
#include "VRUISettings.h"
#include "MapCalibration.h"
#include "VRMenuManager.h"
#include "VRUIPanel.h"
#include "ui/rml/RmlPanelHost.h"

#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string_view>

namespace vrui
{
    namespace
    {
        struct QuestObjectivePositionCache
        {
            RE::FormID questFormId = 0;
            RE::FormID targetFormId = 0;
            RE::NiPoint3 worldPosition{};
            bool valid = false;
        };

        std::mutex g_questObjectivePositionMutex;
        std::array<
            QuestObjectivePositionCache,
            VRUIMapMarker::kQuestMarkerSlotCount> g_questObjectivePositions{};

        constexpr float kAtlasSize = 4096.0f;
        constexpr float kRotatedMapSourceWidth = 1536.0f;
        constexpr float kRotatedMapSourceHeight = 2216.0f;
        constexpr float kRotatedMapSourceTop = 1880.0f;
        constexpr float kInverseSqrtTwo = 0.70710678118f;

        RE::NiPoint2 MapArtworkUvToAtlasUv(float mapU, float mapV)
        {
            // The map is stored rotated counter-clockwise in the lower-left
            // portion of DragonBoardMat_Tex.dds. This is the inverse of the
            // crop=1536:2216:0:1880,transpose=2 inspection transform.
            return {
                (1.0f - mapV) * kRotatedMapSourceWidth / kAtlasSize,
                (kRotatedMapSourceTop + mapU * kRotatedMapSourceHeight) / kAtlasSize
            };
        }

        bool ContainsCaseInsensitive(std::string_view value, std::string_view needle)
        {
            std::string lowered(value);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return lowered.find(needle) != std::string::npos;
        }

        float ReadHalf(const std::uint8_t* bytes)
        {
            std::uint16_t value = 0;
            std::memcpy(&value, bytes, sizeof(value));
            return DirectX::PackedVector::XMConvertHalfToFloat(value);
        }

        RE::NiPoint3 ReadPosition(
            const RE::BSGraphics::TriShape& rendererData,
            RE::BSGraphics::VertexDesc vertexDesc,
            std::uint16_t index)
        {
            const auto stride = vertexDesc.GetSize();
            const auto offset = vertexDesc.GetAttributeOffset(
                RE::BSGraphics::Vertex::Attribute::VA_POSITION);
            const auto* bytes = rendererData.rawVertexData +
                static_cast<std::size_t>(index) * stride + offset;
            RE::NiPoint3 position;
            std::memcpy(&position, bytes, sizeof(position));
            return position;
        }

        RE::NiPoint2 ReadTextureUv(
            const RE::BSGraphics::TriShape& rendererData,
            RE::BSGraphics::VertexDesc vertexDesc,
            std::uint16_t index)
        {
            const auto stride = vertexDesc.GetSize();
            const auto offset = vertexDesc.GetAttributeOffset(
                RE::BSGraphics::Vertex::Attribute::VA_TEXCOORD0);
            const auto* bytes = rendererData.rawVertexData +
                static_cast<std::size_t>(index) * stride + offset;
            return { ReadHalf(bytes), ReadHalf(bytes + sizeof(std::uint16_t)) };
        }

        bool CalculateBarycentric(
            const RE::NiPoint2& point,
            const std::array<RE::NiPoint2, 3>& triangle,
            std::array<float, 3>& weights)
        {
            const float denominator =
                (triangle[1].y - triangle[2].y) * (triangle[0].x - triangle[2].x) +
                (triangle[2].x - triangle[1].x) * (triangle[0].y - triangle[2].y);
            if (std::abs(denominator) <= 1.0e-8f) return false;

            weights[0] =
                ((triangle[1].y - triangle[2].y) * (point.x - triangle[2].x) +
                    (triangle[2].x - triangle[1].x) * (point.y - triangle[2].y)) /
                denominator;
            weights[1] =
                ((triangle[2].y - triangle[0].y) * (point.x - triangle[2].x) +
                    (triangle[0].x - triangle[2].x) * (point.y - triangle[2].y)) /
                denominator;
            weights[2] = 1.0f - weights[0] - weights[1];
            constexpr float kUvTolerance = -0.002f;
            return weights[0] >= kUvTolerance && weights[1] >= kUvTolerance &&
                weights[2] >= kUvTolerance;
        }
    }

    VRUIMapMarker::VRUIMapMarker(
        const std::string& nifPath,
        MapMarkerSource source,
        std::size_t questSlot,
        std::string texturePath,
        RE::NiPoint3 fixedWorldPosition,
        std::string galleryPhotoId,
        RE::FormID fixedWorldspaceFormId) :
        VRUIButton("", nifPath, texturePath, 0.5f, 0.5f, true),
        _nifPath(nifPath),
        _texturePath(std::move(texturePath)),
        _source(source),
        _questSlot(std::min(questSlot, kQuestMarkerSlotCount - 1)),
        _fixedWorldPosition(fixedWorldPosition),
        _galleryPhotoId(std::move(galleryPhotoId)),
        _fixedWorldspaceFormId(fixedWorldspaceFormId)
    {
        _name = source == MapMarkerSource::Player ? "MapMarker" :
            (source == MapMarkerSource::QuestObjective ?
                "QuestObjectiveMarker" + std::to_string(questSlot + 1) :
                "GalleryMarker_" + _galleryPhotoId);
        setPointerHitTestEnabled(source == MapMarkerSource::GalleryPhoto);
        if (source == MapMarkerSource::GalleryPhoto) {
            setOnPressHandler([photoId = _galleryPhotoId](VRUIButton*, EquipHand) {
                (void)dragonboard::ui::rml::RmlPanelHost::GetSingleton().OpenGalleryPhoto(photoId);
            });
        }
        initializeVisuals();
    }

    void VRUIMapMarker::SetQuestObjectivePosition(
        std::size_t questSlot,
        RE::FormID questFormId,
        RE::FormID targetFormId,
        const RE::NiPoint3& worldPosition)
    {
        if (questSlot >= kQuestMarkerSlotCount) return;
        std::scoped_lock lock(g_questObjectivePositionMutex);
        g_questObjectivePositions[questSlot] = {
            questFormId,
            targetFormId,
            worldPosition,
            true
        };
    }

    void VRUIMapMarker::ClearQuestObjectivePosition(std::size_t questSlot)
    {
        if (questSlot >= kQuestMarkerSlotCount) return;
        std::scoped_lock lock(g_questObjectivePositionMutex);
        g_questObjectivePositions[questSlot] = {};
    }

    void VRUIMapMarker::ClearAllQuestObjectivePositions()
    {
        std::scoped_lock lock(g_questObjectivePositionMutex);
        g_questObjectivePositions = {};
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
            if (!_texturePath.empty()) {
                auto* rawTextureSet = RE::BSShaderTextureSet::Create();
                RE::NiPointer<RE::BSTextureSet> textureSet(rawTextureSet);
                if (rawTextureSet) {
                    rawTextureSet->SetTexturePath(
                        RE::BSTextureSet::Texture::kDiffuse,
                        _texturePath.c_str());
                }
                RE::BSVisit::TraverseScenegraphGeometries(
                    modelNode.get(),
                    [&](RE::BSGeometry* geometry) {
                        if (!geometry) {
                            return RE::BSVisit::BSVisitControl::kContinue;
                        }
                        auto& runtimeData = geometry->GetGeometryRuntimeData();
                        auto* effect = netimmerse_cast<RE::BSEffectShaderProperty*>(
                            runtimeData
                                .properties[RE::BSGeometry::States::kEffect]
                                .get());
                        if (effect && effect->GetMaterial()) {
                            effect->GetMaterial()->sourceTexturePath = _texturePath;
                        }
                        auto* lighting = geometry->lightingShaderProp_cast();
                        auto* material = lighting ?
                            static_cast<RE::BSLightingShaderMaterialBase*>(
                                lighting->GetBaseMaterial()) : nullptr;
                        if (material && textureSet) {
                            material->SetTextureSet(textureSet);
                            lighting->InvalidateTextures(0);
                            lighting->DoClearRenderPasses();
                        }
                        return RE::BSVisit::BSVisitControl::kContinue;
                    });
            }
            _node->AttachChild(modelNode.get(), true);
            if (_source == MapMarkerSource::GalleryPhoto) {
                _galleryHitVisualNode = modelNode;
            }
        } else {
            // Fallback: create a small colored quad if NIF fails
            auto fallback = createQuadNode("MapMarkerFallback", 0.3f, 0.3f, { 1.0f, 0.0f, 0.0f, 1.0f });
            _node->AttachChild(fallback.get(), true);
        }

    }

    bool VRUIMapMarker::hitTest(
        const RE::NiPoint3& rayOriginWorld,
        const RE::NiPoint3& rayDirWorld,
        float& outDistance) const
    {
        if (_source != MapMarkerSource::GalleryPhoto || !_galleryHitVisualNode) {
            return VRUIButton::hitTest(rayOriginWorld, rayDirWorld, outDistance);
        }
        if (!_node || !_pointerHitTestEnabled || !isVisible() ||
            !_galleryHitSurfaceValid) {
            return false;
        }

        const auto& bound = _galleryHitVisualNode->worldBound;
        if (!std::isfinite(bound.radius) || bound.radius <= 0.001f) return false;

        const auto dot = [](const RE::NiPoint3& left, const RE::NiPoint3& right) {
            return left.x * right.x + left.y * right.y + left.z * right.z;
        };

        const float denominator = dot(rayDirWorld, _galleryHitSurfaceNormal);
        if (std::abs(denominator) <= 1.0e-6f) return false;
        const float distance = dot(_galleryHitSurfaceCenter - rayOriginWorld, _galleryHitSurfaceNormal) / denominator;
        if (distance < 0.0f) return false;

        const RE::NiPoint3 hitOffset =
            rayOriginWorld + rayDirWorld * distance - _galleryHitSurfaceCenter;
        const float halfExtent = bound.radius * kInverseSqrtTwo;
        if (std::abs(dot(hitOffset, _galleryHitSurfaceAxisX)) > halfExtent ||
            std::abs(dot(hitOffset, _galleryHitSurfaceAxisY)) > halfExtent) {
            return false;
        }

        outDistance = distance;
        return true;
    }

    void VRUIMapMarker::update(float deltaTime)
    {
        VRUIWidget::update(deltaTime);

        auto& settings = VRUISettings::get();
        const bool questMarker = _source == MapMarkerSource::QuestObjective;
        const bool galleryMarker = _source == MapMarkerSource::GalleryPhoto;
        if (galleryMarker) _galleryHitSurfaceValid = false;
        if ((!questMarker && !galleryMarker && !settings.bEnableMapMarker) ||
            (questMarker && !settings.bEnableQuestMarker)) {
            setVisible(false);
            return;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        RE::NiPoint3 worldPos{};
        if (questMarker) {
            std::scoped_lock lock(g_questObjectivePositionMutex);
            const auto& cached = g_questObjectivePositions[_questSlot];
            if (!cached.valid) {
                setVisible(false);
                return;
            }
            worldPos = cached.worldPosition;
        } else if (galleryMarker) {
            auto* currentCell = player->GetParentCell();
            const bool currentCellIsInterior =
                currentCell && currentCell->IsInteriorCell();
            auto* currentWorldspace = currentCell ?
                currentCell->GetRuntimeData().worldSpace : nullptr;
            if (!currentCellIsInterior &&
                (!currentWorldspace ||
                 currentWorldspace->GetFormID() != _fixedWorldspaceFormId)) {
                setVisible(false);
                return;
            }
            worldPos = _fixedWorldPosition;
        } else {
            worldPos = player->GetPosition();
            RE::TESObjectCELL* currentCell = player->parentCell;
            const bool isInterior = currentCell &&
                currentCell->cellFlags.any(RE::TESObjectCELL::Flag::kIsInteriorCell);
            if (isInterior) {
                setVisible(false);
                return;
            }
        }

        float mapU = 0.0f;
        float mapV = 0.0f;
        if (!MapWorldToTextureUv(
                settings.mapCalibrationPoints, worldPos.x, worldPos.y, mapU, mapV) ||
            mapU < -0.1f || mapU > 1.1f || mapV < -0.1f || mapV > 1.1f) {
            setVisible(false);
            return;
        }

        auto& manager = VRMenuManager::get();
        auto surfacePanel = manager.findPanelByName("Background_Panel");
        auto* panelNode = surfacePanel ? surfacePanel->getNode() : nullptr;
        auto* backgroundNode = manager.isPhysicalBoardActive() ?
            manager.getPhysicalBoardAnchorNode() :
            (surfacePanel ? surfacePanel->getBackgroundNode() : nullptr);
        if (!panelNode || !backgroundNode) {
            setVisible(false);
            return;
        }
        if (_cachedBackgroundNode != backgroundNode && !rebuildMapSurfaceCache(backgroundNode)) {
            setVisible(false);
            return;
        }

        RE::NiPoint3 surfacePosition;
        RE::NiPoint3 surfaceNormal;
        if (!textureUvToPanelLocal(
                panelNode,
                std::clamp(mapU, 0.0f, 1.0f),
                std::clamp(mapV, 0.0f, 1.0f),
                surfacePosition,
                surfaceNormal)) {
            setVisible(false);
            if (!_surfaceFailureLogged) {
                const auto atlasUv = MapArtworkUvToAtlasUv(mapU, mapV);
                logger::warn(
                    "DragonBoardVR: {} calibrated map UV ({:.4f}, {:.4f}) / atlas UV ({:.4f}, {:.4f}) is not covered by the map mesh UVs.",
                    galleryMarker ? "gallery photo" : (questMarker ? "quest objective" : "player"),
                    mapU, mapV, atlasUv.x, atlasUv.y);
                _surfaceFailureLogged = true;
            }
            return;
        }

        if (galleryMarker) {
            RE::NiPoint3 pointerPlanePosition = surfacePosition;
            pointerPlanePosition.y = 0.0f;
            _galleryHitSurfaceCenter = panelNode->world * pointerPlanePosition;
            const auto& panelRotation = panelNode->world.rotate;
            _galleryHitSurfaceAxisX = RE::NiPoint3(
                panelRotation.entry[0][0],
                panelRotation.entry[1][0],
                panelRotation.entry[2][0]);
            _galleryHitSurfaceNormal = RE::NiPoint3(
                panelRotation.entry[0][1],
                panelRotation.entry[1][1],
                panelRotation.entry[2][1]);
            _galleryHitSurfaceAxisY = RE::NiPoint3(
                panelRotation.entry[0][2],
                panelRotation.entry[1][2],
                panelRotation.entry[2][2]);
            _galleryHitSurfaceValid = true;
        }

        _surfaceFailureLogged = false;
        if (!_surfacePlacementLogged) {
            const auto atlasUv = MapArtworkUvToAtlasUv(mapU, mapV);
            logger::trace(
                "DragonBoardVR: {} map marker placement resolved: mapUV=({:.4f}, {:.4f}) atlasUV=({:.4f}, {:.4f}) panel=({:.3f}, {:.3f}, {:.3f}) normal=({:.3f}, {:.3f}, {:.3f}).",
                galleryMarker ? "gallery photo" : (questMarker ? "quest objective" : "player"),
                mapU, mapV, atlasUv.x, atlasUv.y,
                surfacePosition.x, surfacePosition.y, surfacePosition.z,
                surfaceNormal.x, surfaceNormal.y, surfaceNormal.z);
            _surfacePlacementLogged = true;
        }
        setVisible(true);
        setLocalPosition(surfacePosition + surfaceNormal * (galleryMarker ? 0.12f : (questMarker ? 0.10f : 0.08f)));
        setLocalScale(galleryMarker ? settings.galleryCameraMarkerScale :
            (questMarker ? settings.questMarkerScale : settings.mapMarkerScale));

        // Apply rotation
        RE::NiMatrix3 meshRot;
        meshRot.SetEulerAnglesXYZ(
            (galleryMarker ? settings.galleryCameraMarkerRotX : (questMarker ? settings.questMarkerRotX : settings.mapMarkerRotX)) * kDegToRad,
            (galleryMarker ? settings.galleryCameraMarkerRotY : (questMarker ? settings.questMarkerRotY : settings.mapMarkerRotY)) * kDegToRad,
            (galleryMarker ? settings.galleryCameraMarkerRotZ : (questMarker ? settings.questMarkerRotZ : settings.mapMarkerRotZ)) * kDegToRad
        );

        if (!questMarker && !galleryMarker && settings.bMapMarkerDynamicRotation) {
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

    bool VRUIMapMarker::rebuildMapSurfaceCache(RE::NiNode* backgroundNode)
    {
        _cachedBackgroundNode = backgroundNode;
        _mapSurfaceTriangles.clear();
        _surfaceFailureLogged = false;
        _surfacePlacementLogged = false;
        if (!backgroundNode) return false;

        std::size_t matchedGeometryCount = 0;
        RE::BSVisit::TraverseScenegraphGeometries(
            backgroundNode,
            [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
                auto* triShape = geometry ? geometry->AsTriShape() : nullptr;
                auto* property = geometry ? geometry->lightingShaderProp_cast() : nullptr;
                auto* material = property ? static_cast<RE::BSLightingShaderMaterialBase*>(
                    property->GetBaseMaterial()) : nullptr;
                const std::string_view textureName =
                    material && material->diffuseTexture && material->diffuseTexture->name.c_str() ?
                    material->diffuseTexture->name.c_str() : "";
                if (!triShape || !material ||
                    !ContainsCaseInsensitive(textureName, "dragonboardmat_tex")) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                ++matchedGeometryCount;
                const auto& geometryData = triShape->GetGeometryRuntimeData();
                const auto& shapeData = triShape->GetTrishapeRuntimeData();
                auto* rendererData = geometryData.rendererData;
                auto vertexDesc = geometryData.vertexDesc;
                if (!rendererData || !rendererData->rawVertexData || !rendererData->rawIndexData ||
                    !vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) ||
                    !vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_UV)) {
                    logger::warn(
                        "DragonBoardVR: map geometry '{}' has no retained CPU vertex/index/UV data.",
                        geometry->name.c_str());
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                for (std::uint16_t triangleIndex = 0;
                     triangleIndex < shapeData.triangleCount;
                     ++triangleIndex) {
                    MapSurfaceTriangle triangle;
                    triangle.geometry = triShape;
                    bool valid = true;
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const auto index = rendererData->rawIndexData[
                            static_cast<std::size_t>(triangleIndex) * 3 + corner];
                        if (index >= shapeData.vertexCount) {
                            valid = false;
                            break;
                        }
                        triangle.indices[corner] = index;
                        auto uv = ReadTextureUv(*rendererData, vertexDesc, index);
                        uv.x = uv.x * material->texCoordScale[0].x + material->texCoordOffset[0].x;
                        uv.y = uv.y * material->texCoordScale[0].y + material->texCoordOffset[0].y;
                        triangle.textureUv[corner] = uv;
                    }
                    if (valid) _mapSurfaceTriangles.push_back(triangle);
                }

                logger::trace(
                    "DragonBoardVR: indexed map surface geometry '{}' texture='{}' vertices={} triangles={}.",
                    geometry->name.c_str(), textureName, shapeData.vertexCount,
                    shapeData.triangleCount);
                return RE::BSVisit::BSVisitControl::kContinue;
            });

        if (_mapSurfaceTriangles.empty()) {
            logger::warn(
                "DragonBoardVR: no usable UV triangles found for DragonBoardMat_Tex.dds (matched geometries={}).",
                matchedGeometryCount);
            return false;
        }

        logger::trace(
            "DragonBoardVR: texture-UV map marker ready with {} cached surface triangles.",
            _mapSurfaceTriangles.size());
        return true;
    }

    bool VRUIMapMarker::textureUvToPanelLocal(
        RE::NiNode* panelNode,
        float mapU,
        float mapV,
        RE::NiPoint3& position,
        RE::NiPoint3& normal)
    {
        if (!panelNode) return false;
        const auto targetUv = MapArtworkUvToAtlasUv(mapU, mapV);
        for (const auto& triangle : _mapSurfaceTriangles) {
            if (!triangle.geometry) continue;
            std::array<float, 3> weights{};
            if (!CalculateBarycentric(targetUv, triangle.textureUv, weights)) continue;

            const auto& geometryData = triangle.geometry->GetGeometryRuntimeData();
            auto* rendererData = geometryData.rendererData;
            if (!rendererData || !rendererData->rawVertexData) continue;
            auto vertexDesc = geometryData.vertexDesc;

            std::array<RE::NiPoint3, 3> panelVertices{};
            const auto worldToPanel = panelNode->world.Invert();
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto geometryLocal = ReadPosition(
                    *rendererData, vertexDesc, triangle.indices[corner]);
                panelVertices[corner] = worldToPanel *
                    (triangle.geometry->world * geometryLocal);
            }

            position = panelVertices[0] * weights[0] +
                panelVertices[1] * weights[1] + panelVertices[2] * weights[2];
            const auto edgeA = panelVertices[1] - panelVertices[0];
            const auto edgeB = panelVertices[2] - panelVertices[0];
            normal = {
                edgeA.y * edgeB.z - edgeA.z * edgeB.y,
                edgeA.z * edgeB.x - edgeA.x * edgeB.z,
                edgeA.x * edgeB.y - edgeA.y * edgeB.x
            };
            const float length = normal.Length();
            if (length <= 1.0e-5f) return false;
            normal = normal / length;
            // The sampled parchment lies near local Y=-0.812, while the
            // established visible marker plane is near Y=-0.750. Therefore
            // the viewer-facing side of the DragonBoard is positive local Y.
            if (normal.y < 0.0f) normal = normal * -1.0f;
            return true;
        }
        return false;
    }
}
