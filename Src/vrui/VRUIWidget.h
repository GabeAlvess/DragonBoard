#pragma once

#include <RE/Skyrim.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vrui
{
    /// Degrees-to-radians conversion constant (avoids magic numbers everywhere)
    inline constexpr float kDegToRad = 3.14159265f / 180.0f;

    /// Axis-Aligned Bounding Box for hit testing
    struct AABB
    {
        RE::NiPoint3 min;
        RE::NiPoint3 max;

        bool intersectsRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float& outDistance) const;
    };

    enum class EquipHand {
        kLeft,
        kRight
    };

    /// Base class for all VR UI elements.
    /// Each widget wraps a NiNode in the scene graph with position, size, and hit-test support.
    class VRUIWidget : public std::enable_shared_from_this<VRUIWidget>
    {
    public:
        explicit VRUIWidget(const std::string& name, float width = 3.0f, float height = 1.5f);
        virtual ~VRUIWidget();

        // --- Hierarchy ---
        void addChild(std::shared_ptr<VRUIWidget> child);
        void removeChild(const std::shared_ptr<VRUIWidget>& child);
        const std::vector<std::shared_ptr<VRUIWidget>>& getChildren() const { return _children; }
        VRUIWidget* getParent() const { return _parent; }

        // --- Scene Graph ---
        RE::NiNode* getNode() const { return _node.get(); }
        void attachToNode(RE::NiNode* parent);
        void detachFromParent();

        // --- Transform ---
        void setLocalPosition(const RE::NiPoint3& pos);
        virtual void setLocalScale(float scale);
        void setLocalRotation(const RE::NiMatrix3& rot);
        
        RE::NiPoint3 getLocalPosition() const;
        float getLocalScale() const;
        RE::NiPoint3 getWorldPosition() const;

        // --- Visibility ---
        void setVisible(bool visible);
        virtual bool isVisible() const;

        void setPage(int page);
        virtual int getPageSize() const { return 0; }

        /// Find a child widget by name (recursive)
        VRUIWidget* findWidgetByName(const std::string& name);

        // --- Hit Testing ---
        AABB getWorldAABB() const;
        virtual bool hitTest(const RE::NiPoint3& rayOriginWorld, const RE::NiPoint3& rayDirWorld, float& outDistance) const;
        void setVisualHitTestBounds(RE::NiNode* node, float width, float height, float depth = 0.8f);
        void clearVisualHitTestBounds();
        void setPointerHitTestEnabled(bool enabled) { _pointerHitTestEnabled = enabled; }

        virtual RE::NiPoint2 calculateLogicalDimensions() const;

        void setDimensions(float w, float h) { _width = w; _height = h; }
        float getWidth() const { return _width; }
        float getHeight() const { return _height; }

        // --- Input Events ---
        virtual void onRayEnter() {}
        virtual void onRayExit() {}
        virtual void onTriggerPress(EquipHand hand = EquipHand::kRight) {}
        virtual void onTriggerRelease(EquipHand hand = EquipHand::kRight) {}
        virtual void onTriggerLongPress(EquipHand hand = EquipHand::kRight) {}
        virtual void onSecondaryPress() {}
        virtual void onSecondaryRelease() {}
        virtual void onSecondaryLongPress() {}

        // --- Per-Frame ---
        virtual void update(float deltaTime);
        virtual void recalculateLayout() {}
        
        /// Notification from child that it changed size/layout
        virtual void onChildLayoutChanged(VRUIWidget* child);

        // --- Animation ---
        /// Trigger a scale-up animation with the specified frame delay
        void startScaleAnimation(float delaySeconds);
        /// Recursively cascades the entrance animation.
        virtual void triggerEntranceAnimation(float& accumDelay);

        // --- Name ---
        const std::string& getName() const { return _name; }

        /// Override in subclasses to load meshes AFTER construction (vtable is ready).
        /// Called manually at the end of derived class constructors.
        virtual void initializeVisuals();
 
        const std::string& getLayoutId() const { return _layoutId; }
        void setLayoutId(const std::string& id) { _layoutId = id; }
 
        /// Helper: load a NIF model using BSModelDB::Demand (game's native pipeline)
        static RE::NiPointer<RE::NiNode> loadModelFromNif(const std::string& nifPath, bool applyUIShaderTweaks = true);
        static void clearNifCache();

        /// Recursive helper to strip physics/animations and ensure visibility
        static void sanitizeModel(RE::NiAVObject* a_obj, bool applyUIShaderTweaks = true);
        static void normalizePhysicalMaterialLighting(
            RE::NiAVObject* a_obj, bool allowCastShadows = true);

    protected:
        /// Creates the base NiNode. NOT virtual - safe to call from base constructor.
        void createNode();

        /// Helper: creates a flat quad mesh (two triangles) as NiTriShape
        static RE::NiPointer<RE::NiNode> createQuadNode(
            const std::string& name, float width, float height,
            const RE::NiColorA& color);

        /// Debug: log the node hierarchy starting from this widget's node
        void logNodeHierarchy(const std::string& context) const;

        std::string _name;
        float _width;
        float _height;
        bool _visible = true;

        // Animation state
        float _baseScale = 1.0f;
        float _animProgress = 1.0f; // 1.0 = finished
        float _animDelayTimer = 0.0f;

        RE::NiPointer<RE::NiNode> _node;
        RE::NiPointer<RE::NiNode> _visualHitTestNode;
        float _visualHitTestWidth = 0.0f;
        float _visualHitTestHeight = 0.0f;
        float _visualHitTestDepth = 0.0f;
        bool _pointerHitTestEnabled = true;
        std::string _layoutId;
 
        static std::map<std::string, RE::NiPointer<RE::NiNode>> _nifCache;

        VRUIWidget* _parent = nullptr;
        std::vector<std::shared_ptr<VRUIWidget>> _children;
    };
}
