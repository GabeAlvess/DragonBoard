#include "MenuActionRouter.h"

#include "integrations/higgs/PhysicalBoardController.h"
#include "runtime/vr/GameMenuActions.h"
#include "ui/rml/RmlPanelHost.h"
#include "vrui/VRMenuManager.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

namespace dragonboard::ui::menu
{
    namespace
    {
        std::string NormalizeAction(std::string_view action)
        {
            const auto first = action.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return {};
            const auto last = action.find_last_not_of(" \t\r\n");

            std::string normalized(action.substr(first, last - first + 1));
            std::transform(
                normalized.begin(),
                normalized.end(),
                normalized.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return normalized;
        }

        void OpenHome(vrui::VRMenuManager& manager)
        {
            manager.navigateHome();
            manager.switchToPanel("MainPanel");
        }

        bool OpenRmlDocument(
            vrui::VRMenuManager& manager,
            MenuActionMode mode,
            bool alreadyOpen,
            const std::function<bool()>& open)
        {
            auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
            if (mode == MenuActionMode::Toggle && alreadyOpen) {
                rmlHost.Close();
                return true;
            }

            OpenHome(manager);
            return open();
        }

        bool OpenNativeMenu(
            vrui::VRMenuManager& manager,
            const char* menuName)
        {
            manager.closeMenu();
            dragonboard::runtime::vr::ShowGameMenu(menuName);
            return true;
        }
    }

    bool MenuActionRouter::Execute(std::string_view action, MenuActionMode mode)
    {
        auto& manager = vrui::VRMenuManager::get();
        auto& rmlHost = dragonboard::ui::rml::RmlPanelHost::GetSingleton();
        const std::string normalized = NormalizeAction(action);

        if (normalized.empty() || normalized == "none") return true;
        if (normalized == "mainpanel" || normalized == "home") {
            OpenHome(manager);
            return true;
        }
        if (normalized == "close") {
            manager.closeMenu();
            return true;
        }
        if (normalized == "settings" || normalized == "mcm_panel") {
            return OpenRmlDocument(
                manager,
                mode,
                rmlHost.IsSettingsOpen(),
                [&rmlHost]() { return rmlHost.OpenSettings(); });
        }
        if (normalized == "dev" || normalized == "devpanel" || normalized == "developer") {
            return OpenRmlDocument(
                manager,
                mode,
                rmlHost.IsDeveloperOpen(),
                [&rmlHost]() { return rmlHost.OpenDeveloper(); });
        }
        if (normalized == "gallery" || normalized == "gallerypanel") {
            return OpenRmlDocument(
                manager,
                mode,
                rmlHost.IsGalleryOpen(),
                [&rmlHost]() { return rmlHost.OpenGallery(); });
        }
        if (normalized == "journal" || normalized == "journalmenu" ||
            normalized == "favoritespanel" || normalized == "container:favorites" ||
            normalized == "favorites_dyn") {
            return OpenRmlDocument(
                manager,
                mode,
                rmlHost.IsJournalOpen(),
                [&rmlHost]() { return rmlHost.OpenJournal(); });
        }
        if (normalized == "inventorypanel" || normalized == "container:inventory" ||
            normalized == "inventory_dyn") {
            mode == MenuActionMode::Toggle ?
                manager.togglePanel("InventoryPanel") : manager.switchToPanel("InventoryPanel");
            return true;
        }
        if (normalized == "magicpanel" || normalized == "container:magic" ||
            normalized == "magic_dyn") {
            mode == MenuActionMode::Toggle ?
                manager.togglePanel("MagicPanel") : manager.switchToPanel("MagicPanel");
            return true;
        }
        if (normalized == "modspanel" || normalized == "mods" ||
            normalized == "container:mods" || normalized == "mods_dyn") {
            mode == MenuActionMode::Toggle ?
                manager.togglePanel("ModsPanel") : manager.switchToPanel("ModsPanel");
            return true;
        }
        if (normalized == "quicksave" || normalized == "save") {
            manager.closeMenu();
            dragonboard::runtime::vr::QueueNewSave();
            return true;
        }
        if (normalized == "statsmenu" || normalized == "stats") {
            return OpenNativeMenu(manager, "StatsMenu");
        }
        if (normalized == "inventorymenu" || normalized == "inventory") {
            return OpenNativeMenu(manager, "InventoryMenu");
        }
        if (normalized == "magicmenu" || normalized == "magic") {
            return OpenNativeMenu(manager, "MagicMenu");
        }
        if (normalized == "mapmenu" || normalized == "map") {
            if (dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton()
                    .StoreHeldBoardBeforeOpeningMap()) {
                return true;
            }
            return OpenNativeMenu(manager, "MapMenu");
        }
        if (normalized == "tweenmenu") {
            return OpenNativeMenu(manager, "TweenMenu");
        }
        if (normalized == "wait" || normalized == "sleep" ||
            normalized == "sleep/wait menu") {
            return OpenNativeMenu(manager, "Sleep/Wait Menu");
        }

        const std::string panelName(action);
        if (manager.findPanelByName(panelName)) {
            mode == MenuActionMode::Toggle ?
                manager.togglePanel(panelName) : manager.switchToPanel(panelName);
            return true;
        }

        logger::error(
            "DragonBoardVR: rejected unknown menu action '{}'; returning to MainPanel instead of sending it to Skyrim UI.",
            action);
        OpenHome(manager);
        return false;
    }
}
