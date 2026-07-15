#include "pch.h"

#include "JournalMenuProbe.h"

#include "VRMenuManager.h"
#include "VRUIPanel.h"

#include <RE/J/JournalMenu.h>
#include <RE/U/UI.h>

#include <cmath>

namespace vrui
{
    namespace
    {
        constexpr std::uint32_t kJournalOpenTimeoutFrames = 300;
        constexpr float kTransformEpsilon = 0.001f;

        const char* NodeName(const RE::NiAVObject* node)
        {
            return node && !node->name.empty() ? node->name.c_str() : "<unnamed>";
        }

        const char* ParentName(const RE::NiAVObject* node)
        {
            return node && node->parent ? NodeName(node->parent) : "<none>";
        }

        bool TranslationChanged(const RE::NiTransform& lhs, const RE::NiTransform& rhs)
        {
            return std::abs(lhs.translate.x - rhs.translate.x) > kTransformEpsilon ||
                   std::abs(lhs.translate.y - rhs.translate.y) > kTransformEpsilon ||
                   std::abs(lhs.translate.z - rhs.translate.z) > kTransformEpsilon;
        }

        void LogNode(const char* label, const RE::NiAVObject* node)
        {
            if (!node) {
                logger::info("DragonBoardVR SkyUI probe: {}=<null>.", label);
                return;
            }

            logger::info(
                "DragonBoardVR SkyUI probe: {} node='{}' ptr={} parent='{}' "
                "localPos=({:.3f}, {:.3f}, {:.3f}) localScale={:.5f} "
                "worldPos=({:.3f}, {:.3f}, {:.3f}) worldScale={:.5f}.",
                label,
                NodeName(node),
                static_cast<const void*>(node),
                ParentName(node),
                node->local.translate.x,
                node->local.translate.y,
                node->local.translate.z,
                node->local.scale,
                node->world.translate.x,
                node->world.translate.y,
                node->world.translate.z,
                node->world.scale);
        }
    }

    JournalMenuProbe& JournalMenuProbe::GetSingleton()
    {
        static JournalMenuProbe singleton;
        return singleton;
    }

    void JournalMenuProbe::ArmFromDragonBoard()
    {
        Restore();

        _armed = true;
        _journalWasOpen = false;
        _nodesLogged = false;
        _overwriteLogged = false;
        _framesWaitingForJournal = 0;
        _targetAvailable = false;
        _rotationOffsetCaptured = false;
        _targetAnchor = nullptr;

        auto& manager = VRMenuManager::get();
        auto panel = manager.findPanelByName("Background_Panel");
        if (!panel || !panel->getNode()) {
            panel = manager.findPanelByName("Persistent_Panel");
        }
        if (!panel || !panel->getNode()) {
            panel = manager.findPanelByName("MainPanel");
        }

        auto* targetNode = panel ? panel->getNode() : nullptr;
        if (targetNode) {
            _targetWorld = targetNode->world;
            _targetAvailable = true;

            if (targetNode->parent) {
                _targetAnchor = RE::NiPointer<RE::NiNode>(targetNode->parent);
                _targetLocal = targetNode->parent->world.Invert() * targetNode->world;
            }

            logger::info(
                "DragonBoardVR SkyUI probe: armed from board node '{}' with anchor '{}' at worldPos=({:.3f}, {:.3f}, {:.3f}).",
                NodeName(targetNode),
                NodeName(_targetAnchor.get()),
                _targetWorld.translate.x,
                _targetWorld.translate.y,
                _targetWorld.translate.z);
        } else {
            logger::warn(
                "DragonBoardVR SkyUI probe: armed without a board target; node discovery will run but no transform will be changed.");
        }
    }

    void JournalMenuProbe::Update()
    {
        if (!_armed && !_activeUiNode) {
            return;
        }

        auto* ui = RE::UI::GetSingleton();
        const bool journalOpen = ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME);
        if (!journalOpen) {
            if (_journalWasOpen || _activeUiNode) {
                logger::info("DragonBoardVR SkyUI probe: Journal Menu closed; restoring engine UI transform.");
                Restore();
                _armed = false;
                _journalWasOpen = false;
                return;
            }

            if (_armed && ++_framesWaitingForJournal >= kJournalOpenTimeoutFrames) {
                logger::warn("DragonBoardVR SkyUI probe: Journal Menu did not open; cancelling probe.");
                Reset();
            }
            return;
        }

