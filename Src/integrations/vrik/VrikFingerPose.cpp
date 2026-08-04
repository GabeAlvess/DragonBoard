#include "pch.h"
#include "VrikFingerPose.h"

#include "higgsinterface001.h"

#include <array>
#include <atomic>
#include <format>
#include <string_view>

namespace dragonboard::integrations::vrik
{
    namespace
    {
        class IVrikInterface001
        {
        public:
            virtual unsigned int getBuildNumber() = 0;
            virtual double getSettingDouble(const char* name) = 0;
            virtual void setSettingDouble(const char* name, double value) = 0;
            virtual void getSettingString(
                const char* name, char* buffer, std::size_t bufferSize) = 0;
            virtual void setSettingString(const char* name, const char* value) = 0;
            virtual void saveSettings() = 0;
            virtual void restoreSettings() = 0;
            using GestureCallback = void (*)(int pressCount);
            virtual void addGestureAction(
                GestureCallback callback, const char* mcmMenuName) = 0;
            virtual void beginGestureProfile() = 0;
            virtual void setProfileAction(
                int gestureNumber, GestureCallback callback) = 0;
            virtual void endGestureProfile() = 0;
            virtual float getFingerPos(bool isLeft, int fingerIndex) = 0;
            virtual void setFingerRange(
                bool isLeft,
                float minThumb, float maxThumb,
                float minIndex, float maxIndex,
                float minMiddle, float maxMiddle,
                float minRing, float maxRing,
                float minPinky, float maxPinky) = 0;
            virtual void restoreFingers(bool isLeft) = 0;
        };

        struct VrikMessage
        {
            static constexpr std::uint32_t kMessageGetInterface = 0xF2AFAEE6;
            void* (*getApiFunction)(unsigned int revisionNumber) = nullptr;
        };

        IVrikInterface001* g_interface = nullptr;
        bool g_poseApplied = false;
        bool g_poseLeftHand = false;
        bool g_higgsSelectionPoseSuppressed = false;
        double g_higgsSelectionPoseMaxHandSpeed = 0.0;
        bool g_higgsCollisionFilterRegistered = false;
        std::int32_t g_suppressedHolsterSlot = -1;
        std::array<double, 6> g_previousHolsterSlotTypes{};
        std::atomic_uint8_t g_touchHandCollisionMask{ 0 };

        constexpr std::array<std::string_view, 6> kHolsterSlotTypeSettings{
            "allowSmallSlot",
            "allowMediumSlot",
            "allowLargeSlot",
            "allowRangedSlot",
            "allowShieldSlot",
            "allowTorchSlot" };

        constexpr std::uint8_t kRightHandMask = 1u << 0;
        constexpr std::uint8_t kLeftHandMask = 1u << 1;
        constexpr std::uint32_t kCollisionLayerMask = 0x7Fu;
        constexpr std::uint32_t kHiggsHandCollisionLayer = 56u;
        constexpr std::uint32_t kRagdollLayerMask = 0x1Fu;
        constexpr std::uint32_t kRightHandRagdollLayer = 3u;
        constexpr std::uint32_t kLeftHandRagdollLayer = 5u;

        bool IsHiggsHandCollision(std::uint32_t filterInfo, bool leftHand)
        {
            if ((filterInfo & kCollisionLayerMask) != kHiggsHandCollisionLayer) {
                return false;
            }
            const auto ragdollLayer =
                (filterInfo >> 8) & kRagdollLayerMask;
            return ragdollLayer == (leftHand ?
                kLeftHandRagdollLayer : kRightHandRagdollLayer);
        }

        HiggsPluginAPI::IHiggsInterface001::CollisionFilterComparisonResult
        IgnoreTouchHandCollision(
            void*,
            std::uint32_t filterInfoA,
            std::uint32_t filterInfoB)
        {
            using Result = HiggsPluginAPI::IHiggsInterface001::
                CollisionFilterComparisonResult;
            const auto mask =
                g_touchHandCollisionMask.load(std::memory_order_relaxed);
            if ((mask & kLeftHandMask) != 0 &&
                (IsHiggsHandCollision(filterInfoA, true) ||
                 IsHiggsHandCollision(filterInfoB, true))) {
                return Result::Ignore;
            }
            if ((mask & kRightHandMask) != 0 &&
                (IsHiggsHandCollision(filterInfoA, false) ||
                 IsHiggsHandCollision(filterInfoB, false))) {
                return Result::Ignore;
            }
            return Result::Continue;
        }
    }

