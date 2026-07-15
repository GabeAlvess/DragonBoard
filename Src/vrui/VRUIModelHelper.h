#pragma once
#include <string>
#include <RE/N/NiAVObject.h>

namespace vrui
{
    class VRUIModelHelper
    {
    public:
        static void normalizeAndCenterModel(RE::NiAVObject* a_obj);
        static void applyRotationOverrides(RE::NiAVObject* a_obj, const std::string& nifPath);
    };
}
