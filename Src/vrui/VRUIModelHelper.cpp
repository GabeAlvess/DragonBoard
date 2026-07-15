#include "pch.h"
#include "VRUIModelHelper.h"
#include <algorithm>
#include <functional>
#include <cctype>
#include <RE/N/NiNode.h>
#include <RE/N/NiTransform.h>
#include <RE/B/BSGeometry.h>
#include "VRUIWidget.h"

namespace vrui
{
    void VRUIModelHelper::normalizeAndCenterModel(RE::NiAVObject* a_obj)
    {
        if (!a_obj) return;

        // Bounding box accumulators
        float xMin = 1e6f, yMin = 1e6f, zMin = 1e6f;
        float xMax = -1e6f, yMax = -1e6f, zMax = -1e6f;
        bool found = false;

        // Recursive helper ensuring we find ALL visual geometry
        std::function<void(RE::NiAVObject*, RE::NiTransform)> calcBounds;
        calcBounds = [&](RE::NiAVObject* obj, RE::NiTransform cumulative) {
            if (!obj) return;

            // Check if this object itself has geometry data (or is a geometry)
            if (auto* geom = obj->AsGeometry()) {
                const char* nodeName = obj->name.c_str();
                if (nodeName) {
                    std::string nameLower = nodeName;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) { return std::tolower(c); });
                    
                    // Filter out massive invisible helper meshes
                    if (nameLower.find("marker") != std::string::npos || 
                        nameLower.find("collision") != std::string::npos ||
                        nameLower.find("occlusion") != std::string::npos ||
                        nameLower.find("shadow") != std::string::npos ||
                        nameLower.find("hitbox") != std::string::npos) 
                    {
                        return;
                    }
                }

                auto& modelData = geom->GetModelData();
                auto& bound = modelData.modelBound;
                
                // If radius is zero, this geometry has no volume to measure
                if (bound.radius > 0.001f) {
                    RE::NiPoint3 centerInRoot = cumulative * bound.center;
                    float radiusInRoot = cumulative.scale * bound.radius;

                    xMin = (std::min)(xMin, centerInRoot.x - radiusInRoot);
                    yMin = (std::min)(yMin, centerInRoot.y - radiusInRoot);
                    zMin = (std::min)(zMin, centerInRoot.z - radiusInRoot);
                    xMax = (std::max)(xMax, centerInRoot.x + radiusInRoot);
                    yMax = (std::max)(yMax, centerInRoot.y + radiusInRoot);
                    zMax = (std::max)(zMax, centerInRoot.z + radiusInRoot);
                    found = true;
                }
            }

            // Recurse into children
            if (auto* node = obj->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    if (child) {
                        calcBounds(child.get(), cumulative * child->local);
                    }
                }
            }
        };

        // Perform the scan
        calcBounds(a_obj, RE::NiTransform());

        if (found) {
            float dx = xMax - xMin;
            float dy = yMax - yMin;
            float dz = zMax - zMin;
            
            float maxDim = dx;
            if (dy > maxDim) maxDim = dy;
            if (dz > maxDim) maxDim = dz;
            
            if (maxDim > 0.01f) {
                // Opção 2: Ajuste de escala baseado na densidade/volume (Bounding Box Ratio)
                float volume = dx * dy * dz;
                float maxVolume = maxDim * maxDim * maxDim;
                float ratio = (maxVolume > 0.0001f) ? (volume / maxVolume) : 1.0f;
                
                // Se o arco está gigante e a poção normal, significa que a lógica inversa
                // deve ser aplicada: itens muito finos/compridos (baixo volume relativo) 
                // devem ter um multiplicador MENOR para não explodirem para fora do botão.
                float scaleMultiplier = 0.5f + (ratio * 1.0f); 
                if (scaleMultiplier < 0.4f) scaleMultiplier = 0.4f;   // Limite mínimo para arcos/espadas
                if (scaleMultiplier > 1.8f) scaleMultiplier = 1.8f;   // Limite máximo para poções/comidas

                float normScale = (1.0f / maxDim) * scaleMultiplier;
                
                a_obj->local.scale = normScale;

                // Center pivot at (0,0,0) in parent space
                a_obj->local.translate.x = -((xMin + xMax) * 0.5f) * normScale;
                a_obj->local.translate.y = -((yMin + yMax) * 0.5f) * normScale;
                a_obj->local.translate.z = -((zMin + zMax) * 0.5f) * normScale;
            } else {
                a_obj->local.scale = 1.0f;
            }
        } else {
            // Fail-safe: Se nenhuma geometria for detectada (o que causaria o arco a manter a escala 1.0 do Skyrim,
            // que é gigante), aplicamos uma escala muito menor como prevenção.
            a_obj->local.scale = 0.05f;
        }
    }

    void VRUIModelHelper::applyRotationOverrides(RE::NiAVObject* a_obj, const std::string& nifPath)
    {
        if (!a_obj) return;

        std::string pathString = nifPath;
        for (size_t i = 0; i < pathString.length(); ++i) {
            pathString[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(pathString[i])));
        }

        if (pathString.find("potion") != std::string::npos || 
            pathString.find("alchemy") != std::string::npos || 
            pathString.find("food") != std::string::npos) 
        {
            float rAngleX = 90.0f * kDegToRad; 
            RE::NiMatrix3 rotation{};
            rotation.SetEulerAnglesXYZ(rAngleX, 0.0f, 0.0f);
            a_obj->local.rotate = a_obj->local.rotate * rotation;
        }
    }
}
