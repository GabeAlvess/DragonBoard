#include "DragonBoardVR_API.h"

#include "ui/rml/RmlPanelHost.h"

namespace
{
    using namespace DragonBoardVR_API;

    class DragonBoardVRAPIImpl final : public IDragonBoardVR
    {
    public:
        PanelHandle RegisterPanel(const PanelDescriptor* descriptor) noexcept override
        {
            if (!descriptor) return InvalidPanel;
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .RegisterExternalPanel(*descriptor);
        }

        void UnregisterPanel(PanelHandle panel) noexcept override
        {
            dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .UnregisterExternalPanel(panel);
        }

        bool ShowPanel(PanelHandle panel) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .ShowExternalPanel(panel);
        }

        void HidePanel(PanelHandle panel) noexcept override
        {
            dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .HideExternalPanel(panel);
        }

        bool IsPanelVisible(PanelHandle panel) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .IsExternalPanelVisible(panel);
        }

        bool SetElementText(
            PanelHandle panel, const char* elementId, const char* text) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .SetExternalElementText(panel, elementId, text);
        }

        bool SetElementAttribute(
            PanelHandle panel,
            const char* elementId,
            const char* name,
            const char* value) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .SetExternalElementAttribute(panel, elementId, name, value);
        }

        bool SetElementClass(
            PanelHandle panel,
            const char* elementId,
            const char* className,
            bool enabled) noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .SetExternalElementClass(panel, elementId, className, enabled);
        }
    };

    DragonBoardVRAPIImpl g_api;

    class DragonBoardVRAPI2Impl final : public IDragonBoardVR2
    {
    public:
        [[nodiscard]] std::uint32_t GetInterfaceVersion() const noexcept override
        {
            return InterfaceVersion2;
        }

        [[nodiscard]] std::uint64_t GetCapabilities() const noexcept override
        {
            return ToMask(Capability::VersionedDescriptors) |
                ToMask(Capability::ThreadSafeDomUpdates) |
                ToMask(Capability::PanelLoadState) |
                ToMask(Capability::ElementEvents);
        }

        PanelHandle RegisterPanelV2(const PanelDescriptorV2* descriptor) noexcept override
        {
            if (!descriptor || descriptor->structSize < sizeof(PanelDescriptorV2) ||
                descriptor->interfaceVersion != InterfaceVersion2) {
                return InvalidPanel;
            }
            const PanelDescriptor legacy{
                descriptor->id,
                descriptor->documentPath,
                descriptor->onEvent,
                descriptor->userData
            };
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .RegisterExternalPanel(legacy);
        }

        void UnregisterPanel(PanelHandle panel) noexcept override
        {
            g_api.UnregisterPanel(panel);
        }

        [[nodiscard]] PanelState GetPanelState(PanelHandle panel) const noexcept override
        {
            return dragonboard::ui::rml::RmlPanelHost::GetSingleton()
                .GetExternalPanelState(panel);
        }

        bool ShowPanel(PanelHandle panel) noexcept override
        {
            return g_api.ShowPanel(panel);
        }

        void HidePanel(PanelHandle panel) noexcept override
        {
            g_api.HidePanel(panel);
        }

        bool SetElementText(
            PanelHandle panel, const char* elementId, const char* text) noexcept override
        {
            return g_api.SetElementText(panel, elementId, text);
        }

        bool SetElementAttribute(
            PanelHandle panel,
            const char* elementId,
            const char* name,
            const char* value) noexcept override
        {
            return g_api.SetElementAttribute(panel, elementId, name, value);
        }

        bool SetElementClass(
            PanelHandle panel,
            const char* elementId,
            const char* className,
            bool enabled) noexcept override
        {
            return g_api.SetElementClass(panel, elementId, className, enabled);
        }

        SurfaceHandle CreateSurface(const SurfaceDescriptorV2*) noexcept override
        {
            return InvalidSurface;
        }

        void DestroySurface(SurfaceHandle) noexcept override {}

        bool BindPanelToSurface(SurfaceHandle, PanelHandle) noexcept override
        {
            return false;
        }

        bool SetSurfaceVisible(SurfaceHandle, bool) noexcept override
        {
            return false;
        }

        bool SetSurfaceTransform(SurfaceHandle, const SurfaceTransform*) noexcept override
        {
            return false;
        }

        bool GetSurfaceTransform(SurfaceHandle, SurfaceTransform*) const noexcept override
        {
            return false;
        }
    };

    DragonBoardVRAPI2Impl g_api2;
}

DragonBoardVR_API::IDragonBoardVR* GetDragonBoardVRAPI()
{
    return &g_api;
}

DragonBoardVR_API::IDragonBoardVR2* GetDragonBoardVRAPI2()
{
    return &g_api2;
}
