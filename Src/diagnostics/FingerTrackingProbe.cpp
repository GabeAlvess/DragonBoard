#include "pch.h"
#include "FingerTrackingProbe.h"

#include "higgsinterface001.h"
#include "vrui/VRUIWidget.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace dragonboard::diagnostics
{
    namespace
    {
        struct HandNodes
        {
            RE::NiAVObject* hand = nullptr;
            RE::NiAVObject* indexPrevious = nullptr;
            RE::NiAVObject* indexTip = nullptr;
        };

        const char* SafeName(const RE::NiAVObject* object)
        {
            if (!object || object->name.empty()) {
                return "<unnamed>";
            }
            return object->name.c_str();
        }

        RE::NiAVObject* FindFirst(
            RE::NiNode* root,
            const std::array<const char*, 3>& names)
        {
            if (!root) {
                return nullptr;
            }

            for (const char* name : names) {
                if (auto* object = root->GetObjectByName(name)) {
                    return object;
                }
            }
            return nullptr;
        }

        HandNodes ResolveHandNodes(RE::NiNode* root, bool isLeft)
        {
            const std::array<const char*, 3> handNames = isLeft ?
                std::array{
                    "NPC L Hand [LHnd]",
                    "NPC L MagicNode [LMag]",
                    "Left Wand Node" } :
                std::array{
                    "NPC R Hand [RHnd]",
                    "NPC R MagicNode [RMag]",
                    "Right Wand Node" };

            // Prefer the distal index bone, but retain proximal fallbacks so
            // an unusual skeleton can still prove that its finger chain exists.
            const std::array<const char*, 3> indexNames = isLeft ?
                std::array{
                    "NPC L Finger12 [LF12]",
                    "NPC L Finger11 [LF11]",
                    "NPC L Finger10 [LF10]" } :
                std::array{
                    "NPC R Finger12 [RF12]",
                    "NPC R Finger11 [RF11]",
                    "NPC R Finger10 [RF10]" };

            const std::array<const char*, 3> previousNames = isLeft ?
                std::array{
                    "NPC L Finger11 [LF11]",
                    "NPC L Finger10 [LF10]",
                    "NPC L Finger12 [LF12]" } :
                std::array{
                    "NPC R Finger11 [RF11]",
                    "NPC R Finger10 [RF10]",
                    "NPC R Finger12 [RF12]" };

            return {
                FindFirst(root, handNames),
                FindFirst(root, previousNames),
                FindFirst(root, indexNames) };
        }

        RE::NiPointer<RE::NiNode> LoadMarkerModel()
        {
            auto marker = vrui::VRUIWidget::loadModelFromNif(
                "DragonBoardVR\\font\\symbol.nif");
            if (!marker) {
                marker = vrui::VRUIWidget::loadModelFromNif(
                    "DragonBoardVR\\Dragonbeam.nif");
            }
            if (marker) {
                marker->collisionObject = nullptr;
                marker->SetAppCulled(true);
            }
            return marker;
        }

        void UpdateMarker(
            RE::NiNode* root,
            const HandNodes& nodes,
            RE::NiNode* marker,
            float markerScale,
            float tipExtension,
            const RE::NiPoint3& touchLocalOffset)
        {
            if (!root || !nodes.indexTip || !marker) {
                if (marker) {
                    marker->SetAppCulled(true);
                }
                return;
            }

            RE::NiPoint3 tipPosition = nodes.indexTip->world.translate;
            if (nodes.indexPrevious) {
                RE::NiPoint3 direction =
                    nodes.indexTip->world.translate - nodes.indexPrevious->world.translate;
                const float length = direction.Length();
                if (length > 1.0e-4f) {
                    direction = direction / length;
                    tipPosition = tipPosition + direction * tipExtension;
                }
            }
            tipPosition = tipPosition +
                nodes.indexTip->world.rotate * touchLocalOffset;

            const float rootScale = std::max(root->world.scale, 1.0e-4f);
            marker->local.translate =
                root->world.rotate.Transpose() *
                (tipPosition - root->world.translate) / rootScale;
            marker->local.rotate =
                root->world.rotate.Transpose() * nodes.indexTip->world.rotate;
            marker->local.scale = markerScale / rootScale;
            marker->SetAppCulled(false);

            RE::NiUpdateData updateData;
            updateData.time = 0.0f;
            updateData.flags = RE::NiUpdateData::Flag::kDirty;
            marker->Update(updateData);
            marker->UpdateWorldBound();
        }

        void CollectFingerNodeNames(
            RE::NiNode* node,
            std::vector<std::string>& names,
            std::size_t limit)
        {
            if (!node || names.size() >= limit) {
                return;
            }

            const std::string_view name = node->name.c_str();
            if (name.find("Finger") != std::string_view::npos ||
                name.find("[LF") != std::string_view::npos ||
                name.find("[RF") != std::string_view::npos) {
                names.emplace_back(name);
            }

            for (auto& child : node->GetChildren()) {
                if (auto* childNode = child ? child->AsNode() : nullptr) {
                    CollectFingerNodeNames(childNode, names, limit);
                    if (names.size() >= limit) {
                        return;
                    }
                }
            }
        }

        void LogRuntimeSkeleton(RE::NiNode* root, bool isVrikInstalled)
        {
            std::vector<std::string> fingerNames;
            CollectFingerNodeNames(root, fingerNames, 64);

            std::string joined;
            for (std::size_t i = 0; i < fingerNames.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += "'";
                joined += fingerNames[i];
                joined += "'";
            }

            logger::info(
                "DragonBoardVR: Finger probe using runtime root '{}' (VRIK={}, HIGGS={}).",
                SafeName(root),
                isVrikInstalled,
                g_higgsInterface != nullptr);
            if (fingerNames.empty()) {
                logger::warn(
                    "DragonBoardVR: Finger probe found no named finger nodes in the runtime skeleton.");
            } else {
                logger::info(
                    "DragonBoardVR: Finger probe runtime nodes [{}]",
                    joined);
            }
        }

        float ReadIndexCurl(bool isLeft)
        {
            if (!g_higgsInterface) {
                return -1.0f;
            }

            float values[5]{};
            g_higgsInterface->GetFingerValues(isLeft, values);
            return values[1];
        }

        void LogHandSample(RE::NiNode* root, bool isLeft)
        {
            const HandNodes nodes = ResolveHandNodes(root, isLeft);
            const char* side = isLeft ? "left" : "right";
            const float indexCurl = ReadIndexCurl(isLeft);

            if (!nodes.indexTip) {
                logger::warn(
                    "DragonBoardVR: Finger probe {} index node missing (hand='{}', HIGGS index={:.3f}).",
                    side,
                    SafeName(nodes.hand),
                    indexCurl);
                return;
            }

            const auto& world = nodes.indexTip->world.translate;
            RE::NiPoint3 handLocal{};
            float handDistance = -1.0f;
            if (nodes.hand) {
                const RE::NiPoint3 handDelta = world - nodes.hand->world.translate;
                handLocal = nodes.hand->world.rotate.Transpose() * handDelta;
                const float handScale = std::max(nodes.hand->world.scale, 1.0e-4f);
                handLocal = handLocal / handScale;
                handDistance = handDelta.Length();
            }

            logger::info(
                "DragonBoardVR: Finger probe {} index='{}' hand='{}' world=({:.3f},{:.3f},{:.3f}) handLocal=({:.3f},{:.3f},{:.3f}) distance={:.3f} HIGGS index={:.3f}",
                side,
                SafeName(nodes.indexTip),
                SafeName(nodes.hand),
                world.x,
                world.y,
                world.z,
                handLocal.x,
                handLocal.y,
                handLocal.z,
                handDistance,
                indexCurl);
        }
    }

    FingerTrackingProbe& FingerTrackingProbe::GetSingleton()
    {
        static FingerTrackingProbe instance;
        return instance;
    }

    void FingerTrackingProbe::Update(
        RE::NiNode* skeletonRoot,
        bool isVrikInstalled,
        bool boardOpen,
        bool enabled,
        bool showMarkers,
        float markerScale,
        float tipExtension,
        const RE::NiPoint3& touchLocalOffset,
        float logIntervalSeconds,
        float deltaTime)
    {
        if (!enabled) {
            if (_active) {
                logger::info("DragonBoardVR: Finger probe disabled.");
            }
            DetachMarkers();
            Reset();
            return;
        }

        if (!boardOpen || !skeletonRoot) {
            if (_active) {
                logger::info("DragonBoardVR: Finger probe paused.");
            }
            HideMarkers();
            _active = false;
            _elapsed = 0.0f;
            if (!skeletonRoot) {
                DetachMarkers();
                _lastSkeletonRoot = nullptr;
            }
            return;
        }

        _active = true;
        if (skeletonRoot != _lastSkeletonRoot) {
            DetachMarkers();
            _lastSkeletonRoot = skeletonRoot;
            _elapsed = logIntervalSeconds;
            LogRuntimeSkeleton(skeletonRoot, isVrikInstalled);
        }

        if (showMarkers) {
            if (!_leftMarker) {
                _leftMarker = LoadMarkerModel();
            }
            if (!_rightMarker) {
                _rightMarker = LoadMarkerModel();
            }
            if (_leftMarker && _leftMarker->parent != skeletonRoot) {
                if (_leftMarker->parent) {
                    _leftMarker->parent->DetachChild(_leftMarker.get());
                }
                skeletonRoot->AttachChild(_leftMarker.get());
            }
            if (_rightMarker && _rightMarker->parent != skeletonRoot) {
                if (_rightMarker->parent) {
                    _rightMarker->parent->DetachChild(_rightMarker.get());
                }
                skeletonRoot->AttachChild(_rightMarker.get());
            }

            UpdateMarker(
                skeletonRoot,
                ResolveHandNodes(skeletonRoot, true),
                _leftMarker.get(),
                markerScale,
                tipExtension,
                touchLocalOffset);
            UpdateMarker(
                skeletonRoot,
                ResolveHandNodes(skeletonRoot, false),
                _rightMarker.get(),
                markerScale,
                tipExtension,
                touchLocalOffset);
        } else {
            HideMarkers();
        }

        _elapsed += std::max(deltaTime, 0.0f);
        if (_elapsed < logIntervalSeconds) {
            return;
        }
        _elapsed = 0.0f;

        LogHandSample(skeletonRoot, true);
        LogHandSample(skeletonRoot, false);
    }

    void FingerTrackingProbe::Reset()
    {
        _lastSkeletonRoot = nullptr;
        _elapsed = 0.0f;
        _active = false;
    }

    void FingerTrackingProbe::DetachMarkers()
    {
        const auto detach = [](RE::NiNode* marker) {
            if (marker && marker->parent) {
                marker->parent->DetachChild(marker);
            }
        };
        detach(_leftMarker.get());
        detach(_rightMarker.get());
        _leftMarker.reset();
        _rightMarker.reset();
    }

    void FingerTrackingProbe::HideMarkers()
    {
        if (_leftMarker) {
            _leftMarker->SetAppCulled(true);
        }
        if (_rightMarker) {
            _rightMarker->SetAppCulled(true);
        }
    }
}
