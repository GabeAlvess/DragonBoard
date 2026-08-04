#include "integrations/higgs/PhysicalBoardController.h"

#include "higgsinterface001.h"
#include "runtime/vr/GameMenuActions.h"
#include "integrations/vrik/VrikBoardProxyController.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUISettings.h"

namespace dragonboard::integrations::higgs
{
    namespace
    {
        void ApplyConfiguredObjectTransform(RE::NiNode* rootNode)
        {
            if (!rootNode) {
                return;
            }

            const auto& settings = vrui::VRUISettings::get();
            const RE::NiPoint3 offset{
                settings.backgroundOffsetX,
                settings.backgroundOffsetY,
                settings.backgroundOffsetZ
            };
            const float scale = (std::max)(0.001f, settings.backgroundScale);

            for (const auto* name : { "DragonBoard", "DragonBoardCollision" }) {
                if (auto* object = rootNode->GetObjectByName(name)) {
                    object->local.translate = offset;
                    object->local.scale = scale;
                }
            }

            RE::NiUpdateData updateData;
            updateData.flags = RE::NiUpdateData::Flag::kDirty;
            rootNode->Update(updateData);
        }
    }

    PhysicalBoardController& PhysicalBoardController::GetSingleton()
    {
        static PhysicalBoardController instance;
        return instance;
    }

    void PhysicalBoardController::Initialize()
    {
        if (_callbacksRegistered || !g_higgsInterface) {
            return;
        }

        g_higgsInterface->AddGrabbedCallback(OnGrabbed);
        g_higgsInterface->AddDroppedCallback(OnDropped);
        g_higgsInterface->AddStashedCallback(OnStashed);
        _callbacksRegistered = true;
        logger::info(
            "DragonBoardVR: physical board HIGGS callbacks registered.");
    }

    void PhysicalBoardController::RefreshConfiguredForm()
    {
        const auto& settings = vrui::VRUISettings::get();
        _cachedPlugin = settings.physicalBoardPlugin;
        _cachedLocalFormID = settings.physicalBoardLocalFormID;
        _configuredBaseForm = nullptr;

        if (!settings.physicalBoardEnabled) {
            logger::info(
                "DragonBoardVR: physical board prototype disabled in INI.");
            return;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            logger::warn(
                "DragonBoardVR: cannot resolve physical board form before TESDataHandler is ready.");
            return;
        }

        _configuredBaseForm = dataHandler->LookupForm<RE::TESObjectMISC>(
            _cachedLocalFormID,
            _cachedPlugin);
        if (!_configuredBaseForm) {
            logger::error(
                "DragonBoardVR: physical board form {:06X} was not found in '{}'.",
                _cachedLocalFormID,
                _cachedPlugin);
            return;
        }

        logger::info(
            "DragonBoardVR: physical board form resolved: plugin='{}' local={:06X} runtime={:08X}.",
            _cachedPlugin,
            _cachedLocalFormID,
            _configuredBaseForm->GetFormID());
    }

    void PhysicalBoardController::Reset()
    {
        if (_mapStoreRestoreHand && g_higgsInterface) {
            g_higgsInterface->EnableHand(_mapStoreLeft);
        }
        _mapStoreStage = MapStoreStage::None;
        _mapStoreReference.reset();
        _mapStoreBaseForm = nullptr;
        _mapStoreInitialCount = 0;
        _mapStoreWaitFrames = 0;
        _mapStoreLeft = false;
        _mapStoreRestoreHand = false;
        ClearPinnedItemGrabPriority();
        auto& manager = vrui::VRMenuManager::get();
        manager.closeMenu();
        manager.clearPhysicalBoardAnchor();
        _heldReference.reset();
        _heldLeft = false;
        _heldPlayerCellFormID = 0;
        _missingHeldBoardAfterCellChangeFrames = 0;
    }

