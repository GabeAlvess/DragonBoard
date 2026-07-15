#include "papyrus/PapyrusPanelBridge.h"

#include "DragonBoardVR_API.h"
#include "ui/rml/RmlPanelHost.h"

#include <RE/B/BGSBaseAlias.h>
#include <RE/B/BSFixedString.h>
#include <RE/I/IVirtualMachine.h>
#include <RE/T/TESForm.h>
#include <SKSE/RegistrationMap.h>

#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dragonboard::papyrus
{
    namespace
    {
        constexpr auto kPapyrusClass = "DragonBoardVR";
        constexpr auto kPanelEvent = "OnDragonBoardPanelEvent";

        using PanelHandle = DragonBoardVR_API::PanelHandle;
        using PanelEventRegistrations = SKSE::RegistrationMap<
            PanelHandle,
            std::int32_t,
            RE::BSFixedString,
            RE::BSFixedString,
            RE::BSFixedString,
            float>;

        PanelEventRegistrations g_panelEvents{ kPanelEvent };
        std::mutex g_ownerMutex;
        std::unordered_map<PanelHandle, const RE::BGSBaseAlias*> g_panelOwners;

        [[nodiscard]] PanelHandle ToPanelHandle(std::int32_t panel) noexcept
        {
            return panel > 0 ? static_cast<PanelHandle>(panel) : DragonBoardVR_API::InvalidPanel;
        }

        [[nodiscard]] bool IsPapyrusPanel(PanelHandle panel) noexcept
        {
            std::scoped_lock lock(g_ownerMutex);
            return g_panelOwners.contains(panel);
        }

        void DispatchPanelEvent(
            const DragonBoardVR_API::PanelEvent* event,
            void*) noexcept
        {
            if (!event || !IsPapyrusPanel(event->panel)) return;

            const auto eventType = event->type == DragonBoardVR_API::PanelEventType::Change ?
                "Change" : "Click";
            g_panelEvents.SendEvent(
                event->panel,
                static_cast<std::int32_t>(event->panel),
                RE::BSFixedString(eventType),
                RE::BSFixedString(event->elementId ? event->elementId : ""),
                RE::BSFixedString(event->value ? event->value : ""),
                event->numericValue);
        }

        bool IsInstalled(RE::StaticFunctionTag*)
        {
            return true;
        }

        std::int32_t RegisterPanel(
            RE::StaticFunctionTag*,
            RE::BGSBaseAlias* receiver,
            RE::BSFixedString panelId,
            RE::BSFixedString documentPath)
        {
            if (!receiver || panelId.empty() || documentPath.empty()) return 0;

            try {
                const DragonBoardVR_API::PanelDescriptor descriptor{
                    panelId.c_str(),
                    documentPath.c_str(),
                    &DispatchPanelEvent,
                    nullptr
                };
                const auto handle = ui::rml::RmlPanelHost::GetSingleton()
                    .RegisterExternalPanel(descriptor);
                if (handle == DragonBoardVR_API::InvalidPanel ||
                    handle > static_cast<PanelHandle>(std::numeric_limits<std::int32_t>::max())) {
                    if (handle != DragonBoardVR_API::InvalidPanel) {
                        ui::rml::RmlPanelHost::GetSingleton().UnregisterExternalPanel(handle);
                    }
                    return 0;
                }

                if (!g_panelEvents.Register(receiver, handle)) {
                    ui::rml::RmlPanelHost::GetSingleton().UnregisterExternalPanel(handle);
                    logger::error(
                        "DragonBoardVR Papyrus: alias {} has no event-capable script.",
                        receiver->aliasID);
                    return 0;
                }

                {
                    std::scoped_lock lock(g_ownerMutex);
                    g_panelOwners.emplace(handle, receiver);
                }
                logger::info(
                    "DragonBoardVR Papyrus: registered panel '{}' as handle {} for alias {}.",
                    panelId.c_str(),
                    handle,
                    receiver->aliasID);
                return static_cast<std::int32_t>(handle);
            } catch (const std::exception& e) {
                logger::error("DragonBoardVR Papyrus: RegisterPanel failed: {}", e.what());
                return 0;
            }
        }

        bool UnregisterPanel(
            RE::StaticFunctionTag*,
            RE::BGSBaseAlias* receiver,
            std::int32_t panelValue)
        {
            const auto panel = ToPanelHandle(panelValue);
            if (!receiver || panel == DragonBoardVR_API::InvalidPanel) return false;

            {
                std::scoped_lock lock(g_ownerMutex);
                const auto it = g_panelOwners.find(panel);
                if (it == g_panelOwners.end() || it->second != receiver) return false;
                g_panelOwners.erase(it);
            }

            g_panelEvents.Unregister(receiver, panel);
            ui::rml::RmlPanelHost::GetSingleton().UnregisterExternalPanel(panel);
            return true;
        }

        bool ShowPanel(RE::StaticFunctionTag*, std::int32_t panelValue)
        {
            const auto panel = ToPanelHandle(panelValue);
            return IsPapyrusPanel(panel) &&
                ui::rml::RmlPanelHost::GetSingleton().ShowExternalPanel(panel);
        }

        bool HidePanel(RE::StaticFunctionTag*, std::int32_t panelValue)
        {
            const auto panel = ToPanelHandle(panelValue);
            if (!IsPapyrusPanel(panel)) return false;
            ui::rml::RmlPanelHost::GetSingleton().HideExternalPanel(panel);
            return true;
        }

        bool IsPanelVisible(RE::StaticFunctionTag*, std::int32_t panelValue)
        {
            const auto panel = ToPanelHandle(panelValue);
            return IsPapyrusPanel(panel) &&
                ui::rml::RmlPanelHost::GetSingleton().IsExternalPanelVisible(panel);
        }

        bool SetElementText(
            RE::StaticFunctionTag*,
            std::int32_t panelValue,
            RE::BSFixedString elementId,
            RE::BSFixedString text)
        {
            const auto panel = ToPanelHandle(panelValue);
            return IsPapyrusPanel(panel) &&
                ui::rml::RmlPanelHost::GetSingleton().SetExternalElementText(
                    panel, elementId.c_str(), text.c_str());
        }

        bool SetElementAttribute(
            RE::StaticFunctionTag*,
            std::int32_t panelValue,
            RE::BSFixedString elementId,
            RE::BSFixedString name,
            RE::BSFixedString value)
        {
            const auto panel = ToPanelHandle(panelValue);
            return IsPapyrusPanel(panel) &&
                ui::rml::RmlPanelHost::GetSingleton().SetExternalElementAttribute(
                    panel, elementId.c_str(), name.c_str(), value.c_str());
        }

        bool RemoveElementAttribute(
            RE::StaticFunctionTag*,
            std::int32_t panelValue,
            RE::BSFixedString elementId,
            RE::BSFixedString name)
        {
            const auto panel = ToPanelHandle(panelValue);
            return IsPapyrusPanel(panel) &&
                ui::rml::RmlPanelHost::GetSingleton().SetExternalElementAttribute(
                    panel, elementId.c_str(), name.c_str(), nullptr);
        }

        bool SetElementClass(
            RE::StaticFunctionTag*,
            std::int32_t panelValue,
            RE::BSFixedString elementId,
            RE::BSFixedString className,
            bool enabled)
        {
            const auto panel = ToPanelHandle(panelValue);
            return IsPapyrusPanel(panel) &&
                ui::rml::RmlPanelHost::GetSingleton().SetExternalElementClass(
                    panel, elementId.c_str(), className.c_str(), enabled);
        }
    }

    bool RegisterPanelFunctions(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) return false;

        vm->RegisterFunction("IsInstalled", kPapyrusClass, IsInstalled);
        vm->RegisterFunction("RegisterPanel", kPapyrusClass, RegisterPanel);
        vm->RegisterFunction("UnregisterPanel", kPapyrusClass, UnregisterPanel);
        vm->RegisterFunction("ShowPanel", kPapyrusClass, ShowPanel);
        vm->RegisterFunction("HidePanel", kPapyrusClass, HidePanel);
        vm->RegisterFunction("IsPanelVisible", kPapyrusClass, IsPanelVisible);
        vm->RegisterFunction("SetElementText", kPapyrusClass, SetElementText);
        vm->RegisterFunction("SetElementAttribute", kPapyrusClass, SetElementAttribute);
        vm->RegisterFunction("RemoveElementAttribute", kPapyrusClass, RemoveElementAttribute);
        vm->RegisterFunction("SetElementClass", kPapyrusClass, SetElementClass);
        logger::info("DragonBoardVR: Papyrus panel API registered.");
        return true;
    }

    void ResetPapyrusPanels()
    {
        std::vector<PanelHandle> panels;
        {
            std::scoped_lock lock(g_ownerMutex);
            panels.reserve(g_panelOwners.size());
            for (const auto& [panel, owner] : g_panelOwners) {
                (void)owner;
                panels.push_back(panel);
            }
            g_panelOwners.clear();
        }

        g_panelEvents.Clear();
        for (const auto panel : panels) {
            ui::rml::RmlPanelHost::GetSingleton().UnregisterExternalPanel(panel);
        }
        if (!panels.empty()) {
            logger::info("DragonBoardVR Papyrus: cleared {} panel registration(s).", panels.size());
        }
    }
}