    void Initialize()
    {
        if (g_interface) return;

        VrikMessage message;
        SKSE::GetMessagingInterface()->Dispatch(
            VrikMessage::kMessageGetInterface,
            &message,
            sizeof(VrikMessage*),
            "VRIK");
        if (!message.getApiFunction) {
            logger::info(
                "DragonBoardVR: VRIK interface unavailable; touch hand pose disabled.");
            return;
        }

        g_interface = static_cast<IVrikInterface001*>(message.getApiFunction(1));
        if (g_interface) {
            logger::info(
                "DragonBoardVR: VRIK interface obtained successfully. Build: {}",
                g_interface->getBuildNumber());
        }
    }

    void InitializeHiggsHandCollisionSuppression()
    {
        if (!g_higgsInterface || g_higgsCollisionFilterRegistered) {
            return;
        }
        g_higgsInterface->AddCollisionFilterComparisonCallback(
            &IgnoreTouchHandCollision);
        g_higgsCollisionFilterRegistered = true;
        logger::info(
            "DragonBoardVR: HIGGS touch-hand collision suppression registered; grab casts remain enabled.");
    }

    void SetHolsterSlotSuppressed(std::int32_t slotIndex, bool suppressed)
    {
        if (!g_interface) {
            return;
        }

        const auto restoreCurrentSlot = []() {
            if (g_suppressedHolsterSlot < 0) {
                return;
            }
            const auto slotNumber = g_suppressedHolsterSlot + 1;
            for (std::size_t index = 0;
                 index < kHolsterSlotTypeSettings.size();
                 ++index) {
                const auto settingName = std::format(
                    "{}{}", kHolsterSlotTypeSettings[index], slotNumber);
                g_interface->setSettingDouble(
                    settingName.c_str(), g_previousHolsterSlotTypes[index]);
            }
            logger::info(
                "DragonBoardVR: restored VRIK holster slot {} after physical board use.",
                slotNumber);
            g_suppressedHolsterSlot = -1;
        };

        if (!suppressed) {
            restoreCurrentSlot();
            return;
        }
        if (slotIndex < 0 || slotIndex >= 14 ||
            slotIndex == g_suppressedHolsterSlot) {
            return;
        }

        restoreCurrentSlot();
        const auto slotNumber = slotIndex + 1;
        for (std::size_t index = 0;
             index < kHolsterSlotTypeSettings.size();
             ++index) {
            const auto settingName = std::format(
                "{}{}", kHolsterSlotTypeSettings[index], slotNumber);
            g_previousHolsterSlotTypes[index] =
                g_interface->getSettingDouble(settingName.c_str());
            g_interface->setSettingDouble(settingName.c_str(), 0.0);
        }
        g_suppressedHolsterSlot = slotIndex;
        logger::info(
            "DragonBoardVR: temporarily disabled only VRIK holster slot {} while the physical board is active.",
            slotNumber);
    }

    void ApplyTouchPointingPose(bool leftHand)
    {
        if (!g_interface) return;

        if (g_poseApplied && g_poseLeftHand != leftHand) {
            RestoreTouchHandPose();
        }

        if (g_higgsInterface && !g_higgsSelectionPoseSuppressed &&
            g_higgsInterface->GetSettingDouble(
                "SelectedCloseFingerAnimMaxHandSpeed",
                g_higgsSelectionPoseMaxHandSpeed) &&
            g_higgsInterface->SetSettingDouble(
                "SelectedCloseFingerAnimMaxHandSpeed",
                -1.0)) {
            g_higgsSelectionPoseSuppressed = true;
            logger::trace(
                "DragonBoardVR: HIGGS proximity finger-opening animation suspended during touch.");
        }

        // VRIK finger values use 0 for closed and 1 for open. Keep a little
        // relaxation at both extremes so the pointing pose looks less rigid.
        g_interface->setFingerRange(
            leftHand,
            0.1f, 0.1f,
            0.9f, 0.9f,
            0.1f, 0.1f,
            0.1f, 0.1f,
            0.1f, 0.1f);
        g_touchHandCollisionMask.store(
            leftHand ? kLeftHandMask : kRightHandMask,
            std::memory_order_relaxed);
        g_poseApplied = true;
        g_poseLeftHand = leftHand;
    }

    void RestoreTouchHandPose()
    {
        g_touchHandCollisionMask.store(0, std::memory_order_relaxed);
        if (g_poseApplied && g_interface) {
            g_interface->restoreFingers(g_poseLeftHand);
        }
        if (g_higgsSelectionPoseSuppressed && g_higgsInterface) {
            g_higgsInterface->SetSettingDouble(
                "SelectedCloseFingerAnimMaxHandSpeed",
                g_higgsSelectionPoseMaxHandSpeed);
            logger::trace(
                "DragonBoardVR: HIGGS proximity finger-opening animation restored after touch.");
        }
        g_poseApplied = false;
        g_higgsSelectionPoseSuppressed = false;
    }
}