    void PhysicalBoardController::Update()
    {
        if (_mapStoreStage != MapStoreStage::None) {
            UpdateMapStoreRequest();
            return;
        }

        if (!g_higgsInterface) {
            return;
        }

        RE::TESObjectREFR* heldReference = nullptr;
        bool heldLeft = _heldLeft;
        const auto resolveHeldBoard = [this](bool isLeft) -> RE::TESObjectREFR* {
            auto* candidate = g_higgsInterface->GetGrabbedObject(isLeft);
            return candidate && MatchesReference(candidate) ? candidate : nullptr;
        };

        if (_heldReference) {
            heldReference = resolveHeldBoard(_heldLeft);
            if (!heldReference) {
                heldLeft = !_heldLeft;
                heldReference = resolveHeldBoard(heldLeft);
            }
        } else {
            heldLeft = true;
            heldReference = resolveHeldBoard(true);
            if (!heldReference) {
                heldLeft = false;
                heldReference = resolveHeldBoard(false);
            }
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto currentPlayerCellFormID =
            player && player->parentCell ? player->parentCell->GetFormID() : 0;
        if (!heldReference) {
            const bool playerChangedCell =
                _heldReference && _heldPlayerCellFormID != 0 &&
                currentPlayerCellFormID != 0 &&
                currentPlayerCellFormID != _heldPlayerCellFormID;
            if (!playerChangedCell) {
                _missingHeldBoardAfterCellChangeFrames = 0;
                return;
            }

            constexpr std::uint32_t kCellTransitionRecoveryFrames = 30;
            ++_missingHeldBoardAfterCellChangeFrames;
            if (_missingHeldBoardAfterCellChangeFrames <
                kCellTransitionRecoveryFrames) {
                return;
            }

            bool restoredToInventory = false;
            const auto staleReference = _heldReference.get();
            if (player && _configuredBaseForm && staleReference) {
                const auto countBefore = player->GetItemCount(_configuredBaseForm);
                player->PickUpObject(staleReference.get(), 1, false, false);
                restoredToInventory =
                    player->GetItemCount(_configuredBaseForm) > countBefore;
            }
            if (!restoredToInventory && player && _configuredBaseForm &&
                !staleReference) {
                player->AddObjectToContainer(
                    _configuredBaseForm,
                    nullptr,
                    1,
                    nullptr);
                restoredToInventory = true;
            }
            if (!restoredToInventory) {
                logger::error(
                    "DragonBoardVR: held physical board was lost during cell transition, but could not be restored to inventory.");
                _missingHeldBoardAfterCellChangeFrames = 0;
                return;
            }

            logger::info(
                "DragonBoardVR: recovered held physical board after player cell transition {:08X}->{:08X}; board returned to inventory and VRIK proxy restored.",
                _heldPlayerCellFormID,
                currentPlayerCellFormID);
            dragonboard::integrations::vrik::VrikBoardProxyController::GetSingleton()
                .NotifyPhysicalBoardStored();
            ClearPinnedItemGrabPriority();
            auto& manager = vrui::VRMenuManager::get();
            manager.closeMenu();
            manager.clearPhysicalBoardAnchor();
            _heldReference.reset();
            _heldLeft = false;
            _heldPlayerCellFormID = 0;
            _missingHeldBoardAfterCellChangeFrames = 0;
            return;
        }

        _heldPlayerCellFormID = currentPlayerCellFormID;
        _missingHeldBoardAfterCellChangeFrames = 0;

        auto* rootObject = heldReference->Get3D();
        auto* rootNode = rootObject ? rootObject->AsNode() : nullptr;
        if (!rootNode) {
            return;
        }

        auto& manager = vrui::VRMenuManager::get();
        const auto currentHandle = heldReference->CreateRefHandle();
        const bool referenceChanged = currentHandle != _heldReference;
        const bool anchorChanged = manager.getPhysicalBoardAnchorNode() != rootNode;
        const bool handChanged =
            !manager.isPhysicalBoardActive() ||
            manager.isPhysicalBoardHeldLeft() != heldLeft;
        if (!referenceChanged && !anchorChanged && !handChanged) {
            return;
        }

        ApplyConfiguredObjectTransform(rootNode);
        _heldReference = currentHandle;
        _heldLeft = heldLeft;
        manager.setPhysicalBoardAnchor(
            rootNode,
            heldLeft,
            heldReference->GetFormID());
        manager.openMenuForPhysicalBoard();

        logger::info(
            "DragonBoardVR: rebound held physical board {:08X} after 3D/cell transition (hand={}, root={:X}).",
            heldReference->GetFormID(),
            heldLeft ? "left" : "right",
            reinterpret_cast<std::uintptr_t>(rootNode));
    }

    bool PhysicalBoardController::StoreHeldBoardBeforeOpeningMap()
    {
        if (_mapStoreStage != MapStoreStage::None) {
            return true;
        }

        if (!g_higgsInterface) {
            return false;
        }

        RE::TESObjectREFR* heldReference = nullptr;
        if (_heldReference) {
            const auto resolved = _heldReference.get();
            if (resolved && MatchesReference(resolved.get())) {
                heldReference = resolved.get();
            }
        }

        if (!heldReference) {
            for (const bool isLeft : { true, false }) {
                auto* candidate = g_higgsInterface->GetGrabbedObject(isLeft);
                if (candidate && MatchesReference(candidate)) {
                    heldReference = candidate;
                    _heldLeft = isLeft;
                    break;
                }
            }
        }
        if (!heldReference || !_configuredBaseForm) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            logger::error(
                "DragonBoardVR: cannot store the physical board before MapMenu because the player is unavailable.");
            return true;
        }

        _mapStoreReference = heldReference->CreateRefHandle();
        _mapStoreBaseForm = _configuredBaseForm;
        _mapStoreInitialCount = player->GetItemCount(_mapStoreBaseForm);
        _mapStoreWaitFrames = 0;
        _mapStoreLeft = _heldLeft;

        ClearPinnedItemGrabPriority();
        auto& manager = vrui::VRMenuManager::get();
        manager.closeMenu();
        manager.clearPhysicalBoardAnchor();

        _mapStoreRestoreHand = !g_higgsInterface->IsDisabled(_mapStoreLeft);
        if (_mapStoreRestoreHand) {
            g_higgsInterface->DisableHand(_mapStoreLeft);
        }

        _mapStoreStage = MapStoreStage::WaitForHandDisable;
        logger::info(
            "DragonBoardVR: storing held physical board {:08X} from {} hand before opening MapMenu (inventory before={}).",
            heldReference->GetFormID(),
            _mapStoreLeft ? "left" : "right",
            _mapStoreInitialCount);
        return true;
    }

