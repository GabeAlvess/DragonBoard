#include "pch.h"
#include "VrikFingerPose.h"

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

    void ApplyTouchPointingPose(bool leftHand)
    {
        if (!g_interface) return;
        if (g_poseApplied && g_poseLeftHand == leftHand) return;

        RestoreTouchHandPose();

        // VRIK finger values use 0 for closed and 1 for open. Keep a little
        // relaxation at both extremes so the pointing pose looks less rigid.
        g_interface->setFingerRange(
            leftHand,
            0.1f, 0.1f,
            0.9f, 0.9f,
            0.1f, 0.1f,
            0.1f, 0.1f,
            0.1f, 0.1f);
        g_poseApplied = true;
        g_poseLeftHand = leftHand;
    }

    void RestoreTouchHandPose()
    {
        if (!g_poseApplied) return;
        if (g_interface) {
            g_interface->restoreFingers(g_poseLeftHand);
        }
        g_poseApplied = false;
    }
}
