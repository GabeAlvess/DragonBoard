#pragma once

#include <RE/B/BSPointerHandle.h>
#include <unordered_set>

namespace RE
{
    class NiNode;
    class TESForm;
    class TESObjectMISC;
    class TESObjectREFR;
}

namespace dragonboard::integrations::higgs
{
    class PhysicalBoardController
    {
    public:
        static PhysicalBoardController& GetSingleton();

        void Initialize();
        void RefreshConfiguredForm();
        void Reset();
        void Update();
        void PrepareSpawnedReference(RE::TESObjectREFR* reference);
        void UpdatePinnedItemGrabPriority(
            const void* owner,
            bool leftActive,
            bool rightActive);
        void PrepareHandForPinnedItemGrab(bool isLeft);
        [[nodiscard]] bool StoreHeldBoardBeforeOpeningMap();

    private:
        static void OnGrabbed(bool isLeft, RE::TESObjectREFR* reference);
        static void OnDropped(bool isLeft, RE::TESObjectREFR* reference);
        static void OnStashed(bool isLeft, RE::TESForm* baseForm);

        bool MatchesReference(RE::TESObjectREFR* reference);
        bool MatchesBaseForm(RE::TESForm* baseForm);
        void QueueGrab(bool isLeft, RE::TESObjectREFR* reference);
        void QueueRelease(RE::TESObjectREFR* reference, bool stashed);
        void Activate(bool isLeft, RE::TESObjectREFR* reference);
        void Deactivate(RE::TESObjectREFR* reference, bool stashed);
        void ApplyPinnedItemGrabPriority(bool isLeft);
        void ClearPinnedItemGrabPriority();
        void UpdateMapStoreRequest();
        void FinishMapStoreRequest(bool success);

        enum class MapStoreStage
        {
            None,
            WaitForHandDisable,
            WaitForInventory
        };

        bool _callbacksRegistered = false;
        std::string _cachedPlugin;
        std::uint32_t _cachedLocalFormID = 0;
        RE::TESObjectMISC* _configuredBaseForm = nullptr;
        RE::ObjectRefHandle _heldReference;
        bool _heldLeft = false;
        std::uint32_t _heldPlayerCellFormID = 0;
        std::uint32_t _missingHeldBoardAfterCellChangeFrames = 0;
        MapStoreStage _mapStoreStage = MapStoreStage::None;
        RE::ObjectRefHandle _mapStoreReference;
        RE::TESObjectMISC* _mapStoreBaseForm = nullptr;
        std::int32_t _mapStoreInitialCount = 0;
        std::uint32_t _mapStoreWaitFrames = 0;
        bool _mapStoreLeft = false;
        bool _mapStoreRestoreHand = false;
        std::unordered_set<const void*> _leftPinnedItemPriorityOwners;
        std::unordered_set<const void*> _rightPinnedItemPriorityOwners;
        bool _leftHandDisabledByPinnedItemPriority = false;
        bool _rightHandDisabledByPinnedItemPriority = false;
        bool _leftPinnedItemGrabBypass = false;
        bool _rightPinnedItemGrabBypass = false;
    };
}
