#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vrui
{
    class VRUIPanel;
}

namespace dragonboard::ui::panels
{
    class PanelRegistry
    {
    public:
        void Register(std::shared_ptr<vrui::VRUIPanel> panel);
        bool Unregister(const std::shared_ptr<vrui::VRUIPanel>& panel);
        void Clear();

        bool Contains(const std::shared_ptr<vrui::VRUIPanel>& panel) const;
        std::shared_ptr<vrui::VRUIPanel> FindByName(const std::string& name) const;
        std::shared_ptr<vrui::VRUIPanel> FindActive() const;

        std::vector<std::shared_ptr<vrui::VRUIPanel>>& GetPanels() { return _panels; }
        const std::vector<std::shared_ptr<vrui::VRUIPanel>>& GetPanels() const { return _panels; }
        std::size_t Size() const { return _panels.size(); }

    private:
        std::vector<std::shared_ptr<vrui::VRUIPanel>> _panels;
    };
}
