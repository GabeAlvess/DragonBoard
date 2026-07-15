#pragma once
#include <RE/N/NiNode.h>

namespace vrui
{
    class VRUIHandTracking
    {
    public:
        static RE::NiNode* getMenuHandNode(bool isVRIKInstalled);
        static RE::NiNode* getDominantHandNode(bool isVRIKInstalled);
        static RE::NiNode* getNonDominantHandNode(bool isVRIKInstalled);
        static RE::NiNode* getPlayerSkeletonRoot(bool isVRIKInstalled);
        static RE::NiNode* getHeadNode(bool isVRIKInstalled);
    };
}
