#pragma once
#include <string>
#include <RE/N/NiAVObject.h>
#include <RE/N/NiMatrix3.h>

namespace vrui
{
    class VRUIModelHelper
    {
    public:
        static void normalizeAndCenterModel(RE::NiAVObject* a_obj);
        static bool normalizeAndCenterWorldModel(
            RE::NiAVObject* a_obj,
            const RE::NiMatrix3& presentationRotation);
        static bool getInventoryMarkerTransform(
            RE::NiAVObject* a_obj,
            RE::NiMatrix3& rotation,
            float& zoom);
        static void applyRotationOverrides(RE::NiAVObject* a_obj, const std::string& nifPath);
    };
}
