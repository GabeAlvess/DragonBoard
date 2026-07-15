#include "pch.h"
#include "VRUIHandTracking.h"
#include "VRUISettings.h"
#include <RE/P/PlayerCharacter.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace vrui
{
    namespace
    {
        RE::NiNode* resolveHandAnchorNode(RE::NiNode* root, const char* magicNodeName, const char* wandNodeName, const char* handBoneName)
        {
            if (!root) {
                return nullptr;
            }

            if (auto* obj = root->GetObjectByName(magicNodeName)) {
                if (auto* node = obj->AsNode()) {
                    return node;
                }
            }

            if (auto* obj = root->GetObjectByName(wandNodeName)) {
                if (auto* node = obj->AsNode()) {
                    return node;
                }
            }

            if (auto* obj = root->GetObjectByName(handBoneName)) {
                return obj->AsNode();
            }

            return nullptr;
        }

        std::string toLowerCopy(std::string_view value)
        {
            std::string lowered(value);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return lowered;
        }

        bool containsHmdMarker(std::string_view name)
        {
            const std::string lowered = toLowerCopy(name);
            return lowered.find("hmd") != std::string::npos || lowered.find("hdm") != std::string::npos;
        }

        int scoreHmdNodeName(std::string_view name)
        {
            const std::string lowered = toLowerCopy(name);
            if (lowered == "hmd") {
                return 0;
            }
            if (lowered == "hmd node" || lowered == "hdm node" || lowered == "hmdnode" || lowered == "hdmnode") {
                return 1;
            }
            if (lowered.find("camera") != std::string::npos) {
                return 2;
            }
            if (lowered.find("hmd") != std::string::npos || lowered.find("hdm") != std::string::npos) {
                return 3;
            }
            return 100;
        }

        void collectHmdNodes(RE::NiNode* node, std::vector<RE::NiNode*>& outMatches)
        {
            if (!node) {
                return;
            }

            if (containsHmdMarker(node->name.c_str())) {
                outMatches.push_back(node);
            }

            for (auto& child : node->GetChildren()) {
                auto* childNode = child ? child->AsNode() : nullptr;
                if (childNode) {
                    collectHmdNodes(childNode, outMatches);
                }
            }
        }

        RE::NiNode* resolveHmdNode(RE::NiNode* root)
        {
            if (!root) {
                return nullptr;
            }

            auto logResolvedNode = [root](const RE::NiNode* chosenNode, std::string_view candidates) {
                static std::string lastLoggedRootName;
                static std::string lastLoggedChoice;

                const std::string rootName = root->name.c_str();
                const std::string chosenName = chosenNode ? chosenNode->name.c_str() : "";
                if (rootName != lastLoggedRootName || chosenName != lastLoggedChoice) {
                    logger::info("DragonBoardVR: Resolved HMD node '{}' from candidates [{}]", chosenName, candidates);
                    lastLoggedRootName = rootName;
                    lastLoggedChoice = chosenName;
                }
            };

            static constexpr std::array<const char*, 8> preferredNames{
                "HMD",
                "HDM",
                "HMD Node",
                "HDM Node",
                "HMDNode",
                "HDMNode",
                "NPC HMD",
                "NPC HDM"
            };

            for (const char* preferredName : preferredNames) {
                if (auto* obj = root->GetObjectByName(preferredName)) {
                    if (auto* node = obj->AsNode()) {
                        logResolvedNode(node, preferredName);
                        return node;
                    }
                }
            }

            std::vector<RE::NiNode*> matches;
            collectHmdNodes(root, matches);
            if (matches.empty()) {
                return nullptr;
            }

            std::sort(matches.begin(), matches.end(), [](const RE::NiNode* lhs, const RE::NiNode* rhs) {
                const int leftScore = scoreHmdNodeName(lhs ? lhs->name.c_str() : "");
                const int rightScore = scoreHmdNodeName(rhs ? rhs->name.c_str() : "");
                if (leftScore != rightScore) {
                    return leftScore < rightScore;
                }

                const std::size_t leftLength = lhs ? std::strlen(lhs->name.c_str()) : 0;
                const std::size_t rightLength = rhs ? std::strlen(rhs->name.c_str()) : 0;
                return leftLength < rightLength;
            });

            std::string candidates;
            for (std::size_t i = 0; i < matches.size(); ++i) {
                if (i > 0) {
                    candidates += ", ";
                }
                candidates += "'";
                candidates += matches[i] ? matches[i]->name.c_str() : "<null>";
                candidates += "'";
            }
            logResolvedNode(matches.front(), candidates);

            return matches.front();
        }
    }

    RE::NiNode* VRUIHandTracking::getMenuHandNode(bool isVRIKInstalled)
    {
        auto& settings = VRUISettings::get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto* rootObj = player->Get3D(!isVRIKInstalled);
        auto* root = rootObj ? rootObj->AsNode() : nullptr;
        if (!root) return nullptr;

        const char* magicNodeName = settings.useLeftHandAsMenu ? "NPC L MagicNode [LMag]" : "NPC R MagicNode [RMag]";
        const char* handBone = settings.useLeftHandAsMenu ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]";
        const char* wandNodeName = settings.useLeftHandAsMenu ? "Left Wand Node" : "Right Wand Node";
        return resolveHandAnchorNode(root, magicNodeName, wandNodeName, handBone);
    }

    RE::NiNode* VRUIHandTracking::getDominantHandNode(bool isVRIKInstalled)
    {
        auto& settings = VRUISettings::get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto* rootObj = player->Get3D(!isVRIKInstalled);
        auto* root = rootObj ? rootObj->AsNode() : nullptr;
        if (!root) return nullptr;

        const char* magicNodeName = settings.useLeftHandAsMenu ? "NPC R MagicNode [RMag]" : "NPC L MagicNode [LMag]";
        const char* handBone = settings.useLeftHandAsMenu ? "NPC R Hand [RHnd]" : "NPC L Hand [LHnd]";
        const char* wandNodeName = settings.useLeftHandAsMenu ? "Right Wand Node" : "Left Wand Node";
        return resolveHandAnchorNode(root, magicNodeName, wandNodeName, handBone);
    }

    RE::NiNode* VRUIHandTracking::getNonDominantHandNode(bool isVRIKInstalled)
    {
        auto& settings = VRUISettings::get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto* rootObj = player->Get3D(!isVRIKInstalled);
        auto* root = rootObj ? rootObj->AsNode() : nullptr;
        if (!root) return nullptr;

        const char* magicNodeName = settings.useLeftHandAsMenu ? "NPC L MagicNode [LMag]" : "NPC R MagicNode [RMag]";
        const char* handBone = settings.useLeftHandAsMenu ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]";
        const char* wandNodeName = settings.useLeftHandAsMenu ? "Left Wand Node" : "Right Wand Node";
        return resolveHandAnchorNode(root, magicNodeName, wandNodeName, handBone);
    }

    RE::NiNode* VRUIHandTracking::getPlayerSkeletonRoot(bool isVRIKInstalled)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto* root3d = player->Get3D(!isVRIKInstalled);
        if (!root3d) return nullptr;
        return root3d->AsNode();
    }

    RE::NiNode* VRUIHandTracking::getHeadNode(bool isVRIKInstalled)
    {
        auto* root = getPlayerSkeletonRoot(isVRIKInstalled);
        if (!root) return nullptr;

        if (auto* hmdNode = resolveHmdNode(root)) {
            return hmdNode;
        }

        static std::string lastWarnedRootName;
        const std::string rootName = root->name.c_str();
        if (rootName != lastWarnedRootName) {
            logger::warn("DragonBoardVR: No HMD/HDM node found under '{}'; falling back to skeleton root.", rootName);
            lastWarnedRootName = rootName;
        }
        return root;
    }
}
