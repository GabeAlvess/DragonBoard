#include "VRUIContainer.h"
#include <cmath>
#include <cmath>
#include <algorithm>
#include "VRUISettings.h"
#include "VRUIButton.h"
#include "VRMenuManager.h"

// Cleaned macros
namespace vrui
{
    RE::NiPoint2 VRUIContainer::calculateLogicalDimensions() const
    {
        const auto& children = getChildren();
        if (children.empty()) return { 0.0f, 0.0f };

        float minX = 1e6f, maxX = -1e6f;
        float minZ = 1e6f, maxZ = -1e6f;
        bool foundVisible = false;

        for (const auto& child : children) {
            if (!child->isVisible()) continue;

            // Do not expand the container bounding box if the button is grabbed or already floating!
            // This prevents the entire grid/tablet from shifting its center when a button is dragged far away.
            if (auto* btn = dynamic_cast<VRUIButton*>(child.get())) {
                if (btn->isGrabbed()) continue;
                int slot = btn->getSlotIndex();
                if (slot >= 0 && slot < VRUISettings::kMaxSlots && VRUISettings::get().slotFloating[slot]) continue;
                if (btn->isPersistent()) continue;
            }

            RE::NiPoint3 pos = child->getLocalPosition();
            RE::NiPoint2 size = child->calculateLogicalDimensions();

            float halfW = size.x * 0.5f;
            float halfH = size.y * 0.5f;

            float left = pos.x - halfW;
            float right = pos.x + halfW;
            float top = pos.z + halfH;
            float bottom = pos.z - halfH;

            minX = std::min(minX, left);
            maxX = std::max(maxX, right);
            minZ = std::min(minZ, bottom);
            maxZ = std::max(maxZ, top);
            foundVisible = true;
        }

        if (!foundVisible) {
            return { _width > 0.0f ? _width : 0.0f, _height > 0.0f ? _height : 0.0f };
        }
        return { maxX - minX, maxZ - minZ };
    }

    VRUIContainer::VRUIContainer(const std::string& name,
                                 ContainerLayout layout,
                                 float spacingX,
                                 float spacingY,
                                 float spacingZ,
                                 float scale)
        : VRUIWidget(name, 0, 0)  // Container has no intrinsic size
        , _layout(layout)
        , _spacingX(spacingX)
        , _spacingY(spacingY)
        , _spacingZ(spacingZ)
        , _pageSize(0)
        , _currentPage(0)
    {
        setLocalScale(scale);
    }

    void VRUIContainer::addElement(std::shared_ptr<VRUIWidget> element)
    {
        addChild(std::move(element));
        onChildLayoutChanged(nullptr); // Initial layout pass
    }

    void VRUIContainer::removeElement(const std::shared_ptr<VRUIWidget>& element)
    {
        removeChild(element);
        onChildLayoutChanged(nullptr);
    }

    void VRUIContainer::clearElements()
    {
        // Dropping hover to prevent dangling pointers from active elements
        VRMenuManager::get().clearHover();

        auto children = getChildren();  // copy
        for (auto& child : children) {
            removeChild(child);
        }
    }

    void VRUIContainer::setLayout(ContainerLayout layout)
    {
        _layout = layout;
        recalculateLayout();
    }

    void VRUIContainer::setSpacing(float spacingX, float spacingY, float spacingZ)
    {
        _spacingX = spacingX;
        _spacingY = spacingY;
        _spacingZ = spacingZ;
        recalculateLayout();
    }

    void VRUIContainer::setGridColumns(int cols)
    {
        _gridColumns = std::max(1, cols);
        recalculateLayout();
    }

    void VRUIContainer::setPageSize(int size)
    {
        _pageSize = std::max(0, size);
        _currentPage = 0;
        recalculateLayout();
    }

    void VRUIContainer::setPage(int page)
    {
        int total = getTotalPages();
        if (total > 0) {
            _currentPage = std::clamp(page, 0, total - 1);
        } else {
            _currentPage = 0;
        }
        recalculateLayout();
    }

    int VRUIContainer::getTotalPages() const
    {
        if (_pageSize <= 0) return 1;
        int numVisible = static_cast<int>(_children.size());
        if (numVisible == 0) return 1;
        return static_cast<int>(std::ceil((float)numVisible / _pageSize));
    }

