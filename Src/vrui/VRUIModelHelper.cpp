#include "pch.h"
#include "VRUIModelHelper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <REL/Relocation.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSVisit.h>
#include <RE/N/NiExtraData.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiTransform.h>

namespace vrui
{
    namespace
    {
        struct Bounds
        {
            float xMin = 1.0e6f;
            float yMin = 1.0e6f;
            float zMin = 1.0e6f;
            float xMax = -1.0e6f;
            float yMax = -1.0e6f;
            float zMax = -1.0e6f;
            bool found = false;
        };

        // The CommonLib version used by this checkout exposes the VR RTTI but
        // not the BSInvMarker class header. This read-only view matches the
        // Skyrim VR runtime layout used by newer CommonLibSSE-NG versions.
        struct BSInvMarkerView
        {
            std::byte base[0x18];
            float zoom;
            std::uint16_t rotationX;
            std::uint16_t rotationY;
            std::uint16_t rotationZ;
            std::byte padding[6];
        };
        static_assert(sizeof(BSInvMarkerView) == 0x28);

        constexpr float kFixedInvMarkerToRadians =
            2.0f * 3.14159265358979323846f / 6553.6f;
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        bool isHelperGeometry(const RE::NiAVObject* object)
        {
            if (!object) return true;
            std::string lower = object->name.c_str();
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lower.find("marker") != std::string::npos ||
                   lower.find("collision") != std::string::npos ||
                   lower.find("occlusion") != std::string::npos ||
                   lower.find("shadow") != std::string::npos ||
                   lower.find("hitbox") != std::string::npos;
        }

        Bounds collectVisibleBounds(
            RE::NiAVObject* root,
            const RE::NiMatrix3* presentationRotation)
        {
            Bounds bounds;
            std::function<void(RE::NiAVObject*, const RE::NiTransform&)> visit;
            visit = [&](RE::NiAVObject* object, const RE::NiTransform& cumulative) {
                if (!object || object->GetAppCulled()) return;

                if (auto* geometry = object->AsGeometry(); geometry && !isHelperGeometry(object)) {
                    const auto& modelBound = geometry->GetModelData().modelBound;
                    if (modelBound.radius > 0.001f) {
                        RE::NiPoint3 center = cumulative * modelBound.center;
                        if (presentationRotation) center = *presentationRotation * center;
                        const float radius = std::abs(cumulative.scale) * modelBound.radius;

                        bounds.xMin = (std::min)(bounds.xMin, center.x - radius);
                        bounds.yMin = (std::min)(bounds.yMin, center.y - radius);
                        bounds.zMin = (std::min)(bounds.zMin, center.z - radius);
                        bounds.xMax = (std::max)(bounds.xMax, center.x + radius);
                        bounds.yMax = (std::max)(bounds.yMax, center.y + radius);
                        bounds.zMax = (std::max)(bounds.zMax, center.z + radius);
                        bounds.found = true;
                    }
                }

                if (auto* node = object->AsNode()) {
                    for (auto& child : node->GetChildren()) {
                        if (child) visit(child.get(), cumulative * child->local);
                    }
                }
            };

            visit(root, RE::NiTransform{});
            return bounds;
        }

        RE::NiPoint3 boundsCenter(const Bounds& bounds)
        {
            return {
                (bounds.xMin + bounds.xMax) * 0.5f,
                (bounds.yMin + bounds.yMax) * 0.5f,
                (bounds.zMin + bounds.zMax) * 0.5f
            };
        }
    }

    void VRUIModelHelper::normalizeAndCenterModel(RE::NiAVObject* object)
    {
        if (!object) return;

        const Bounds bounds = collectVisibleBounds(object, nullptr);
        if (!bounds.found) {
            object->local.scale = 0.05f;
            return;
        }

        const float dx = bounds.xMax - bounds.xMin;
        const float dy = bounds.yMax - bounds.yMin;
        const float dz = bounds.zMax - bounds.zMin;
        const float maxDimension = (std::max)({ dx, dy, dz });
        if (maxDimension <= 0.01f) {
            object->local.scale = 1.0f;
            return;
        }

        // Keep the legacy density adjustment for DragonBoard-owned UI meshes.
        // World items use normalizeAndCenterWorldModel() below instead.
        const float volume = dx * dy * dz;
        const float maxVolume = maxDimension * maxDimension * maxDimension;
        const float ratio = maxVolume > 0.0001f ? volume / maxVolume : 1.0f;
        const float scaleMultiplier = std::clamp(0.5f + ratio, 0.4f, 1.8f);
        const float normalizedScale = scaleMultiplier / maxDimension;
        const RE::NiPoint3 center = boundsCenter(bounds);

        object->local.scale = normalizedScale;
        object->local.translate = center * -normalizedScale;
    }

