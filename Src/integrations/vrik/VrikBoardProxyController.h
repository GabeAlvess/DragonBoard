#pragma once

#include <RE/B/BSTEvent.h>
#include <RE/B/BSPointerHandle.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiPoint3.h>
#include <RE/T/TESEquipEvent.h>

#include <array>

namespace RE
{
    class BGSArtObject;
    class NiAVObject;
    class TESForm;
    class TESGlobal;
    class TESObjectMISC;
    class TESObjectREFR;
    class TESObjectWEAP;
}

namespace dragonboard::integrations::vrik
{
    class VrikBoardProxyController final : public RE::BSTEventSink<RE::TESEquipEvent>
    {
    public:
        static VrikBoardProxyController& GetSingleton();

        void Initialize();
        void RefreshConfiguredForms();
        void Reset();
        void Update();
        void PrepareDragonBoardInventoryEquip(RE::TESForm* form, bool isLeft);
        void NotifyGripInput(bool isLeft, bool pressed);
        void NotifyPhysicalBoardGrabbed(RE::TESObjectREFR* reference, bool isLeft);
        void NotifyPhysicalBoardReleased(RE::TESObjectREFR* reference, bool stashed);
        void NotifyPhysicalBoardStored();

    protected:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESEquipEvent* event,
            RE::BSTEventSource<RE::TESEquipEvent>* eventSource) override;

    private:
        void ResolveVrikSlotForms();
        void EnsureProxyInventory();
        void ClearHolsterDrawCandidate();
        void ProcessPendingInitialHolster();
        void ClearPendingInitialHolster(bool clearEquippedProxy);
        void BeginPhysicalDraw(
            std::size_t slotIndex,
            bool drawHandLeft,
            bool proxyHandLeft);
        void BeginUnassignedPhysicalDraw(bool isLeft);
        void SpawnAndGrabPhysicalBoard(bool isLeft);
        void UpdateHolsterAnchor();
        void CaptureHolsterAnchor(std::int32_t slotIndex);
        void ClearHolsterAnchor();
        [[nodiscard]] bool TryStorePhysicalBoardAtHolster(RE::TESObjectREFR* reference);
        void ReleasePhysicalBoardToWorld();
        void SetSourceSlotVisible(bool visible);
        void SetEquippedProxyVisible(bool isLeft, bool visible);
        [[nodiscard]] std::int32_t FindAssignedProxySlot() const;
        [[nodiscard]] std::int32_t FindNewlyDisplayedProxySlot() const;
        [[nodiscard]] bool IsProxyEquipped(bool isLeft) const;
        [[nodiscard]] bool IsHandNearHolster(
            std::size_t slotIndex,
            bool isLeft,
            float& distance) const;

        bool _initialized = false;
        bool _formsResolved = false;
        std::string _cachedPlugin;
        std::uint32_t _cachedPhysicalLocalFormID = 0;
        std::uint32_t _cachedProxyLocalFormID = 0;
        RE::TESObjectMISC* _physicalBaseForm = nullptr;
        RE::TESObjectWEAP* _proxyBaseForm = nullptr;
        std::array<RE::TESGlobal*, 14> _slotGlobals{};
        std::array<RE::BGSArtObject*, 14> _slotArtObjects{};
        std::array<bool, 14> _slotDisplayedPrevious{};
        std::int32_t _activeSlotIndex = -1;
        bool _activeHandLeft = false;
        std::uint32_t _directEquipGraceFrames = 0;
        bool _dragonBoardInventoryEquipPending = false;
        bool _dragonBoardInventoryEquipHandLeft = false;
        std::uint32_t _dragonBoardInventoryEquipFrames = 0;
        std::int32_t _holsterDrawCandidateSlot = -1;
        bool _holsterDrawCandidateHandLeft = false;
        std::uint32_t _holsterDrawCandidateFrames = 0;
        bool _holsterReassignmentArmed = false;
        std::int32_t _holsterReassignmentCandidate = -1;
        std::uint32_t _holsterReassignmentStableFrames = 0;
        std::uint32_t _holsterReassignmentWaitFrames = 0;
        bool _proxyWeaponCollisionDisabled = false;
        bool _proxyWeaponCollisionHandLeft = false;
        bool _physicalBoardHeld = false;
        RE::ObjectRefHandle _activePhysicalReference;
        RE::ObjectRefHandle _pendingInitialHolsterReference;
        std::uint32_t _pendingInitialHolsterFrames = 0;
        bool _pendingInitialHolsterStashed = false;
        RE::NiPointer<RE::NiNode> _holsterAnchor;
        RE::NiPointer<RE::NiAVObject> _holsterVisual;
        std::int32_t _holsterAnchorSlotIndex = -1;
    };
}