    void VRUIContainer::recalculateLayout()
    {
        _layoutDirty = false;

        const auto& allChildren = getChildren();

        // Recursively trigger layout update for all children containers first
        for (auto& child : allChildren) {
            child->recalculateLayout();
        }

        if (allChildren.empty()) return;

        // Filter out floating buttons so they don't affect layout mathematics or create gaps -- unless it's a Grid Layout
        std::vector<std::shared_ptr<VRUIWidget>> children;
        auto& settings = VRUISettings::get();
        for (auto& child : allChildren) {
            bool isFloating = false;
            if (_layout != ContainerLayout::Grid) {
                // Use RTTI to detect VRUIButtons
                if (auto* btn = dynamic_cast<VRUIButton*>(child.get())) {
                    int slot = btn->getSlotIndex();
                    if (slot >= 0 && slot < VRUISettings::kMaxSlots && settings.slotFloatingCache[slot]) {
                        isFloating = true;
                    }
                }
            }
            if (!isFloating) {
                children.push_back(child);
            }
        }

        if (children.empty()) return;

        switch (_layout) {
        case ContainerLayout::HorizontalCenter: {
            // Calculate total width of visible children
            float totalWidth = 0.0f;
            int visibleCount = 0;
            for (const auto& child : children) {
                if (child->isVisible()) {
                    totalWidth += child->calculateLogicalDimensions().x;
                    visibleCount++;
                }
            }
            if (visibleCount > 1) totalWidth += _spacingX * (visibleCount - 1);

            // Position centered
            float currentX = -totalWidth * 0.5f;
            for (const auto& child : children) {
                if (!child->isVisible()) continue;
                float childW = child->calculateLogicalDimensions().x;
                child->setLocalPosition(RE::NiPoint3{ currentX + childW * 0.5f, 0.0f, 0.0f });
                currentX += childW + _spacingX;
            }
            break;
        }

        case ContainerLayout::VerticalDown: {
            float currentZ = 0.0f; // Start from top
            for (const auto& child : children) {
                if (!child->isVisible()) continue;
                float childH = child->calculateLogicalDimensions().y;
                // Move currentZ half-way into this widget, position it, then move the rest out
                child->setLocalPosition(RE::NiPoint3{ 0.0f, 0.0f, currentZ - childH * 0.5f });
                currentZ -= (childH + _spacingY);
            }
            break;
        }

        case ContainerLayout::VerticalUp: {
            float currentZ = 0.0f; // Start from bottom
            for (const auto& child : children) {
                if (!child->isVisible()) continue;
                float childH = child->calculateLogicalDimensions().y;
                child->setLocalPosition(RE::NiPoint3{ 0.0f, 0.0f, currentZ + childH * 0.5f });
                currentZ += (childH + _spacingY);
            }
            break;
        }

        case ContainerLayout::Grid: {
            int numChildren = static_cast<int>(children.size());
            if (numChildren == 0) break;

            int itemsPerPage = _pageSize > 0 ? _pageSize : numChildren;
            int startIndex = _currentPage * itemsPerPage;
            int endIndex = std::min(startIndex + itemsPerPage, numChildren);

            // Hide everything first
            for (auto& child : children) {
                child->setVisible(false);
            }

            int rows = (itemsPerPage + _gridColumns - 1) / _gridColumns;
            int cols = std::min(itemsPerPage, _gridColumns);

            // Use first child to determine grid scale cell footprint
            float cellW = children[0]->getWidth();
            float cellH = children[0]->getHeight();

            float totalWidth = (cols * cellW) + ((cols - 1) * _spacingX);
            float totalHeight = (rows * cellH) + ((rows - 1) * _spacingY);

            float startX = totalWidth * 0.5f - (cellW * 0.5f);
            float startZ = totalHeight * 0.5f - (cellH * 0.5f);

            // Grid operates on an absolute mapped index relative to the page offset
            for (int i = startIndex; i < endIndex; ++i) {
                // Determine absolute visual index for this child on the current page
                int pageRelativeIdx = i - startIndex;
                
                int r = pageRelativeIdx / _gridColumns;
                int c = pageRelativeIdx % _gridColumns;


                float cx = startX - c * (cellW + _spacingX);
                float cz = startZ - r * (cellH + _spacingY);

                bool shouldHideSlot = false;
                if (auto* btn = dynamic_cast<VRUIButton*>(children[i].get())) {
                    int slot = btn->getSlotIndex();
                    if (slot >= 0 && slot < VRUISettings::kMaxSlots) {
                        // Crux of the fix: A slot floating or set to "None" retains its matrix position
                        // but is visually invisible. It does NOT steal its siblings' indexes.
                        if (settings.slotFloatingCache[slot] || settings.slotActions[slot] == "None") {
                            shouldHideSlot = true;
                        }
                    }
                }

                if (shouldHideSlot) {
                    children[i]->setVisible(false);
                } else {
                    children[i]->setVisible(true);
                    children[i]->setLocalPosition(RE::NiPoint3{ cx, 0.0f, cz });
                }
            }
            break;
        }

        case ContainerLayout::Free:
            // No automatic layout
            break;
        }

        // --- FINAL DIMENSION UPDATE ---
        // Now that children are positioned, update our own reported width/height 
        // based on the logical bounds of all visible children.
        RE::NiPoint2 dims = calculateLogicalDimensions();
        _width = dims.x;
        _height = dims.y;
    }

    void VRUIContainer::onChildLayoutChanged([[maybe_unused]] VRUIWidget* child)
    {
        // When a child changes layout (e.g. dynamic refresh), mark our layout as dirty
        _layoutDirty = true;
        recalculateLayout();

        // Propagate up to parent (important for nested containers!)
        if (_parent) {
            _parent->onChildLayoutChanged(this);
        }
    }

}
