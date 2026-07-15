#include "DragonBoardVR_API.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUIContainer.h"
#include "vrui/VRUIButton.h"
#include "vrui/VRUIToggleButton.h"

#include <unordered_map>
#include <memory>
#include <string>

namespace
{
    using namespace DragonBoardVR_API;
    using namespace vrui;

    struct PanelContext
    {
        std::shared_ptr<VRUIPanel> panel;
        std::shared_ptr<VRUIContainer> currentRow;
        RE::NiPoint3 offset{ 0.0f, 5.0f, 10.0f };
        float scale = 1.0f;
        bool hasCustomOffset = false;
        bool hasCustomScale = false;
    };

    class DragonBoardVRAPIImpl final : public IVDragonBoardVR1
    {
    public:
        PanelHandle CreatePanel(const char* name) noexcept override
        {
            if (!name || !*name) {
                return InvalidPanel;
            }

            auto& manager = VRMenuManager::get();
            if (manager.findPanelByName(name)) {
                logger::warn("DragonBoardVR API: panel '{}' already exists", name);
                return InvalidPanel;
            }

            auto panel = std::make_shared<VRUIPanel>(name);
            panel->setActive(true);
            manager.registerPanel(panel);

            const PanelHandle handle = _nextPanelHandle++;
            auto& ctx = _panels[handle];
            ctx.panel = std::move(panel);
            return handle;
        }

        void DestroyPanel(PanelHandle panel) noexcept override
        {
            auto it = _panels.find(panel);
            if (it == _panels.end() || !it->second.panel) {
                return;
            }

            VRMenuManager::get().unregisterPanel(it->second.panel);
            it->second.currentRow.reset();
            _panels.erase(it);
        }

        void ShowPanel(PanelHandle panel) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel) {
                return;
            }

            auto& manager = VRMenuManager::get();
            if (!manager.isMenuOpen()) {
                manager.toggleMenu();
            }

            manager.switchToPanel(ctx->panel->getName());
            applyPanelSettings(*ctx);
        }

        void HidePanel(PanelHandle panel) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel) {
                return;
            }

            ctx->panel->hide();
            ctx->panel->setVisible(false);
            ctx->panel->detachFromParent();
        }

        bool IsPanelVisible(PanelHandle panel) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            return ctx && ctx->panel && ctx->panel->isShown();
        }

        ButtonHandle AddButton(PanelHandle panel, const char* label,
                               ButtonPressCallback onPress) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel || !label) {
                return InvalidButton;
            }

            auto btn = std::make_shared<VRUIButton>(label);
            btn->setOnPressHandler([callback = std::move(onPress)](VRUIButton*, EquipHand) {
                if (callback) {
                    callback();
                }
            });

            addWidgetToTarget(*ctx, btn);
            return _nextButtonHandle++;
        }

        ButtonHandle AddToggleButton(PanelHandle panel, const char* label,
                                     bool initial,
                                     ToggleCallback onToggle) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel || !label) {
                return InvalidButton;
            }

            auto btn = std::make_shared<VRUIToggleButton>(label, initial);
            btn->setOnToggleHandler([callback = std::move(onToggle)](VRUIToggleButton*, bool state) {
                if (callback) {
                    callback(state);
                }
            });

            addWidgetToTarget(*ctx, btn);
            return _nextButtonHandle++;
        }

        ButtonHandle AddNifButton(PanelHandle panel, const char* nifPath,
                                  ButtonPressCallback onPress) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel || !nifPath || !*nifPath) {
                return InvalidButton;
            }

            auto btn = std::make_shared<VRUIButton>("", nifPath, "", 3.0f, 1.5f);
            btn->setOnPressHandler([callback = std::move(onPress)](VRUIButton*, EquipHand) {
                if (callback) {
                    callback();
                }
            });

            addWidgetToTarget(*ctx, btn);
            return _nextButtonHandle++;
        }

        void BeginRow(PanelHandle panel) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel) {
                return;
            }

            auto rowName = ctx->panel->getName() + std::string("_row_") + std::to_string(_nextRowId++);
            auto row = std::make_shared<VRUIContainer>(rowName, ContainerLayout::HorizontalCenter, 0.3f, 0.3f, 0.0f, 1.0f);
            ctx->panel->addElement(row);
            ctx->currentRow = row;
            ctx->panel->recalculateLayout();
        }

        void EndRow(PanelHandle panel) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx) {
                return;
            }

            ctx->currentRow.reset();
            if (ctx->panel) {
                ctx->panel->recalculateLayout();
            }
        }

        void SetPanelOffset(PanelHandle panel, float x, float y, float z) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel) {
                return;
            }

            ctx->offset = RE::NiPoint3{ x, y, z };
            ctx->hasCustomOffset = true;
            applyPanelSettings(*ctx);
        }

        void SetPanelScale(PanelHandle panel, float scale) noexcept override
        {
            auto* ctx = getPanelContext(panel);
            if (!ctx || !ctx->panel || scale <= 0.0f) {
                return;
            }

            ctx->scale = scale;
            ctx->hasCustomScale = true;
            applyPanelSettings(*ctx);
        }

    private:
        PanelContext* getPanelContext(PanelHandle handle) noexcept
        {
            auto it = _panels.find(handle);
            if (it == _panels.end()) {
                return nullptr;
            }
            return &it->second;
        }

        void addWidgetToTarget(PanelContext& ctx, const std::shared_ptr<VRUIWidget>& widget)
        {
            if (ctx.currentRow) {
                ctx.currentRow->addElement(widget);
                ctx.currentRow->recalculateLayout();
            } else {
                ctx.panel->addElement(widget);
            }

            ctx.panel->recalculateLayout();
        }

        void applyPanelSettings(PanelContext& ctx)
        {
            if (!ctx.panel) {
                return;
            }

            if (ctx.hasCustomScale) {
                ctx.panel->setLocalScale(ctx.scale);
            }

            if (ctx.hasCustomOffset) {
                auto& manager = VRMenuManager::get();
                if (!manager.isBoardWorldPinned()) {
                    if (auto* handNode = manager.getMenuHandNode()) {
                        ctx.panel->attachToHandNode(handNode, ctx.offset);
                    } else {
                        ctx.panel->setLocalPosition(ctx.offset);
                    }
                } else {
                    ctx.panel->setLocalPosition(ctx.offset);
                }
            }
        }

        std::unordered_map<PanelHandle, PanelContext> _panels;
        PanelHandle _nextPanelHandle = 1;
        ButtonHandle _nextButtonHandle = 1;
        uint32_t _nextRowId = 1;
    };

    DragonBoardVRAPIImpl g_api;
}

void* GetDragonBoardVRAPI(DragonBoardVR_API::InterfaceVersion version)
{
    if (version != DragonBoardVR_API::InterfaceVersion::V1) {
        logger::warn("DragonBoardVR API: unsupported interface version {}", static_cast<int>(version));
        return nullptr;
    }

    return &g_api;
}