    void PhysicalBoardController::UpdateMapStoreRequest()
    {
        constexpr std::uint32_t kInventoryConfirmationTimeoutFrames = 30;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !_mapStoreBaseForm) {
            FinishMapStoreRequest(false);
            return;
        }

        if (_mapStoreStage == MapStoreStage::WaitForHandDisable) {
            const auto reference = _mapStoreReference.get();
            if (!reference) {
                logger::error(
                    "DragonBoardVR: physical board reference vanished before it could be stored for MapMenu.");
                FinishMapStoreRequest(false);
                return;
            }

            player->PickUpObject(reference.get(), 1, false, false);
            _mapStoreStage = MapStoreStage::WaitForInventory;
            _mapStoreWaitFrames = 0;
            return;
        }

        if (_mapStoreStage != MapStoreStage::WaitForInventory) {
            return;
        }

        const auto currentCount = player->GetItemCount(_mapStoreBaseForm);
        if (currentCount == _mapStoreInitialCount + 1) {
            logger::info(
                "DragonBoardVR: physical board stored before MapMenu (inventory after={}).",
                currentCount);
            FinishMapStoreRequest(true);
            return;
        }

        ++_mapStoreWaitFrames;
        if (_mapStoreWaitFrames >= kInventoryConfirmationTimeoutFrames) {
            logger::error(
                "DragonBoardVR: timed out storing physical board before MapMenu (inventory before={}, after={}); MapMenu was not opened.",
                _mapStoreInitialCount,
                currentCount);
            FinishMapStoreRequest(false);
        }
    }

    void PhysicalBoardController::FinishMapStoreRequest(bool success)
    {
        if (_mapStoreRestoreHand && g_higgsInterface) {
            g_higgsInterface->EnableHand(_mapStoreLeft);
        }

        _mapStoreStage = MapStoreStage::None;
        _mapStoreReference.reset();
        _mapStoreBaseForm = nullptr;
        _mapStoreInitialCount = 0;
        _mapStoreWaitFrames = 0;
        _mapStoreLeft = false;
        _mapStoreRestoreHand = false;
        _heldReference.reset();
        _heldLeft = false;

        if (success) {
            dragonboard::integrations::vrik::VrikBoardProxyController::GetSingleton()
                .NotifyPhysicalBoardStored();
            dragonboard::runtime::vr::ShowGameMenu("MapMenu");
        }
    }

    void PhysicalBoardController::UpdatePinnedItemGrabPriority(
        const void* owner,
        bool leftActive,
        bool rightActive)
    {
        if (!owner || !g_higgsInterface) {
            return;
        }

        auto& manager = vrui::VRMenuManager::get();
        const bool leftGripPressed = manager.isDominantHandLeft() ?
            manager.isDominantGripButtonDown() :
            manager.isOffhandGripButtonDown();
        const bool rightGripPressed = manager.isDominantHandLeft() ?
            manager.isOffhandGripButtonDown() :
            manager.isDominantGripButtonDown();
        if (!leftGripPressed) {
            _leftPinnedItemGrabBypass = false;
        }
        if (!rightGripPressed) {
            _rightPinnedItemGrabBypass = false;
        }

        const auto updateOwners = [owner](
            std::unordered_set<const void*>& owners,
            bool active,
            bool bypass) {
            if (active && !bypass) {
                owners.insert(owner);
            } else {
                owners.erase(owner);
            }
        };
        updateOwners(
            _leftPinnedItemPriorityOwners,
            leftActive,
            _leftPinnedItemGrabBypass);
        updateOwners(
            _rightPinnedItemPriorityOwners,
            rightActive,
            _rightPinnedItemGrabBypass);

        ApplyPinnedItemGrabPriority(true);
        ApplyPinnedItemGrabPriority(false);
    }

    void PhysicalBoardController::PrepareHandForPinnedItemGrab(bool isLeft)
    {
        if (!_heldReference || !g_higgsInterface) {
            return;
        }

        auto& owners = isLeft ?
            _leftPinnedItemPriorityOwners :
            _rightPinnedItemPriorityOwners;
        auto& bypass = isLeft ?
            _leftPinnedItemGrabBypass :
            _rightPinnedItemGrabBypass;
        owners.clear();
        bypass = true;
        ApplyPinnedItemGrabPriority(isLeft);
    }

    void PhysicalBoardController::ApplyPinnedItemGrabPriority(bool isLeft)
    {
        if (!g_higgsInterface) {
            return;
        }

        const auto& owners = isLeft ?
            _leftPinnedItemPriorityOwners :
            _rightPinnedItemPriorityOwners;
        auto& disabledByPriority = isLeft ?
            _leftHandDisabledByPinnedItemPriority :
            _rightHandDisabledByPinnedItemPriority;

        if (!owners.empty()) {
            if (!g_higgsInterface->IsDisabled(isLeft)) {
                g_higgsInterface->DisableHand(isLeft);
                disabledByPriority = true;
                logger::trace(
                    "DragonBoardVR: pinned item temporarily owns {} HIGGS grip priority.",
                    isLeft ? "left" : "right");
            }
            return;
        }

        if (disabledByPriority) {
            g_higgsInterface->EnableHand(isLeft);
            disabledByPriority = false;
            logger::trace(
                "DragonBoardVR: restored {} HIGGS hand after pinned item priority.",
                isLeft ? "left" : "right");
        }
    }

    void PhysicalBoardController::ClearPinnedItemGrabPriority()
    {
        _leftPinnedItemPriorityOwners.clear();
        _rightPinnedItemPriorityOwners.clear();
        _leftPinnedItemGrabBypass = false;
        _rightPinnedItemGrabBypass = false;
        ApplyPinnedItemGrabPriority(true);
        ApplyPinnedItemGrabPriority(false);
    }

    bool PhysicalBoardController::MatchesReference(RE::TESObjectREFR* reference)
    {
        return reference && MatchesBaseForm(reference->GetBaseObject());
    }

    bool PhysicalBoardController::MatchesBaseForm(RE::TESForm* baseForm)
    {
        const auto& settings = vrui::VRUISettings::get();
        if (!settings.physicalBoardEnabled || !baseForm) {
            return false;
        }

        if (_cachedPlugin != settings.physicalBoardPlugin ||
            _cachedLocalFormID != settings.physicalBoardLocalFormID ||
            !_configuredBaseForm) {
            RefreshConfiguredForm();
        }
        return _configuredBaseForm && baseForm == _configuredBaseForm;
    }

    void PhysicalBoardController::QueueGrab(
        bool isLeft,
        RE::TESObjectREFR* reference)
    {
        if (!MatchesReference(reference)) {
            return;
        }

        const auto handle = reference->CreateRefHandle();
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([handle, isLeft]() {
                const auto resolved = handle.get();
                if (resolved) {
                    GetSingleton().Activate(isLeft, resolved.get());
                }
            });
        }
    }

    void PhysicalBoardController::QueueRelease(
        RE::TESObjectREFR* reference,
        bool stashed)
    {
        if (reference && !MatchesReference(reference)) {
            return;
        }

        const auto handle = reference ? reference->CreateRefHandle() : RE::ObjectRefHandle{};
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([handle, stashed]() {
                const auto resolved = handle.get();
                GetSingleton().Deactivate(resolved.get(), stashed);
            });
        }
    }

    void PhysicalBoardController::Activate(
        bool isLeft,
        RE::TESObjectREFR* reference)
    {
        if (!reference || !MatchesReference(reference)) {
            return;
        }

        auto* rootObject = reference->Get3D();
        auto* rootNode = rootObject ? rootObject->AsNode() : nullptr;
        if (!rootNode) {
            logger::error(
                "DragonBoardVR: grabbed physical board {:08X} has no usable 3D root.",
                reference->GetFormID());
            return;
        }

        ApplyConfiguredObjectTransform(rootNode);

        _heldReference = reference->CreateRefHandle();
        _heldLeft = isLeft;
        const auto* player = RE::PlayerCharacter::GetSingleton();
        _heldPlayerCellFormID =
            player && player->parentCell ? player->parentCell->GetFormID() : 0;
        _missingHeldBoardAfterCellChangeFrames = 0;
        dragonboard::integrations::vrik::VrikBoardProxyController::GetSingleton()
            .NotifyPhysicalBoardGrabbed(reference, isLeft);
        auto& manager = vrui::VRMenuManager::get();
        manager.setPhysicalBoardAnchor(
            rootNode,
            isLeft,
            reference->GetFormID());
        manager.openMenuForPhysicalBoard();

        logger::info(
            "DragonBoardVR: physical board {:08X} grabbed in {} hand; UI enabled.",
            reference->GetFormID(),
            isLeft ? "left" : "right");
    }

    void PhysicalBoardController::Deactivate(
        RE::TESObjectREFR* reference,
        bool stashed)
    {
        if (!_heldReference) {
            return;
        }

        if (reference) {
            const auto releasedHandle = reference->CreateRefHandle();
            if (releasedHandle != _heldReference) {
                return;
            }
        }

        dragonboard::integrations::vrik::VrikBoardProxyController::GetSingleton()
            .NotifyPhysicalBoardReleased(reference, stashed);

        ClearPinnedItemGrabPriority();
        auto& manager = vrui::VRMenuManager::get();
        manager.closeMenu();
        manager.clearPhysicalBoardAnchor();
        _heldReference.reset();
        _heldLeft = false;
        _heldPlayerCellFormID = 0;
        _missingHeldBoardAfterCellChangeFrames = 0;

        logger::info(
            "DragonBoardVR: physical board released{}; all board panels closed.",
            stashed ? " and stashed" : "");
    }

    void PhysicalBoardController::OnGrabbed(
        bool isLeft,
        RE::TESObjectREFR* reference)
    {
        GetSingleton().QueueGrab(isLeft, reference);
    }

    void PhysicalBoardController::OnDropped(
        bool,
        RE::TESObjectREFR* reference)
    {
        GetSingleton().QueueRelease(reference, false);
    }

    void PhysicalBoardController::OnStashed(bool, RE::TESForm* baseForm)
    {
        auto& controller = GetSingleton();
        if (controller.MatchesBaseForm(baseForm)) {
            controller.QueueRelease(nullptr, true);
        }
    }
}
