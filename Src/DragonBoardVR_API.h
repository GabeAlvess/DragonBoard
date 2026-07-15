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

    using _RequestPluginAPI = IDragonBoardVR* (*)();

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
}
