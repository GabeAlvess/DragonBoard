/*
 * DragonBoardVR API - VR Menu Framework for Skyrim VR
 * 
 * For modders: Copy this file into your own project to use the DragonBoardVR API.
 * Request the API interface during or after kMessage_PostLoad.
 *
 * Example usage:
 *   auto* api = DragonBoardVR_API::RequestPluginAPI();
 *   if (api) {
 *       auto panel = api->CreatePanel("MyMod_Settings");
 *       api->AddButton(panel, "Toggle Feature", [](bool) { ... });
 *       api->ShowPanel(panel);
 *   }
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace DragonBoardVR_API
{
    constexpr const auto PluginName = "DragonBoardVR";

    // Opaque handles
    using PanelHandle = uint32_t;
    using ButtonHandle = uint32_t;
    constexpr PanelHandle InvalidPanel = 0;
    constexpr ButtonHandle InvalidButton = 0;

    // Callback types
    using ButtonPressCallback = std::function<void()>;
    using ToggleCallback = std::function<void(bool)>;

    enum class InterfaceVersion : uint8_t
    {
        V1
    };

    /// Public API interface v1
    class IVDragonBoardVR1
    {
    public:
        // --- Panel Management ---

        /// Create a new panel (root menu container)
        /// @param name  Unique identifier for this panel
        /// @return Handle to the panel, or InvalidPanel on failure
        virtual PanelHandle CreatePanel(const char* name) noexcept = 0;

        /// Destroy a panel and all its children
        virtual void DestroyPanel(PanelHandle panel) noexcept = 0;

        /// Show a panel (attaches to non-dominant hand and becomes interactive)
        virtual void ShowPanel(PanelHandle panel) noexcept = 0;

        /// Hide a panel (detaches from hand, stops interaction)
        virtual void HidePanel(PanelHandle panel) noexcept = 0;

        /// Check if a panel is currently visible
        virtual bool IsPanelVisible(PanelHandle panel) noexcept = 0;

        // --- Button Creation ---

        /// Add a press button to a panel
        /// @param panel    Panel to add the button to
        /// @param label    Button display text
        /// @param onPress  Callback when button is pressed
        /// @return Handle to the button, or InvalidButton on failure
        virtual ButtonHandle AddButton(PanelHandle panel, const char* label,
                                        ButtonPressCallback onPress) noexcept = 0;

        /// Add a toggle button to a panel
        /// @param panel       Panel to add the button to
        /// @param label       Button display text
        /// @param initial     Initial toggle state
        /// @param onToggle    Callback when toggle state changes
        virtual ButtonHandle AddToggleButton(PanelHandle panel, const char* label,
                                              bool initial,
                                              ToggleCallback onToggle) noexcept = 0;

        /// Add a button using a custom NIF mesh
        /// @param nifPath     Path to NIF mesh relative to Data/Meshes/
        virtual ButtonHandle AddNifButton(PanelHandle panel, const char* nifPath,
                                           ButtonPressCallback onPress) noexcept = 0;

        // --- Layout ---

        /// Begin a horizontal row within a panel (buttons added after this go into the row)
        virtual void BeginRow(PanelHandle panel) noexcept = 0;

        /// End the current horizontal row
        virtual void EndRow(PanelHandle panel) noexcept = 0;

        // --- Customization ---

        /// Set the position offset of a panel relative to the hand
        virtual void SetPanelOffset(PanelHandle panel, float x, float y, float z) noexcept = 0;

        /// Set the scale of a panel
        virtual void SetPanelScale(PanelHandle panel, float scale) noexcept = 0;
    };

    // Internal: function pointer type for API request
    typedef void* (*_RequestPluginAPI)(const InterfaceVersion interfaceVersion);

    /// Request the DragonBoardVR API interface.
    /// Call during or after SKSEMessagingInterface::kMessage_PostLoad.
    [[nodiscard]] inline void* RequestPluginAPI(
        const InterfaceVersion version = InterfaceVersion::V1)
    {
        auto pluginHandle = GetModuleHandle("DragonBoardVR.dll");
        if (!pluginHandle) return nullptr;

        auto requestFunc = reinterpret_cast<_RequestPluginAPI>(
            GetProcAddress(pluginHandle, "RequestPluginAPI"));
        if (requestFunc) {
            return requestFunc(version);
        }
        return nullptr;
    }
}

// Internal implementation entry used by the exported RequestPluginAPI symbol.
void* GetDragonBoardVRAPI(DragonBoardVR_API::InterfaceVersion version);