    bool VRUIModelHelper::normalizeAndCenterWorldModel(
        RE::NiAVObject* object,
        const RE::NiMatrix3& presentationRotation)
    {
        if (!object) return false;

        // Measure after the chosen presentation rotation. X and Z are the
        // DragonBoard screen axes; Y is depth and must not make a thin item
        // appear smaller merely because its authoring pivot is unusual.
        const Bounds bounds = collectVisibleBounds(object, &presentationRotation);
        if (!bounds.found) {
            object->local.scale = 0.05f;
            return false;
        }

        const float projectedWidth = bounds.xMax - bounds.xMin;
        const float projectedHeight = bounds.zMax - bounds.zMin;
        const float projectedMax = (std::max)(projectedWidth, projectedHeight);
        if (projectedMax <= 0.01f) {
            object->local.scale = 1.0f;
            return false;
        }

        const float normalizedScale = 1.0f / projectedMax;
        const RE::NiPoint3 orientedCenter = boundsCenter(bounds);
        const RE::NiPoint3 modelCenter = presentationRotation.Transpose() * orientedCenter;
        object->local.scale = normalizedScale;
        object->local.translate = modelCenter * -normalizedScale;

        logger::trace(
            "DragonBoardVR: normalized visible item projectedSize=({:.3f}, {:.3f}) "
            "depth={:.3f} center=({:.3f}, {:.3f}, {:.3f}) scale={:.6f}",
            projectedWidth, projectedHeight, bounds.yMax - bounds.yMin,
            orientedCenter.x, orientedCenter.y, orientedCenter.z, normalizedScale);
        return true;
    }

    bool VRUIModelHelper::getInventoryMarkerTransform(
        RE::NiAVObject* object,
        RE::NiMatrix3& rotation,
        float& zoom)
    {
        if (!object) return false;

        const REL::Relocation<const RE::NiRTTI*> markerRtti{ RE::NiRTTI_BSInvMarker };
        bool found = false;
        RE::BSVisit::TraverseScenegraphObjects(
            object,
            [&](RE::NiAVObject* current) -> RE::BSVisit::BSVisitControl {
                if (!current) return RE::BSVisit::BSVisitControl::kContinue;
                const auto extraCount = current->GetExtraDataSize();
                for (std::uint16_t i = 0; i < extraCount; ++i) {
                    auto* extra = current->GetExtraDataAt(i);
                    if (!extra || extra->GetRTTI() != markerRtti.get()) continue;

                    const auto* marker = reinterpret_cast<const BSInvMarkerView*>(extra);
                    zoom = marker->zoom;
                    rotation.SetEulerAnglesXYZ(
                        static_cast<float>(marker->rotationX) * kFixedInvMarkerToRadians,
                        static_cast<float>(marker->rotationY) * kFixedInvMarkerToRadians,
                        static_cast<float>(marker->rotationZ) * kFixedInvMarkerToRadians);
                    found = true;
                    return RE::BSVisit::BSVisitControl::kStop;
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        return found;
    }

    void VRUIModelHelper::applyRotationOverrides(RE::NiAVObject* object, const std::string& nifPath)
    {
        if (!object) return;

        std::string lower = nifPath;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.find("potion") != std::string::npos ||
            lower.find("alchemy") != std::string::npos ||
            lower.find("food") != std::string::npos) {
            RE::NiMatrix3 rotation{};
            rotation.SetEulerAnglesXYZ(90.0f * kDegToRad, 0.0f, 0.0f);
            object->local.rotate = object->local.rotate * rotation;
        }
    }
}
