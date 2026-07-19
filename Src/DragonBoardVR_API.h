/*
 * DragonBoardVR API - generic RmlUi panels for Skyrim VR
 *
 * For mod authors: copy this file into your project and request the API during
 * or after SKSEMessagingInterface::kMessage_PostLoad.
 *
 * Example:
 *   auto* api = DragonBoardVR_API::RequestPluginAPI();
 *   if (api) {
 *       DragonBoardVR_API::PanelDescriptor panel{
 *           .id = "Author.Mod.Settings",
 *           .documentPath = "Data/SKSE/Plugins/MyMod/ui/settings.rml"
 *       };
 *       auto handle = api->RegisterPanel(&panel);
 *       api->ShowPanel(handle);
 *   }
 */
#pragma once

#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace DragonBoardVR_API
{
    constexpr const auto PluginName = "DragonBoardVR";

    constexpr std::uint32_t InterfaceVersion1 = 1;
    constexpr std::uint32_t InterfaceVersion2 = 2;

    using PanelHandle = std::uint32_t;
    constexpr PanelHandle InvalidPanel = 0;

    enum class PanelEventType : std::uint8_t
    {
        Click,
        Change
    };

    struct PanelEvent
    {
        PanelHandle panel = InvalidPanel;
        PanelEventType type = PanelEventType::Click;
        const char* elementId = nullptr;
        const char* value = nullptr;
        float numericValue = 0.0f;
    };

    using PanelEventCallback = void (*)(const PanelEvent* event, void* userData) noexcept;

    struct PanelDescriptor
    {
        /// Stable identifier, normally "Author.Mod.Settings".
        const char* id = nullptr;
        /// Path relative to the Skyrim directory, for example
        /// "Data/SKSE/Plugins/MyMod/ui/settings.rml".
        const char* documentPath = nullptr;
        PanelEventCallback onEvent = nullptr;
        void* userData = nullptr;
    };

    class IDragonBoardVR
    {
    public:
        virtual PanelHandle RegisterPanel(const PanelDescriptor* descriptor) noexcept = 0;
        virtual void UnregisterPanel(PanelHandle panel) noexcept = 0;
        virtual bool ShowPanel(PanelHandle panel) noexcept = 0;
        virtual void HidePanel(PanelHandle panel) noexcept = 0;
        virtual bool IsPanelVisible(PanelHandle panel) noexcept = 0;

        /// Thread-safe DOM update requests. They are applied on the next
        /// Present frame; callbacks are delivered later on Skyrim's game thread.
        virtual bool SetElementText(
            PanelHandle panel, const char* elementId, const char* text) noexcept = 0;
        virtual bool SetElementAttribute(
            PanelHandle panel,
            const char* elementId,
            const char* name,
            const char* value) noexcept = 0;
        virtual bool SetElementClass(
            PanelHandle panel,
            const char* elementId,
            const char* className,
            bool enabled) noexcept = 0;

    protected:
        ~IDragonBoardVR() = default;
    };

    enum class Capability : std::uint64_t
    {
        None = 0,
        VersionedDescriptors = 1ULL << 0,
        ThreadSafeDomUpdates = 1ULL << 1,
        PanelLoadState = 1ULL << 2,
        ElementEvents = 1ULL << 3,
        IndependentSurfaces = 1ULL << 4,
        SimultaneousSurfaces = 1ULL << 5,
        GrabbableSurfaces = 1ULL << 6
    };

    [[nodiscard]] constexpr std::uint64_t ToMask(Capability capability) noexcept
    {
        return static_cast<std::uint64_t>(capability);
    }

    enum class PanelState : std::uint8_t
    {
        Unknown,
        Queued,
        Ready,
        Failed,
        Visible
    };

    enum class PanelFlags : std::uint32_t
    {
        None = 0,
        Interactive = 1U << 0
    };

    struct PanelDescriptorV2
    {
        std::uint32_t structSize = sizeof(PanelDescriptorV2);
        std::uint32_t interfaceVersion = InterfaceVersion2;
        const char* id = nullptr;
        const char* documentPath = nullptr;
        PanelEventCallback onEvent = nullptr;
        void* userData = nullptr;
        std::uint32_t flags = static_cast<std::uint32_t>(PanelFlags::Interactive);
        std::uint32_t reserved = 0;
    };

    using SurfaceHandle = std::uint32_t;
    constexpr SurfaceHandle InvalidSurface = 0;

    enum class SurfaceAnchor : std::uint8_t
    {
        DragonBoard,
        LeftHand,
        RightHand,
        Hmd,
        World
    };

    enum class SurfaceFlags : std::uint32_t
    {
        None = 0,
        Visible = 1U << 0,
        Interactive = 1U << 1,
        RenderOnDirty = 1U << 2,
        Grabbable = 1U << 3,
        PersistTransform = 1U << 4
    };

    // Baseline contract for every newly created independent RmlUi surface.
    // Additional capabilities may be ORed into this mask in later versions.
    constexpr std::uint32_t DefaultSurfaceFeatures =
        static_cast<std::uint32_t>(SurfaceFlags::Visible) |
        static_cast<std::uint32_t>(SurfaceFlags::Interactive) |
        static_cast<std::uint32_t>(SurfaceFlags::RenderOnDirty) |
        static_cast<std::uint32_t>(SurfaceFlags::Grabbable) |
        static_cast<std::uint32_t>(SurfaceFlags::PersistTransform);

    struct SurfaceTransform
    {
        // Position is expressed in the selected anchor's local space.
        // Rotation uses degrees in XYZ order. Scale is uniform.
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        float rotationX = 0.0f;
        float rotationY = 0.0f;
        float rotationZ = 0.0f;
        float scale = 1.0f;
    };

    enum class SurfaceEventType : std::uint8_t
    {
        GrabStarted,
        TransformChanged,
        GrabEnded
    };

    struct SurfaceEvent
    {
        SurfaceEventType type = SurfaceEventType::TransformChanged;
        SurfaceHandle surface = InvalidSurface;
        SurfaceTransform transform{};
    };

    using SurfaceEventCallback = void (*)(const SurfaceEvent* event, void* userData);

    struct SurfaceDescriptorV2
    {
        std::uint32_t structSize = sizeof(SurfaceDescriptorV2);
        std::uint32_t interfaceVersion = InterfaceVersion2;
        const char* id = nullptr;
        SurfaceAnchor anchor = SurfaceAnchor::DragonBoard;
        SurfaceTransform transform{};
        float physicalWidth = 4.0f;
        float physicalHeight = 2.25f;
        std::uint32_t textureWidth = 512;
        std::uint32_t textureHeight = 288;
        std::uint32_t logicalWidth = 512;
        std::uint32_t logicalHeight = 288;
        float maxFramesPerSecond = 15.0f;
        std::uint32_t flags = DefaultSurfaceFeatures;
        SurfaceEventCallback onEvent = nullptr;
        void* userData = nullptr;
    };

    class IDragonBoardVR2
    {
    public:
        [[nodiscard]] virtual std::uint32_t GetInterfaceVersion() const noexcept = 0;
        [[nodiscard]] virtual std::uint64_t GetCapabilities() const noexcept = 0;

        virtual PanelHandle RegisterPanelV2(const PanelDescriptorV2* descriptor) noexcept = 0;
        virtual void UnregisterPanel(PanelHandle panel) noexcept = 0;
        [[nodiscard]] virtual PanelState GetPanelState(PanelHandle panel) const noexcept = 0;
        virtual bool ShowPanel(PanelHandle panel) noexcept = 0;
        virtual void HidePanel(PanelHandle panel) noexcept = 0;
        virtual bool SetElementText(
            PanelHandle panel, const char* elementId, const char* text) noexcept = 0;
        virtual bool SetElementAttribute(
            PanelHandle panel,
            const char* elementId,
            const char* name,
            const char* value) noexcept = 0;
        virtual bool SetElementClass(
            PanelHandle panel,
            const char* elementId,
            const char* className,
            bool enabled) noexcept = 0;

        // Reserved in v2 so surface support can be enabled without changing the
        // interface layout. Check IndependentSurfaces before calling.
        virtual SurfaceHandle CreateSurface(const SurfaceDescriptorV2* descriptor) noexcept = 0;
        virtual void DestroySurface(SurfaceHandle surface) noexcept = 0;
        virtual bool BindPanelToSurface(
            SurfaceHandle surface, PanelHandle panel) noexcept = 0;
        virtual bool SetSurfaceVisible(SurfaceHandle surface, bool visible) noexcept = 0;
        virtual bool SetSurfaceTransform(
            SurfaceHandle surface, const SurfaceTransform* transform) noexcept = 0;
        virtual bool GetSurfaceTransform(
            SurfaceHandle surface, SurfaceTransform* transform) const noexcept = 0;

    protected:
        ~IDragonBoardVR2() = default;
    };

    using _RequestPluginAPI = IDragonBoardVR* (*)();
    using _RequestPluginAPI2 = IDragonBoardVR2* (*)();

    /// Request the single DragonBoardVR panel API.
    /// Call during or after SKSEMessagingInterface::kMessage_PostLoad.
    [[nodiscard]] inline IDragonBoardVR* RequestPluginAPI()
    {
        const auto pluginHandle = GetModuleHandleA("DragonBoardVR.dll");
        if (!pluginHandle) return nullptr;

        const auto requestFunc = reinterpret_cast<_RequestPluginAPI>(
            GetProcAddress(pluginHandle, "RequestPluginAPI"));
        return requestFunc ? requestFunc() : nullptr;
    }

    [[nodiscard]] inline IDragonBoardVR2* RequestPluginAPI2()
    {
        const auto pluginHandle = GetModuleHandleA("DragonBoardVR.dll");
        if (!pluginHandle) return nullptr;

        const auto requestFunc = reinterpret_cast<_RequestPluginAPI2>(
            GetProcAddress(pluginHandle, "RequestPluginAPI2"));
        return requestFunc ? requestFunc() : nullptr;
    }
}
