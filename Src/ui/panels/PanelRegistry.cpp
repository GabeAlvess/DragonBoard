#include "PanelRegistry.h"

#include "vrui/VRUIPanel.h"

#include <algorithm>

namespace dragonboard::ui::panels
{
    void PanelRegistry::Register(std::shared_ptr<vrui::VRUIPanel> panel)
    {
        _panels.push_back(std::move(panel));
    }

    bool PanelRegistry::Unregister(const std::shared_ptr<vrui::VRUIPanel>& panel)
    {
        const auto it = std::find(_panels.begin(), _panels.end(), panel);
        if (it == _panels.end()) {
            return false;
        }

        _panels.erase(it);
        return true;
    }

    void PanelRegistry::Clear()
    {
        _panels.clear();
    }

    bool PanelRegistry::Contains(const std::shared_ptr<vrui::VRUIPanel>& panel) const
    {
        return std::find(_panels.begin(), _panels.end(), panel) != _panels.end();
    }

    std::shared_ptr<vrui::VRUIPanel> PanelRegistry::FindByName(const std::string& name) const
    {
        for (const auto& panel : _panels) {
            if (panel && panel->getName() == name) {
                return panel;
            }
        }

        return nullptr;
    }

    std::shared_ptr<vrui::VRUIPanel> PanelRegistry::FindActive() const
    {
        for (const auto& panel : _panels) {
            if (panel && panel->isActive()) {
                return panel;
            }
        }

        return nullptr;
    }
}