        _journalWasOpen = true;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* nodes = player ? player->GetVRNodeData() : nullptr;
        if (!nodes) {
            if (!_nodesLogged) {
                logger::warn("DragonBoardVR SkyUI probe: PlayerCharacter VR node data is unavailable.");
                _nodesLogged = true;
            }
            return;
        }

        if (!_nodesLogged) {
            logger::info("DragonBoardVR SkyUI probe: Journal Menu opened; inspecting Skyrim VR UI nodes.");
            LogVrNodes(*nodes);
            _nodesLogged = true;
        }

        auto* uiNode = nodes->uiNode.get();
        if (!uiNode || !uiNode->parent || !RefreshTargetWorld()) {
            return;
        }

        if (!_activeUiNode) {
            _activeUiNode = RE::NiPointer<RE::NiNode>(uiNode);
            _originalLocal = uiNode->local;
            _uiRotationFromTarget = _targetWorld.rotate.Transpose() * uiNode->world.rotate;
            _rotationOffsetCaptured = true;
            logger::info(
                "DragonBoardVR SkyUI probe: captured original uiNode transform; applying board-following pose while preserving the initial Journal orientation and scale.");
        } else if (_activeUiNode.get() != uiNode) {
            logger::warn(
                "DragonBoardVR SkyUI probe: uiNode changed while Journal was open; restoring and stopping this probe.");
            Restore();
            _armed = false;
            return;
        } else if (!_overwriteLogged && TranslationChanged(uiNode->local, _lastAppliedLocal)) {
            logger::info(
                "DragonBoardVR SkyUI probe: Skyrim rewrote uiNode between updates; the probe will keep applying the test transform while Journal remains open.");
            _overwriteLogged = true;
        }

        ApplyAnchoredPose(*uiNode);
    }

    void JournalMenuProbe::Reset()
    {
        Restore();
        _armed = false;
        _journalWasOpen = false;
        _nodesLogged = false;
        _targetAvailable = false;
        _rotationOffsetCaptured = false;
        _overwriteLogged = false;
        _framesWaitingForJournal = 0;
        _targetAnchor = nullptr;
    }

    void JournalMenuProbe::LogVrNodes(RE::VR_NODE_DATA& nodes) const
    {
        LogNode("uiNode", nodes.uiNode.get());
        LogNode("InWorldUIQuadGeo", nodes.InWorldUIQuadGeo.get());
        LogNode("UIPointerNode", nodes.UIPointerNode.get());
        LogNode("UIPointerGeo", nodes.UIPointerGeo.get());
        LogNode("DialogueUINode", nodes.DialogueUINode.get());
    }

    bool JournalMenuProbe::RefreshTargetWorld()
    {
        if (_targetAnchor) {
            _targetWorld = _targetAnchor->world * _targetLocal;
            _targetAvailable = true;
        }
        return _targetAvailable;
    }

    void JournalMenuProbe::ApplyAnchoredPose(RE::NiNode& uiNode)
    {
        RE::NiTransform desiredWorld = uiNode.world;
        desiredWorld.translate = _targetWorld.translate;
        if (_rotationOffsetCaptured) {
            desiredWorld.rotate = _targetWorld.rotate * _uiRotationFromTarget;
        }
        desiredWorld.scale = _originalLocal.scale * uiNode.parent->world.scale;

        const RE::NiTransform parentInverse = uiNode.parent->world.Invert();
        uiNode.local = parentInverse * desiredWorld;
        _lastAppliedLocal = uiNode.local;

        RE::NiUpdateData updateData;
        uiNode.Update(updateData);
        uiNode.UpdateWorldBound();
    }

    void JournalMenuProbe::Restore()
    {
        if (_activeUiNode) {
            _activeUiNode->local = _originalLocal;
            RE::NiUpdateData updateData;
            _activeUiNode->Update(updateData);
            _activeUiNode->UpdateWorldBound();
            logger::info("DragonBoardVR SkyUI probe: original uiNode transform restored.");
            _activeUiNode = nullptr;
        }
    }
}
