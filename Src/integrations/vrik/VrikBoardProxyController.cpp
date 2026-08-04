#include "integrations/vrik/VrikBoardProxyController.h"

#include "integrations/higgs/PhysicalBoardController.h"
#include "integrations/vrik/VrikFingerPose.h"
#include "higgsinterface001.h"
#include "runtime/vr/ReferencePlacement.h"
#include "vrui/VRMenuManager.h"
#include "vrui/VRUIHandTracking.h"
#include "vrui/VRUILayoutManager.h"
#include "vrui/VRUISettings.h"

#include <RE/A/ActorEquipManager.h>
#include <RE/N/NiAVObject.h>
#include <RE/B/BGSArtObject.h>
#include <RE/B/BGSEquipSlot.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/S/ScriptEventSourceHolder.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESObjectMISC.h>
#include <RE/T/TESObjectREFR.h>
#include <RE/T/TESObjectWEAP.h>

#include <format>

namespace dragonboard::integrations::vrik
{
    namespace
    {
        constexpr float kHolsterStoreDistance = 10.0f;
        constexpr float kHolsterStoreDistanceSquared =
            kHolsterStoreDistance * kHolsterStoreDistance;
        constexpr float kHolsterDrawDistance = 10.0f;
        constexpr RE::NiPoint3 kHolsterBoardHalfExtents{ 9.90f, 0.70f, 7.15f };
        constexpr std::uint32_t kInitialHolsterAssignmentTimeoutFrames = 60;
        constexpr std::uint32_t kDirectEquipGraceFrames = 45;
        constexpr std::uint32_t kDragonBoardInventoryEquipIntentFrames = 30;
        constexpr std::uint32_t kHolsterDrawCandidateFrames = 180;
        constexpr std::uint32_t kHolsterDrawReleaseGraceFrames = 120;
        constexpr std::uint32_t kHolsterReassignmentStableFrames = 12;
        constexpr std::uint32_t kHolsterReassignmentTimeoutFrames = 300;

        RE::BGSEquipSlot* GetHandEquipSlot(bool isLeft)
        {
            return RE::TESForm::LookupByID<RE::BGSEquipSlot>(
                isLeft ? 0x00013F43 : 0x00013F42);
        }

        bool IsDirectEquipMenuOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && (
                ui->IsMenuOpen("InventoryMenu") ||
                ui->IsMenuOpen("FavoritesMenu") ||
                ui->IsMenuOpen("ContainerMenu") ||
                ui->IsMenuOpen("BarterMenu"));
        }

        bool IsOriginalInventoryMenuOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen("InventoryMenu");
        }

        RE::NiNode* GetControllerTrackingNode(bool isLeft)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* vrNodes = player ? player->GetVRNodeData() : nullptr;
            if (!vrNodes) {
                return nullptr;
            }

            auto* wandNode = isLeft ?
                vrNodes->LeftWandNode.get() : vrNodes->RightWandNode.get();
            if (wandNode) {
                return wandNode;
            }
            return isLeft ?
                vrNodes->LeftValveIndexControllerNode.get() :
                vrNodes->RightValveIndexControllerNode.get();
        }
    }

    VrikBoardProxyController& VrikBoardProxyController::GetSingleton()
    {
        static VrikBoardProxyController instance;
        return instance;
    }

    void VrikBoardProxyController::Initialize()
    {
        if (_initialized) {
            return;
        }

        auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();
        if (!eventSource) {
            logger::error(
                "DragonBoardVR: VRIK board proxy equip sink could not be registered.");
            return;
        }

        eventSource->AddEventSink<RE::TESEquipEvent>(this);
        _initialized = true;
        logger::info("DragonBoardVR: VRIK board proxy equip sink registered.");
    }

    void VrikBoardProxyController::RefreshConfiguredForms()
    {
        ClearHolsterDrawCandidate();
        SetHolsterSlotSuppressed(-1, false);
        const auto& settings = vrui::VRUISettings::get();
        _cachedPlugin = settings.physicalBoardPlugin;
        _cachedPhysicalLocalFormID = settings.physicalBoardLocalFormID;
        _cachedProxyLocalFormID = settings.physicalBoardVrikProxyLocalFormID;
        _physicalBaseForm = nullptr;
        _proxyBaseForm = nullptr;
        _formsResolved = false;

        if (!settings.physicalBoardEnabled) {
            return;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return;
        }

        _physicalBaseForm = dataHandler->LookupForm<RE::TESObjectMISC>(
            _cachedPhysicalLocalFormID,
            _cachedPlugin);
        _proxyBaseForm = dataHandler->LookupForm<RE::TESObjectWEAP>(
            _cachedProxyLocalFormID,
            _cachedPlugin);
        ResolveVrikSlotForms();
        _formsResolved = _physicalBaseForm && _proxyBaseForm;

        if (_formsResolved) {
            logger::info(
                "DragonBoardVR: VRIK proxy resolved (physical={:08X}, proxy={:08X}, slotGlobals={}/14, slotArt={}/14).",
                _physicalBaseForm->GetFormID(),
                _proxyBaseForm->GetFormID(),
                std::count_if(
                    _slotGlobals.begin(),
                    _slotGlobals.end(),
                    [](const auto* global) { return global != nullptr; }),
                std::count_if(
                    _slotArtObjects.begin(),
                    _slotArtObjects.end(),
                    [](const auto* art) { return art != nullptr; }));
        } else {
            logger::error(
                "DragonBoardVR: VRIK proxy forms were not found in '{}' (physical={:06X}, proxy={:06X}).",
                _cachedPlugin,
                _cachedPhysicalLocalFormID,
                _cachedProxyLocalFormID);
        }
    }

    void VrikBoardProxyController::Reset()
    {
        SetHolsterSlotSuppressed(-1, false);
        SetSourceSlotVisible(true);
        _activeSlotIndex = -1;
        if (_proxyWeaponCollisionDisabled && g_higgsInterface) {
            g_higgsInterface->EnableWeaponCollision(_proxyWeaponCollisionHandLeft);
        }
        _activeHandLeft = false;
        _proxyWeaponCollisionDisabled = false;
        _proxyWeaponCollisionHandLeft = false;
        _physicalBoardHeld = false;
        _activePhysicalReference.reset();
        _pendingInitialHolsterReference.reset();
        _pendingInitialHolsterFrames = 0;
        _pendingInitialHolsterStashed = false;
        _directEquipGraceFrames = 0;
        _dragonBoardInventoryEquipPending = false;
        _dragonBoardInventoryEquipHandLeft = false;
        _dragonBoardInventoryEquipFrames = 0;
        ClearHolsterDrawCandidate();
        _holsterReassignmentArmed = false;
        _holsterReassignmentCandidate = -1;
        _holsterReassignmentStableFrames = 0;
        _holsterReassignmentWaitFrames = 0;
        _slotDisplayedPrevious.fill(false);
        ClearHolsterAnchor();
    }

    void VrikBoardProxyController::Update()
    {
        if (_dragonBoardInventoryEquipFrames > 0) {
            --_dragonBoardInventoryEquipFrames;
            if (_dragonBoardInventoryEquipFrames == 0) {
                _dragonBoardInventoryEquipPending = false;
            }
        }

        const auto& settings = vrui::VRUISettings::get();
        if (!settings.physicalBoardEnabled) {
            return;
        }

        if (!_formsResolved ||
            _cachedPlugin != settings.physicalBoardPlugin ||
            _cachedPhysicalLocalFormID != settings.physicalBoardLocalFormID ||
            _cachedProxyLocalFormID != settings.physicalBoardVrikProxyLocalFormID) {
            RefreshConfiguredForms();
        }
        if (!_formsResolved) {
            return;
        }

        if (IsDirectEquipMenuOpen()) {
            _directEquipGraceFrames = kDirectEquipGraceFrames;
        } else if (_directEquipGraceFrames > 0) {
            --_directEquipGraceFrames;
        }

        EnsureProxyInventory();
        ProcessPendingInitialHolster();
        if (_holsterDrawCandidateFrames > 0) {
            --_holsterDrawCandidateFrames;
            if (_holsterDrawCandidateFrames == 0) {
                ClearHolsterDrawCandidate();
            }
        }
        if (_physicalBoardHeld) {
            const auto suppressedSlot = _activeSlotIndex >= 0 ?
                _activeSlotIndex : _holsterAnchorSlotIndex;
            SetHolsterSlotSuppressed(suppressedSlot, true);
        }

        if (_holsterReassignmentArmed && !IsProxyEquipped(true) &&
            !IsProxyEquipped(false)) {
            ++_holsterReassignmentWaitFrames;
            const auto sourceSlotIndex = _activeSlotIndex >= 0 ?
                _activeSlotIndex : _holsterAnchorSlotIndex;
            const auto newlyDisplayedSlot = FindNewlyDisplayedProxySlot();
            if (newlyDisplayedSlot >= 0 && newlyDisplayedSlot != sourceSlotIndex) {
                _holsterReassignmentCandidate = newlyDisplayedSlot;
                _holsterReassignmentStableFrames = 0;
                logger::info(
                    "DragonBoardVR: VRIK holster reassignment candidate detected at slot {}.",
                    newlyDisplayedSlot + 1);
            }

            if (_holsterReassignmentCandidate >= 0) {
                const auto candidateIndex =
                    static_cast<std::size_t>(_holsterReassignmentCandidate);
                const auto* candidateGlobal = _slotGlobals[candidateIndex];
                if (candidateGlobal && candidateGlobal->value > 0.5f) {
                    ++_holsterReassignmentStableFrames;
                    if (_holsterReassignmentStableFrames >=
                        kHolsterReassignmentStableFrames) {
                        const auto candidateSlot =
                            _holsterReassignmentCandidate;
                        CaptureHolsterAnchor(candidateSlot);
                        if (_holsterAnchorSlotIndex != candidateSlot) {
                            if (_holsterReassignmentStableFrames ==
                                kHolsterReassignmentStableFrames) {
                                logger::info(
                                    "DragonBoardVR: VRIK slot {} is stable; waiting for its holster visual anchor before confirming reassignment.",
                                    candidateSlot + 1);
                            }
                        } else {
                            const auto previousSlot = sourceSlotIndex + 1;
                            if (_activeSlotIndex >= 0) {
                                _activeSlotIndex = candidateSlot;
                            }
                            const auto confirmedSlot = candidateSlot + 1;
                            _holsterReassignmentArmed = false;
                            _holsterReassignmentCandidate = -1;
                            _holsterReassignmentStableFrames = 0;
                            _holsterReassignmentWaitFrames = 0;
                            logger::info(
                                "DragonBoardVR: shared VRIK holster reassignment confirmed from slot {} to slot {} (physicalActive={}).",
                                previousSlot,
                                confirmedSlot,
                                _activeSlotIndex >= 0);
                        }
                    }
                } else {
                    _holsterReassignmentCandidate = -1;
                    _holsterReassignmentStableFrames = 0;
                }
            } else if (_holsterReassignmentWaitFrames >=
                kHolsterReassignmentTimeoutFrames) {
                _holsterReassignmentArmed = false;
                _holsterReassignmentWaitFrames = 0;
                logger::info(
                    "DragonBoardVR: VRIK holster reassignment expired without a new slot.");
            }
        }

        if (!_holsterReassignmentArmed) {
            UpdateHolsterAnchor();
        }

        if (_activeSlotIndex >= 0) {
            SetSourceSlotVisible(false);
        }

        for (std::size_t index = 0; index < _slotGlobals.size(); ++index) {
            const auto* global = _slotGlobals[index];
            _slotDisplayedPrevious[index] = global && global->value > 0.5f;
        }
    }

    void VrikBoardProxyController::ResolveVrikSlotForms()
    {
        _slotGlobals.fill(nullptr);
        _slotArtObjects.fill(nullptr);
        _slotDisplayedPrevious.fill(false);
        for (std::size_t index = 0; index < _slotGlobals.size(); ++index) {
            const auto slotNumber = index + 1;
            const auto globalEditorID = std::format("_vrik_glob_show_slot{}", slotNumber);
            const auto artEditorID = std::format("_vrik_arto_slot{}", slotNumber);
            _slotGlobals[index] =
                RE::TESForm::LookupByEditorID<RE::TESGlobal>(globalEditorID);
            _slotArtObjects[index] =
                RE::TESForm::LookupByEditorID<RE::BGSArtObject>(artEditorID);
        }
    }

    void VrikBoardProxyController::EnsureProxyInventory()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !_physicalBaseForm || !_proxyBaseForm) {
            return;
        }

        const auto physicalCount = player->GetItemCount(_physicalBaseForm);
        const auto proxyCount = player->GetItemCount(_proxyBaseForm);
        const bool proxyNeeded = physicalCount > 0 || _activeSlotIndex >= 0 ||
            static_cast<bool>(_activePhysicalReference) ||
            static_cast<bool>(_pendingInitialHolsterReference);
        if (proxyNeeded && proxyCount <= 0) {
            player->AddObjectToContainer(_proxyBaseForm, nullptr, 1, nullptr);
            logger::info(
                "DragonBoardVR: added the persistent shared VRIK holster proxy after the player acquired a physical DragonBoard.");
        }
    }

    void VrikBoardProxyController::PrepareDragonBoardInventoryEquip(
        RE::TESForm* form,
        bool isLeft)
    {
        _dragonBoardInventoryEquipPending = false;
        _dragonBoardInventoryEquipHandLeft = false;
        _dragonBoardInventoryEquipFrames = 0;

        if (!_formsResolved) {
            RefreshConfiguredForms();
        }
        if (!form || form != _proxyBaseForm) {
            return;
        }

        ClearHolsterDrawCandidate();
        _dragonBoardInventoryEquipPending = true;
        _dragonBoardInventoryEquipHandLeft = isLeft;
        _dragonBoardInventoryEquipFrames =
            kDragonBoardInventoryEquipIntentFrames;
        logger::info(
            "DragonBoardVR: armed DragonBoard inventory proxy equip for the {} hand.",
            isLeft ? "left" : "right");
    }

    void VrikBoardProxyController::NotifyGripInput(
        bool isLeft,
        bool pressed)
    {
        if (!pressed) {
            if (_holsterDrawCandidateSlot >= 0 &&
                _holsterDrawCandidateHandLeft == isLeft) {
                _holsterDrawCandidateFrames = std::min(
                    _holsterDrawCandidateFrames,
                    kHolsterDrawReleaseGraceFrames);
            }
            return;
        }

        if (!_formsResolved || !_proxyBaseForm || _physicalBoardHeld ||
            _activeSlotIndex >= 0 || _activePhysicalReference ||
            _pendingInitialHolsterReference || IsDirectEquipMenuOpen()) {
            return;
        }

        const auto assignedSlotIndex = FindAssignedProxySlot();
        if (assignedSlotIndex < 0) {
            return;
        }

        float handDistance = -1.0f;
        if (!IsHandNearHolster(
                static_cast<std::size_t>(assignedSlotIndex),
                isLeft,
                handDistance)) {
            return;
        }

        _holsterDrawCandidateSlot = assignedSlotIndex;
        _holsterDrawCandidateHandLeft = isLeft;
        _holsterDrawCandidateFrames = kHolsterDrawCandidateFrames;
        logger::info(
            "DragonBoardVR: latched VRIK slot {} draw from {} grip at {:.2f} units before pull.",
            assignedSlotIndex + 1,
            isLeft ? "left" : "right",
            handDistance);
    }

    void VrikBoardProxyController::ClearHolsterDrawCandidate()
    {
        _holsterDrawCandidateSlot = -1;
        _holsterDrawCandidateHandLeft = false;
        _holsterDrawCandidateFrames = 0;
    }

    void VrikBoardProxyController::ProcessPendingInitialHolster()
    {
        if (!_pendingInitialHolsterReference) {
            return;
        }

        const auto assignedSlotIndex = FindAssignedProxySlot();
        if (assignedSlotIndex >= 0) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto pendingReference = _pendingInitialHolsterReference.get();
            bool stored = _pendingInitialHolsterStashed;
            if (!stored && player && pendingReference) {
                const auto countBefore = player->GetItemCount(_physicalBaseForm);
                player->PickUpObject(pendingReference.get(), 1, false, false);
                stored = player->GetItemCount(_physicalBaseForm) > countBefore;
            }

            if (!stored) {
                logger::warn(
                    "DragonBoardVR: VRIK assigned the initial proxy to slot {}, but the physical board could not be returned to inventory.",
                    assignedSlotIndex + 1);
                ClearPendingInitialHolster(true);
                return;
            }

            CaptureHolsterAnchor(assignedSlotIndex);
            logger::info(
                "DragonBoardVR: initial physical board stored after VRIK assigned proxy slot {}; the board is ready to be drawn again.",
                assignedSlotIndex + 1);
            ClearPendingInitialHolster(false);
            return;
        }

        ++_pendingInitialHolsterFrames;
        if (_pendingInitialHolsterFrames >= kInitialHolsterAssignmentTimeoutFrames) {
            logger::info(
                "DragonBoardVR: initial physical board was released without a VRIK slot assignment; leaving it in the world.");
            ClearPendingInitialHolster(true);
        }
    }

    void VrikBoardProxyController::ClearPendingInitialHolster(
        bool clearEquippedProxy)
    {
        _pendingInitialHolsterReference.reset();
        _pendingInitialHolsterFrames = 0;
        _pendingInitialHolsterStashed = false;

        if (!clearEquippedProxy) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (player && equipManager && _proxyBaseForm &&
            IsProxyEquipped(_activeHandLeft)) {
            equipManager->UnequipObject(
                player,
                _proxyBaseForm,
                nullptr,
                1,
                GetHandEquipSlot(_activeHandLeft));
        }
        _activeHandLeft = false;
    }

    std::int32_t VrikBoardProxyController::FindAssignedProxySlot() const
    {
        if (!_proxyBaseForm) {
            return -1;
        }

        const auto normalizePath = [](const char* rawPath) {
            std::string normalized = rawPath ? rawPath : "";
            std::ranges::transform(
                normalized,
                normalized.begin(),
                [](unsigned char value) {
                    if (value == '/') {
                        return '\\';
                    }
                    return static_cast<char>(std::tolower(value));
                });
            constexpr std::string_view meshesPrefix = "meshes\\";
            if (normalized.starts_with(meshesPrefix)) {
                normalized.erase(0, meshesPrefix.size());
            }
            return normalized;
        };

        const auto proxyModelPath = normalizePath(_proxyBaseForm->GetModel());
        if (proxyModelPath.empty()) {
            return -1;
        }

        std::int32_t firstMatchingSlot = -1;
        std::int32_t firstDisplayedSlot = -1;
        bool cachedSlotMatches = false;
        bool cachedSlotDisplayed = false;
        for (std::size_t index = 0; index < _slotArtObjects.size(); ++index) {
            const auto* artObject = _slotArtObjects[index];
            if (!artObject || normalizePath(artObject->GetModel()) != proxyModelPath) {
                continue;
            }

            const auto slotIndex = static_cast<std::int32_t>(index);
            if (firstMatchingSlot < 0) {
                firstMatchingSlot = slotIndex;
            }

            const auto* slotGlobal = _slotGlobals[index];
            const bool slotDisplayed = slotGlobal && slotGlobal->value > 0.5f;
            if (slotDisplayed && firstDisplayedSlot < 0) {
                firstDisplayedSlot = slotIndex;
            }
            if (slotIndex == _holsterAnchorSlotIndex) {
                cachedSlotMatches = true;
                cachedSlotDisplayed = slotDisplayed;
            }
        }

        if (cachedSlotDisplayed) {
            return _holsterAnchorSlotIndex;
        }
        if (firstDisplayedSlot >= 0) {
            return firstDisplayedSlot;
        }
        return cachedSlotMatches ? _holsterAnchorSlotIndex : firstMatchingSlot;
    }

    std::int32_t VrikBoardProxyController::FindNewlyDisplayedProxySlot() const
    {
        if (!_proxyBaseForm) {
            return -1;
        }

        const auto normalizePath = [](const char* rawPath) {
            std::string normalized = rawPath ? rawPath : "";
            std::ranges::transform(
                normalized,
                normalized.begin(),
                [](unsigned char value) {
                    if (value == '/') {
                        return '\\';
                    }
                    return static_cast<char>(std::tolower(value));
                });
            constexpr std::string_view meshesPrefix = "meshes\\";
            if (normalized.starts_with(meshesPrefix)) {
                normalized.erase(0, meshesPrefix.size());
            }
            return normalized;
        };

        const auto proxyModelPath = normalizePath(_proxyBaseForm->GetModel());
        for (std::size_t index = 0; index < _slotArtObjects.size(); ++index) {
            if (static_cast<std::int32_t>(index) == _activeSlotIndex) {
                continue;
            }
            const auto* artObject = _slotArtObjects[index];
            const auto* slotGlobal = _slotGlobals[index];
            if (artObject && slotGlobal && slotGlobal->value > 0.5f &&
                !_slotDisplayedPrevious[index] &&
                normalizePath(artObject->GetModel()) == proxyModelPath) {
                return static_cast<std::int32_t>(index);
            }
        }
        return -1;
    }

    void VrikBoardProxyController::UpdateHolsterAnchor()
    {
        if (_activeSlotIndex >= 0 || _activePhysicalReference ||
            IsProxyEquipped(true) || IsProxyEquipped(false)) {
            return;
        }

        const auto assignedSlotIndex = FindAssignedProxySlot();
        if (assignedSlotIndex < 0) {
            if (_holsterAnchorSlotIndex >= 0) {
                ClearHolsterAnchor();
            }
            return;
        }
        CaptureHolsterAnchor(assignedSlotIndex);
    }

    void VrikBoardProxyController::CaptureHolsterAnchor(std::int32_t slotIndex)
    {
        if (slotIndex < 0) {
            return;
        }

        auto* skeletonRoot = vrui::VRUIHandTracking::getPlayerSkeletonRoot(true);
        auto* proxyRoot = skeletonRoot ?
            skeletonRoot->GetObjectByName("DragonBoardVrikProxy") : nullptr;
        if (!proxyRoot && skeletonRoot) {
            if (auto* boardVisual = skeletonRoot->GetObjectByName("DragonBoard")) {
                proxyRoot = boardVisual->parent;
            }
        }
        auto* proxyParent = proxyRoot && proxyRoot->parent ? proxyRoot->parent : nullptr;
        if (!proxyRoot || !proxyParent) {
            return;
        }

        std::string parentName = proxyParent->name.c_str();
        std::ranges::transform(
            parentName,
            parentName.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
        if (parentName.contains("weapon") ||
            parentName.contains("forearm") ||
            parentName.contains("hand") ||
            parentName.contains("wand")) {
            return;
        }

        auto* boardVisual = proxyRoot->GetObjectByName("DragonBoard");
        auto* anchorSource = boardVisual ? boardVisual : proxyRoot;
        auto* anchorParent = anchorSource->parent;
        if (!anchorParent) {
            return;
        }

        const bool anchorChanged = !_holsterAnchor ||
            _holsterAnchorSlotIndex != slotIndex ||
            _holsterAnchor->parent != anchorParent;
        if (!_holsterAnchor) {
            _holsterAnchor = RE::NiPointer<RE::NiNode>(RE::NiNode::Create(0));
            if (!_holsterAnchor) {
                return;
            }
            _holsterAnchor->name = "DragonBoardVRVrikHolsterAnchor";
        }

        if (_holsterAnchor->parent != anchorParent) {
            if (_holsterAnchor->parent) {
                _holsterAnchor->parent->DetachChild(_holsterAnchor.get());
            }
            anchorParent->AttachChild(_holsterAnchor.get(), true);
        }
        _holsterAnchor->local = anchorSource->local;
        if (_holsterVisual && _holsterVisual.get() != proxyRoot &&
            _activeSlotIndex < 0) {
            _holsterVisual->SetAppCulled(false);
        }
        _holsterVisual = RE::NiPointer<RE::NiAVObject>(proxyRoot);
        _holsterAnchorSlotIndex = slotIndex;
        if (_activeSlotIndex < 0) {
            _holsterVisual->SetAppCulled(false);
        }

        RE::NiUpdateData updateData;
        updateData.flags = RE::NiUpdateData::Flag::kDirty;
        _holsterAnchor->Update(updateData);

        if (anchorChanged) {
            logger::info(
                "DragonBoardVR: captured VRIK slot {} holster anchor on parent '{}' at ({:.2f}, {:.2f}, {:.2f}).",
                slotIndex + 1,
                proxyParent->name.c_str(),
                _holsterAnchor->world.translate.x,
                _holsterAnchor->world.translate.y,
                _holsterAnchor->world.translate.z);
        }
    }

    void VrikBoardProxyController::ClearHolsterAnchor()
    {
        if (_holsterVisual) {
            _holsterVisual->SetAppCulled(false);
        }
        if (_holsterAnchor && _holsterAnchor->parent) {
            _holsterAnchor->parent->DetachChild(_holsterAnchor.get());
        }
        _holsterAnchor.reset();
        _holsterVisual.reset();
        _holsterAnchorSlotIndex = -1;
    }

    bool VrikBoardProxyController::TryStorePhysicalBoardAtHolster(
        RE::TESObjectREFR* reference)
    {
        if (!reference || _activeSlotIndex < 0 || !_holsterAnchor ||
            _holsterAnchorSlotIndex != _activeSlotIndex ||
            !_holsterAnchor->parent || !_physicalBaseForm) {
            logger::warn(
                "DragonBoardVR: physical board release could not test VRIK reholster (reference={}, activeSlot={}, anchor={}, anchorSlot={}, anchorParent={}, physicalForm={}).",
                reference != nullptr,
                _activeSlotIndex + 1,
                static_cast<bool>(_holsterAnchor),
                _holsterAnchorSlotIndex + 1,
                _holsterAnchor && _holsterAnchor->parent != nullptr,
                _physicalBaseForm != nullptr);
            return false;
        }

        auto* boardRootObject = reference->Get3D();
        auto* boardRoot = boardRootObject ? boardRootObject->AsNode() : nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!boardRoot || !player) {
            logger::warn(
                "DragonBoardVR: physical board release could not test VRIK slot {} distance (boardRoot={}, player={}).",
                _activeSlotIndex + 1,
                boardRoot != nullptr,
                player != nullptr);
            return false;
        }

        auto* boardVisual = boardRoot->GetObjectByName("DragonBoard");
        const auto boardPosition = boardVisual ?
            boardVisual->world.translate : boardRoot->world.translate;
        const auto boardDelta = boardPosition - _holsterAnchor->world.translate;
        const float boardDistanceSquared = boardDelta.SqrLength();

        const auto& manager = vrui::VRMenuManager::get();
        auto* handNode = _activeHandLeft ?
            vrui::VRUIHandTracking::getLeftHandNode(manager.isVRIKInstalled()) :
            vrui::VRUIHandTracking::getRightHandNode(manager.isVRIKInstalled());
        const auto handDelta = handNode ?
            handNode->world.translate - _holsterAnchor->world.translate : RE::NiPoint3{};
        const float handDistanceSquared = handNode ?
            handDelta.SqrLength() : boardDistanceSquared;
        const float distanceSquared =
            std::min(handDistanceSquared, boardDistanceSquared);
        if (!std::isfinite(distanceSquared) || !std::isfinite(boardDistanceSquared)) {
            logger::warn(
                "DragonBoardVR: physical board release produced a non-finite distance for VRIK slot {}.",
                _activeSlotIndex + 1);
            return false;
        }
        if (distanceSquared > kHolsterStoreDistanceSquared) {
            logger::info(
                "DragonBoardVR: physical board released with hand {:.2f} units and board {:.2f} units from VRIK slot {} (store radius {:.2f}); leaving it in the world.",
                std::sqrt(handDistanceSquared),
                std::sqrt(boardDistanceSquared),
                _activeSlotIndex + 1,
                kHolsterStoreDistance);
            return false;
        }

        const auto countBefore = player->GetItemCount(_physicalBaseForm);
        player->PickUpObject(reference, 1, false, false);
        if (player->GetItemCount(_physicalBaseForm) <= countBefore) {
            logger::warn(
                "DragonBoardVR: physical board reached VRIK slot {}, but Skyrim did not return it to inventory.",
                _activeSlotIndex + 1);
            return false;
        }

        const auto storedSlot = _activeSlotIndex + 1;
        NotifyPhysicalBoardStored();
        logger::info(
            "DragonBoardVR: physical board reholstered into VRIK slot {} (handDistance={:.2f}, boardDistance={:.2f}).",
            storedSlot,
            std::sqrt(handDistanceSquared),
            std::sqrt(boardDistanceSquared));
        return true;
    }

    bool VrikBoardProxyController::IsProxyEquipped(bool isLeft) const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* equipped = player ? player->GetEquippedObject(isLeft) : nullptr;
        return equipped && _proxyBaseForm && equipped == _proxyBaseForm;
    }

    bool VrikBoardProxyController::IsHandNearHolster(
        std::size_t slotIndex,
        bool isLeft,
        float& distance) const
    {
        distance = -1.0f;
        if (!_holsterAnchor || !_holsterAnchor->parent ||
            _holsterAnchorSlotIndex != static_cast<std::int32_t>(slotIndex)) {
            return false;
        }

        const auto& manager = vrui::VRMenuManager::get();
        auto* skeletonRoot = vrui::VRUIHandTracking::getPlayerSkeletonRoot(
            manager.isVRIKInstalled());
        const char* handBoneName =
            isLeft ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]";
        const char* fingerMiddleName =
            isLeft ? "NPC L Finger11 [LF11]" : "NPC R Finger11 [RF11]";
        const char* fingerTipName =
            isLeft ? "NPC L Finger12 [LF12]" : "NPC R Finger12 [RF12]";
        const auto resolveSkeletonNode = [skeletonRoot](const char* name) {
            auto* object = skeletonRoot ? skeletonRoot->GetObjectByName(name) : nullptr;
            return object ? object->AsNode() : nullptr;
        };
        const std::array<RE::NiNode*, 4> handNodes{
            resolveSkeletonNode(handBoneName),
            resolveSkeletonNode(fingerMiddleName),
            resolveSkeletonNode(fingerTipName),
            GetControllerTrackingNode(isLeft) };

        const auto& rotation = _holsterAnchor->world.rotate;
        const float scale = std::abs(_holsterAnchor->world.scale);
        if (!std::isfinite(scale) || scale <= 0.0001f) {
            return false;
        }

        float nearestDistance = std::numeric_limits<float>::max();
        bool foundTrackingPoint = false;
        for (const auto* handNode : handNodes) {
            if (!handNode) {
                continue;
            }
            const auto worldDelta =
                handNode->world.translate - _holsterAnchor->world.translate;
            const RE::NiPoint3 localPoint{
                (rotation.entry[0][0] * worldDelta.x +
                 rotation.entry[1][0] * worldDelta.y +
                 rotation.entry[2][0] * worldDelta.z) / scale,
                (rotation.entry[0][1] * worldDelta.x +
                 rotation.entry[1][1] * worldDelta.y +
                 rotation.entry[2][1] * worldDelta.z) / scale,
                (rotation.entry[0][2] * worldDelta.x +
                 rotation.entry[1][2] * worldDelta.y +
                 rotation.entry[2][2] * worldDelta.z) / scale };
            const RE::NiPoint3 outsideDistance{
                std::max(
                    0.0f, std::abs(localPoint.x) - kHolsterBoardHalfExtents.x),
                std::max(
                    0.0f, std::abs(localPoint.y) - kHolsterBoardHalfExtents.y),
                std::max(
                    0.0f, std::abs(localPoint.z) - kHolsterBoardHalfExtents.z) };
            const float localDistanceSquared = outsideDistance.SqrLength();
            if (!std::isfinite(localDistanceSquared)) {
                continue;
            }
            nearestDistance = std::min(
                nearestDistance, std::sqrt(localDistanceSquared) * scale);
            foundTrackingPoint = true;
        }

        if (!foundTrackingPoint) {
            return false;
        }
        distance = nearestDistance;
        return distance <= kHolsterDrawDistance;
    }

    void VrikBoardProxyController::BeginPhysicalDraw(
        std::size_t slotIndex,
        bool drawHandLeft,
        bool proxyHandLeft)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        if (!player || !equipManager || !_physicalBaseForm || !_proxyBaseForm ||
            !g_higgsInterface) {
            return;
        }

        if (player->GetItemCount(_physicalBaseForm) <= 0) {
            logger::warn(
                "DragonBoardVR: VRIK proxy was drawn from slot {}, but no physical DragonBoard is available.",
                slotIndex + 1);
            return;
        }

        ClearHolsterDrawCandidate();
        _activeSlotIndex = static_cast<std::int32_t>(slotIndex);
        _activeHandLeft = drawHandLeft;
        _holsterReassignmentArmed = false;
        _holsterReassignmentCandidate = -1;
        _holsterReassignmentStableFrames = 0;
        _holsterReassignmentWaitFrames = 0;
        SetSourceSlotVisible(false);
        SetEquippedProxyVisible(proxyHandLeft, false);

        if (!g_higgsInterface->IsWeaponCollisionDisabled(proxyHandLeft)) {
            g_higgsInterface->DisableWeaponCollision(proxyHandLeft);
            _proxyWeaponCollisionDisabled = true;
            _proxyWeaponCollisionHandLeft = proxyHandLeft;
        }

        equipManager->UnequipObject(
            player,
            _proxyBaseForm,
            nullptr,
            1,
            GetHandEquipSlot(proxyHandLeft));

        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([drawHandLeft]() {
                GetSingleton().SpawnAndGrabPhysicalBoard(drawHandLeft);
            });
        }

        logger::info(
            "DragonBoardVR: VRIK slot {} proxy equipped in {} hand; physical board draw assigned to initiating {} hand.",
            slotIndex + 1,
            proxyHandLeft ? "left" : "right",
            drawHandLeft ? "left" : "right");
    }

    void VrikBoardProxyController::BeginUnassignedPhysicalDraw(bool isLeft)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !_physicalBaseForm || !g_higgsInterface ||
            _activePhysicalReference || _pendingInitialHolsterReference) {
            return;
        }

        if (player->GetItemCount(_physicalBaseForm) <= 0) {
            logger::warn(
                "DragonBoardVR: unassigned VRIK proxy was equipped, but no physical DragonBoard is available.");
            return;
        }

        _activeSlotIndex = -1;
        _activeHandLeft = isLeft;
        SetEquippedProxyVisible(isLeft, true);

        logger::info(
            "DragonBoardVR: unassigned VRIK proxy equipped with {} hand; keeping the visible weapon-safe proxy in hand for initial holster assignment without spawning a physical board.",
            isLeft ? "left" : "right");
    }

    void VrikBoardProxyController::SpawnAndGrabPhysicalBoard(bool isLeft)
    {
        if (_proxyWeaponCollisionDisabled && g_higgsInterface &&
            !IsProxyEquipped(_proxyWeaponCollisionHandLeft)) {
            g_higgsInterface->EnableWeaponCollision(
                _proxyWeaponCollisionHandLeft);
            _proxyWeaponCollisionDisabled = false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !_physicalBaseForm || !g_higgsInterface) {
            SetSourceSlotVisible(true);
            _activeSlotIndex = -1;
            return;
        }

        const auto countBefore = player->GetItemCount(_physicalBaseForm);
        if (countBefore <= 0) {
            SetSourceSlotVisible(true);
            _activeSlotIndex = -1;
            return;
        }

        player->RemoveItem(
            _physicalBaseForm,
            1,
            RE::ITEM_REMOVE_REASON::kRemove,
            nullptr,
            nullptr);
        if (player->GetItemCount(_physicalBaseForm) >= countBefore) {
            SetSourceSlotVisible(true);
            _activeSlotIndex = -1;
            logger::warn(
                "DragonBoardVR: Skyrim rejected removal of the physical board for VRIK draw.");
            return;
        }

        auto reference = player->PlaceObjectAtMe(_physicalBaseForm, false);
        if (!reference) {
            player->AddObjectToContainer(_physicalBaseForm, nullptr, 1, nullptr);
            SetSourceSlotVisible(true);
            _activeSlotIndex = -1;
            logger::error(
                "DragonBoardVR: failed to spawn the physical board for VRIK draw.");
            return;
        }

        reference->SetOwner(player->GetActorBase());
        dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton().
            PrepareSpawnedReference(reference.get());

        const auto& manager = vrui::VRMenuManager::get();
        auto* handNode = isLeft ?
            vrui::VRUIHandTracking::getLeftHandNode(manager.isVRIKInstalled()) :
            vrui::VRUIHandTracking::getRightHandNode(manager.isVRIKInstalled());
        if (handNode) {
            float pitch = 0.0f;
            float yaw = 0.0f;
            float roll = 0.0f;
            vrui::VRUILayoutManager::getMatrixEuler(
                handNode->world.rotate,
                pitch,
                yaw,
                roll);
            const auto& settings = vrui::VRUISettings::get();
            const float boardScale =
                (std::max)(0.001f, settings.backgroundScale);
            const float gripEdgeX =
                (isLeft ? -1.0f : 1.0f) *
                kHolsterBoardHalfExtents.x * boardScale;
            const RE::NiPoint3 gripPointLocal{
                settings.backgroundOffsetX + gripEdgeX,
                settings.backgroundOffsetY,
                settings.backgroundOffsetZ
            };
            const auto spawnPosition = handNode->world.translate -
                handNode->world.rotate * gripPointLocal;
            (void)dragonboard::runtime::vr::SetReferenceTransform(
                reference.get(),
                spawnPosition,
                { pitch, yaw, roll });
            logger::info(
                "DragonBoardVR: spawned physical board with the {} hand centered on the lateral edge (localGrip=({:.2f}, {:.2f}, {:.2f})).",
                isLeft ? "left" : "right",
                gripPointLocal.x,
                gripPointLocal.y,
                gripPointLocal.z);
        }

        _activePhysicalReference = reference->CreateRefHandle();
        const auto handle = _activePhysicalReference;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([handle, isLeft]() {
                const auto resolved = handle.get();
                if (resolved && g_higgsInterface &&
                    g_higgsInterface->CanGrabObject(resolved.get(), isLeft)) {
                    g_higgsInterface->GrabObject(resolved.get(), isLeft);
                    return;
                }

                if (auto* retryTasks = SKSE::GetTaskInterface()) {
                    retryTasks->AddTask([handle, isLeft]() {
                        const auto retryReference = handle.get();
                        if (retryReference && g_higgsInterface &&
                            g_higgsInterface->CanGrabObject(retryReference.get(), isLeft)) {
                            g_higgsInterface->GrabObject(retryReference.get(), isLeft);
                            return;
                        }

                        auto& controller = GetSingleton();
                        auto* retryPlayer = RE::PlayerCharacter::GetSingleton();
                        if (retryReference && retryPlayer) {
                            retryPlayer->PickUpObject(
                                retryReference.get(),
                                1,
                                false,
                                false);
                        }
                        controller.SetSourceSlotVisible(true);
                        controller._activeSlotIndex = -1;
                        controller._activePhysicalReference.reset();
                        logger::warn(
                            "DragonBoardVR: HIGGS could not grab the board drawn from VRIK; it was returned to inventory.");
                    });
                }
            });
        }
    }

    void VrikBoardProxyController::SetSourceSlotVisible(bool visible)
    {
        if (_activeSlotIndex < 0 ||
            static_cast<std::size_t>(_activeSlotIndex) >= _slotGlobals.size()) {
            return;
        }

        if (_holsterVisual && _holsterAnchorSlotIndex == _activeSlotIndex) {
            _holsterVisual->SetAppCulled(!visible);
        }
    }

    void VrikBoardProxyController::SetEquippedProxyVisible(
        bool isLeft,
        bool visible)
    {
        const auto& manager = vrui::VRMenuManager::get();
        auto* handNode = isLeft ?
            vrui::VRUIHandTracking::getLeftHandNode(manager.isVRIKInstalled()) :
            vrui::VRUIHandTracking::getRightHandNode(manager.isVRIKInstalled());
        auto* proxyRoot = handNode ?
            handNode->GetObjectByName("DragonBoardVrikProxy") : nullptr;
        if (proxyRoot && proxyRoot != _holsterVisual.get()) {
            proxyRoot->SetAppCulled(!visible);
        }
    }

    void VrikBoardProxyController::NotifyPhysicalBoardGrabbed(
        RE::TESObjectREFR* reference,
        bool isLeft)
    {
        if (!reference) {
            return;
        }

        if (_activePhysicalReference) {
            const auto activeReference = _activePhysicalReference.get();
            if (activeReference && activeReference.get() != reference &&
                _physicalBoardHeld) {
                return;
            }
        }

        if (_activeSlotIndex < 0) {
            auto assignedSlotIndex = FindAssignedProxySlot();
            if (assignedSlotIndex < 0) {
                assignedSlotIndex = _holsterAnchorSlotIndex;
            }
            if (assignedSlotIndex >= 0) {
                _activeSlotIndex = assignedSlotIndex;
                CaptureHolsterAnchor(assignedSlotIndex);
                SetSourceSlotVisible(false);
                logger::info(
                    "DragonBoardVR: world physical board {:08X} adopted persistent VRIK slot {}.",
                    reference->GetFormID(),
                    assignedSlotIndex + 1);
            }
        }

        ClearHolsterDrawCandidate();
        _activePhysicalReference = reference->CreateRefHandle();
        _activeHandLeft = isLeft;
        _physicalBoardHeld = true;
        const auto suppressedSlot = _activeSlotIndex >= 0 ?
            _activeSlotIndex : _holsterAnchorSlotIndex;
        SetHolsterSlotSuppressed(suppressedSlot, true);
    }

    void VrikBoardProxyController::NotifyPhysicalBoardReleased(
        RE::TESObjectREFR* reference,
        bool stashed)
    {
        if (reference && _activePhysicalReference) {
            const auto activeReference = _activePhysicalReference.get();
            if (activeReference && activeReference.get() != reference) {
                return;
            }
        } else if (_activeSlotIndex < 0) {
            return;
        }

        _physicalBoardHeld = false;
        SetHolsterSlotSuppressed(-1, false);
        if (_activeSlotIndex >= 0) {
            if (!stashed && TryStorePhysicalBoardAtHolster(reference)) {
                return;
            }
            if (stashed) {
                NotifyPhysicalBoardStored();
                return;
            }

            ReleasePhysicalBoardToWorld();
            return;
        }

        _activePhysicalReference.reset();
        if (reference) {
            _pendingInitialHolsterReference = reference->CreateRefHandle();
        }
        _pendingInitialHolsterFrames = 0;
        _pendingInitialHolsterStashed = stashed;

        if (!_pendingInitialHolsterReference && !stashed) {
            ClearPendingInitialHolster(true);
            return;
        }

        logger::info(
            "DragonBoardVR: initial physical board {} while waiting for VRIK to publish its assigned slot.",
            stashed ? "stored" : "released");
    }

    void VrikBoardProxyController::ReleasePhysicalBoardToWorld()
    {
        SetHolsterSlotSuppressed(-1, false);
        const auto releasedSlot = _activeSlotIndex + 1;
        SetSourceSlotVisible(true);
        _activeSlotIndex = -1;
        _activeHandLeft = false;
        _physicalBoardHeld = false;
        _activePhysicalReference.reset();
        _holsterReassignmentArmed = false;
        _holsterReassignmentCandidate = -1;
        _holsterReassignmentStableFrames = 0;
        _holsterReassignmentWaitFrames = 0;
        logger::info(
            "DragonBoardVR: physical board left in the world; persistent VRIK slot {} proxy restored for another inventory board.",
            releasedSlot);
    }

    void VrikBoardProxyController::NotifyPhysicalBoardStored()
    {
        SetHolsterSlotSuppressed(-1, false);
        _holsterReassignmentArmed = false;
        _holsterReassignmentCandidate = -1;
        _holsterReassignmentStableFrames = 0;
        _holsterReassignmentWaitFrames = 0;
        if (_activeSlotIndex < 0) {
            if (!_activePhysicalReference) {
                return;
            }

            _physicalBoardHeld = false;
            _activePhysicalReference.reset();
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* equipManager = RE::ActorEquipManager::GetSingleton();
            if (player && equipManager && _proxyBaseForm &&
                IsProxyEquipped(_activeHandLeft)) {
                equipManager->UnequipObject(
                    player,
                    _proxyBaseForm,
                    nullptr,
                    1,
                    GetHandEquipSlot(_activeHandLeft));
            }
            logger::info(
                "DragonBoardVR: initial physical board stored outside an assigned VRIK slot; invisible proxy cleared from the hand.");
            return;
        }

        const auto restoredSlot = _activeSlotIndex + 1;
        SetSourceSlotVisible(true);
        _activeSlotIndex = -1;
        _activeHandLeft = false;
        _physicalBoardHeld = false;
        _activePhysicalReference.reset();
        logger::info(
            "DragonBoardVR: physical board stored; VRIK slot {} proxy restored.",
            restoredSlot);
    }

    RE::BSEventNotifyControl VrikBoardProxyController::ProcessEvent(
        const RE::TESEquipEvent* event,
        RE::BSTEventSource<RE::TESEquipEvent>*)
    {
        if (!event || !_proxyBaseForm ||
            event->baseObject != _proxyBaseForm->GetFormID()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || event->actor.get() != player) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const bool dragonBoardInventoryEquip =
            event->equipped && _dragonBoardInventoryEquipPending &&
            _dragonBoardInventoryEquipFrames > 0;
        const bool dragonBoardInventoryEquipHandLeft =
            _dragonBoardInventoryEquipHandLeft;
        if (dragonBoardInventoryEquip) {
            _dragonBoardInventoryEquipPending = false;
            _dragonBoardInventoryEquipFrames = 0;
        }
        const bool originalInventoryEquip =
            event->equipped && IsOriginalInventoryMenuOpen();

        const bool physicalBoardActive =
            _activeSlotIndex >= 0 || static_cast<bool>(_activePhysicalReference);
        if (event->equipped &&
            (physicalBoardActive || _holsterAnchorSlotIndex >= 0)) {
            if (physicalBoardActive) {
                const bool allowedInventoryEquip =
                    dragonBoardInventoryEquip || originalInventoryEquip;
                const bool leftProxyEquipped = IsProxyEquipped(true);
                const bool rightProxyEquipped = IsProxyEquipped(false);
                if (!allowedInventoryEquip) {
                    _holsterReassignmentArmed = false;
                    _holsterReassignmentCandidate = -1;
                    _holsterReassignmentStableFrames = 0;
                    _holsterReassignmentWaitFrames = 0;
                    SetEquippedProxyVisible(true, false);
                    SetEquippedProxyVisible(false, false);
                    if (auto* equipManager =
                            RE::ActorEquipManager::GetSingleton()) {
                        equipManager->UnequipObject(
                            player,
                            _proxyBaseForm,
                            nullptr,
                            1,
                            GetHandEquipSlot(true));
                        equipManager->UnequipObject(
                            player,
                            _proxyBaseForm,
                            nullptr,
                            1,
                            GetHandEquipSlot(false));
                    }
                    logger::info(
                        "DragonBoardVR: rejected proxy equip because a physical DragonBoard is already active (leftEquipped={}, rightEquipped={}); other VRIK holsters remain enabled.",
                        leftProxyEquipped,
                        rightProxyEquipped);
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (dragonBoardInventoryEquip) {
                    _activeHandLeft = dragonBoardInventoryEquipHandLeft;
                } else if (leftProxyEquipped != rightProxyEquipped) {
                    _activeHandLeft = leftProxyEquipped;
                } else {
                    logger::warn(
                        "DragonBoardVR: original inventory proxy equip hand was ambiguous while the physical board was active; leaving the proxy equipped without changing holster state.");
                    return RE::BSEventNotifyControl::kContinue;
                }

                ClearHolsterDrawCandidate();
                _holsterReassignmentArmed = true;
                _holsterReassignmentCandidate = -1;
                _holsterReassignmentStableFrames = 0;
                _holsterReassignmentWaitFrames = 0;
                SetEquippedProxyVisible(_activeHandLeft, true);
                if (g_higgsInterface &&
                    !g_higgsInterface->IsWeaponCollisionDisabled(_activeHandLeft)) {
                    g_higgsInterface->DisableWeaponCollision(_activeHandLeft);
                    _proxyWeaponCollisionDisabled = true;
                    _proxyWeaponCollisionHandLeft = _activeHandLeft;
                }
                logger::info(
                    "DragonBoardVR: allowed {} proxy equip in the {} hand while the physical board is active; keeping the proxy equipped only for holster reassignment.",
                    dragonBoardInventoryEquip ? "DragonBoard inventory" :
                        "original InventoryMenu",
                    _activeHandLeft ? "left" : "right");
                return RE::BSEventNotifyControl::kContinue;
            }

            _holsterReassignmentArmed = true;
            _holsterReassignmentCandidate = -1;
            _holsterReassignmentStableFrames = 0;
            _holsterReassignmentWaitFrames = 0;
            logger::info(
                "DragonBoardVR: shared VRIK holster reassignment armed (physicalActive={}, currentSlot={}).",
                physicalBoardActive,
                (_activeSlotIndex >= 0 ? _activeSlotIndex :
                    _holsterAnchorSlotIndex) + 1);
            if (physicalBoardActive) {
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        if (!event->equipped) {
            if (_proxyWeaponCollisionDisabled && g_higgsInterface) {
                g_higgsInterface->EnableWeaponCollision(
                    _proxyWeaponCollisionHandLeft);
                _proxyWeaponCollisionDisabled = false;
                logger::info(
                    "DragonBoardVR: restored {} weapon collision after proxy unequip.",
                    _proxyWeaponCollisionHandLeft ? "left" : "right");
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (_activeSlotIndex >= 0 || _activePhysicalReference ||
            _pendingInitialHolsterReference) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (dragonBoardInventoryEquip) {
            _activeHandLeft = dragonBoardInventoryEquipHandLeft;
        } else {
            const bool leftEquipped = IsProxyEquipped(true);
            const bool rightEquipped = IsProxyEquipped(false);
            if (leftEquipped == rightEquipped) {
                logger::warn(
                    "DragonBoardVR: VRIK proxy equip hand was ambiguous; physical conversion skipped.");
                return RE::BSEventNotifyControl::kContinue;
            }

            _activeHandLeft = leftEquipped;
        }
        const auto rejectProxyEquip = [this, player]() {
            SetEquippedProxyVisible(_activeHandLeft, false);
            if (auto* equipManager = RE::ActorEquipManager::GetSingleton()) {
                equipManager->UnequipObject(
                    player,
                    _proxyBaseForm,
                    nullptr,
                    1,
                    GetHandEquipSlot(_activeHandLeft));
            }
        };
        const auto prepareVisibleProxy = [this]() {
            if (g_higgsInterface &&
                !g_higgsInterface->IsWeaponCollisionDisabled(_activeHandLeft)) {
                g_higgsInterface->DisableWeaponCollision(_activeHandLeft);
                _proxyWeaponCollisionDisabled = true;
                _proxyWeaponCollisionHandLeft = _activeHandLeft;
            }
        };

        if (dragonBoardInventoryEquip) {
            ClearHolsterDrawCandidate();
            _holsterReassignmentArmed = true;
            _holsterReassignmentCandidate = -1;
            _holsterReassignmentStableFrames = 0;
            _holsterReassignmentWaitFrames = 0;
            prepareVisibleProxy();
            BeginUnassignedPhysicalDraw(_activeHandLeft);
            logger::info(
                "DragonBoardVR: DragonBoard inventory equipped the proxy in the {} hand; keeping the WEAP visible without spawning the physical MISC.",
                _activeHandLeft ? "left" : "right");
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto assignedSlotIndex = FindAssignedProxySlot();
        if (assignedSlotIndex >= 0) {
            const auto slotArrayIndex = static_cast<std::size_t>(assignedSlotIndex);
            const auto* slotGlobal = _slotGlobals[slotArrayIndex];
            const bool slotWasDisplayed = slotGlobal && slotGlobal->value > 0.5f;
            float leftHandDistance = -1.0f;
            float rightHandDistance = -1.0f;
            (void)IsHandNearHolster(
                slotArrayIndex, true, leftHandDistance);
            (void)IsHandNearHolster(
                slotArrayIndex, false, rightHandDistance);
            const bool directEquipGuard =
                IsDirectEquipMenuOpen() || _directEquipGraceFrames > 0;
            if (directEquipGuard) {
                ClearHolsterDrawCandidate();
                prepareVisibleProxy();
                logger::info(
                    "DragonBoardVR: proxy equip treated as direct equipment (slot={}, leftDistance={:.2f}, rightDistance={:.2f}, drawRadius={:.2f}, slotDisplayed={}); keeping the WEAP visible for holster assignment.",
                    assignedSlotIndex + 1,
                    leftHandDistance,
                    rightHandDistance,
                    kHolsterDrawDistance,
                    slotWasDisplayed);
                BeginUnassignedPhysicalDraw(_activeHandLeft);
            } else {
                logger::info(
                    "DragonBoardVR: accepting VRIK slot {} draw in the {} equip hand (leftDistanceAfterPull={:.2f}, rightDistanceAfterPull={:.2f}); custom proximity and pull direction ignored.",
                    assignedSlotIndex + 1,
                    _activeHandLeft ? "left" : "right",
                    leftHandDistance,
                    rightHandDistance);
                BeginPhysicalDraw(
                    slotArrayIndex, _activeHandLeft, _activeHandLeft);
            }
        } else if (IsDirectEquipMenuOpen() || _directEquipGraceFrames > 0) {
            prepareVisibleProxy();
            BeginUnassignedPhysicalDraw(_activeHandLeft);
        } else {
            rejectProxyEquip();
            logger::info(
                "DragonBoardVR: rejected unassigned proxy equip outside direct inventory assignment.");
        }
        return RE::BSEventNotifyControl::kContinue;
    }

}
