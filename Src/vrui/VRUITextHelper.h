#pragma once
#include <vector>
#include <string>
#include <RE/N/NiNode.h>
#include <RE/N/NiSmartPointer.h>

namespace vrui
{
    class VRUITextHelper
    {
    public:
        static std::vector<std::string> wrapText(const std::string& text, int maxChars = 12);
        static float buildTextLine(RE::NiNode* parentNode, const std::string& line,
                                   float charScale, float spacing, float lineY,
                                   std::vector<RE::NiPointer<RE::NiAVObject>>& outNodes,
                                   size_t& poolIndex);
    };
}
