#include "pch.h"
#include "VRUIWidget.h"
#include "VRUISettings.h"
#include <RE/Skyrim.h>
#include <CLIBUtil/numeric.hpp>
#include <RE/B/BSEffectShaderProperty.h>
#include <RE/B/BSEffectShaderMaterial.h>
#include <RE/B/BSShaderProperty.h>
#include <RE/N/NiAlphaProperty.h>
#include <RE/B/BSModelDB.h>
#include <RE/B/BSVisit.h>
#include <RE/B/BSGeometry.h>
#include <RE/N/NiSmartPointer.h>
#include <RE/N/NiColor.h>
#include <algorithm>

namespace vrui
{
    std::map<std::string, RE::NiPointer<RE::NiNode>> VRUIWidget::_nifCache;

    void VRUIWidget::clearNifCache()
    {
        _nifCache.clear();
    }

    // =====================================================================
    // AABB
    // =====================================================================

    bool AABB::intersectsRay(const RE::NiPoint3& origin, const RE::NiPoint3& direction, float& outDistance) const
    {
        // Slab method for ray-AABB intersection
        float tmin = -std::numeric_limits<float>::infinity();
        float tmax = std::numeric_limits<float>::infinity();

        float axes[3] = { direction.x, direction.y, direction.z };
        float origins[3] = { origin.x, origin.y, origin.z };
        float mins[3] = { min.x, min.y, min.z };
        float maxs[3] = { max.x, max.y, max.z };

        for (int i = 0; i < 3; ++i) {
            if (std::abs(axes[i]) < 1e-8f) {
                if (origins[i] < mins[i] || origins[i] > maxs[i]) {
                    return false;
                }
            } else {
                float invD = 1.0f / axes[i];
                float t1 = (mins[i] - origins[i]) * invD;
                float t2 = (maxs[i] - origins[i]) * invD;
                if (t1 > t2) std::swap(t1, t2);
                tmin = (tmin > t1) ? tmin : t1;
                tmax = (tmax < t2) ? tmax : t2;
                if (tmin > tmax) {
                    return false;
                }
            }
        }

        outDistance = tmin >= 0 ? tmin : tmax;
        return outDistance >= 0;
    }

    // =====================================================================
    // VRUIWidget
    // =====================================================================

    VRUIWidget::VRUIWidget(const std::string& name, float width, float height)
        : _name(name), _width(width), _height(height)
    {
        _baseScale = 1.0f;
        _animProgress = 1.0f;
        _animDelayTimer = 0.0f;
        createNode();
    }

    VRUIWidget::~VRUIWidget()
    {
        detachFromParent();
    }

    void VRUIWidget::addChild(std::shared_ptr<VRUIWidget> child)
    {
        if (child->_parent) {
            child->detachFromParent();
        }
        child->_parent = this;
        _children.push_back(child);

        if (_node && child->_node) {
            _node->AttachChild(child->_node.get());
        }
    }

    void VRUIWidget::removeChild(const std::shared_ptr<VRUIWidget>& child)
    {
        auto it = std::find(_children.begin(), _children.end(), child);
        if (it != _children.end()) {
            child->_parent = nullptr;
            if (_node && child->_node) {
                _node->DetachChild(child->_node.get());
            }
            _children.erase(it);
        }
    }

    void VRUIWidget::attachToNode(RE::NiNode* parent)
    {
        if (parent && _node) {
            parent->AttachChild(_node.get());
            RE::NiUpdateData updateData;
            _node->Update(updateData);
            _node->UpdateWorldBound();
            logger::trace("DragonBoardVR: Widget '{}' attached to node '{}'",
                _name, parent->name.c_str());
        }
    }

    void VRUIWidget::detachFromParent()
    {
        if (_node && _node->parent) {
            _node->parent->DetachChild(_node.get());
        }
    }

    void VRUIWidget::setLocalPosition(const RE::NiPoint3& pos)
    {
        if (_node) {
            _node->local.translate = pos;
            RE::NiUpdateData updateData;
            _node->Update(updateData);
            _node->UpdateWorldBound();
        }
    }

    RE::NiPoint3 VRUIWidget::getLocalPosition() const
    {
        return _node ? _node->local.translate : RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
    }


    void VRUIWidget::setLocalScale(float scale)
    {
        _baseScale = scale;
        if (_node) {
            // If we're not currently in the middle of an animation, apply immediately
            if (_animProgress >= 1.0f) {
                _node->local.scale = scale;
                RE::NiUpdateData updateData;
                _node->Update(updateData);
            _node->UpdateWorldBound();
            }
        }
    }

    float VRUIWidget::getLocalScale() const
    {
        return _node ? _node->local.scale : _baseScale;
    }

    VRUIWidget* VRUIWidget::findWidgetByName(const std::string& name)
    {
        if (_name == name) return this;
        for (auto& child : _children) {
            auto* found = child->findWidgetByName(name);
            if (found) return found;
        }
        return nullptr;
    }

    bool VRUIWidget::isVisible() const
    {
        if (!_visible) return false;
        if (_parent) return _parent->isVisible();
        return true;
    }

    RE::NiPoint2 VRUIWidget::calculateLogicalDimensions() const
    {
        return { _width, _height };
    }
    
    void VRUIWidget::setLocalRotation(const RE::NiMatrix3& rot)
    {
        if (_node) {
            _node->local.rotate = rot;
            RE::NiUpdateData updateData;
            _node->Update(updateData);
            _node->UpdateWorldBound();
        }
    }

    RE::NiPoint3 VRUIWidget::getWorldPosition() const
    {
        if (_node) {
            return _node->world.translate;
        }
        return {};
    }

    void VRUIWidget::setVisible(bool visible)
    {
        _visible = visible;
        if (_node) {
            _node->SetAppCulled(!visible);
        }
    }

    AABB VRUIWidget::getWorldAABB() const
    {
        AABB box;
        if (_node) {
            auto pos = _node->world.translate;
            float halfW = _width * _node->world.scale * 0.5f;
            float halfH = _height * _node->world.scale * 0.5f;
            // Menu panel is in XZ plane relative to hand, with Y as depth
            box.min = { pos.x - halfW, pos.y - 0.5f, pos.z - halfH };
            box.max = { pos.x + halfW, pos.y + 0.5f, pos.z + halfH };
        }
        return box;
    }

    bool VRUIWidget::hitTest(const RE::NiPoint3& rayOriginWorld, const RE::NiPoint3& rayDirWorld, float& outDistance) const
    {
        if (!_node || !_pointerHitTestEnabled) return false;

        const bool useVisualBounds =
            _visualHitTestNode &&
            _visualHitTestWidth > 0.001f &&
            _visualHitTestHeight > 0.001f &&
            _visualHitTestDepth > 0.001f;
        const auto* hitNode = useVisualBounds ? _visualHitTestNode.get() : _node.get();
        RE::NiTransform t = hitNode->world;
        if (useVisualBounds && hitNode->worldBound.radius > 0.001f) {
            // A child mesh can remain offset from its transform origin even after
            // pivot normalization (especially modded/skinned NIFs). The rendered
            // world bound tracks the geometry centre that the player actually sees.
            t.translate = hitNode->worldBound.center;
        }
        float wScale = (t.scale > 0.001f) ? t.scale : 1.0f;

        // 1. Transform ray Origin to Node Local Space
        //    localOrigin = R^T * (worldOrigin - worldTranslate) / worldScale
        RE::NiPoint3 diff;
        diff.x = rayOriginWorld.x - t.translate.x;
        diff.y = rayOriginWorld.y - t.translate.y;
        diff.z = rayOriginWorld.z - t.translate.z;

        RE::NiPoint3 localOrigin;
        localOrigin.x = (t.rotate.entry[0][0]*diff.x + t.rotate.entry[1][0]*diff.y + t.rotate.entry[2][0]*diff.z) / wScale;
        localOrigin.y = (t.rotate.entry[0][1]*diff.x + t.rotate.entry[1][1]*diff.y + t.rotate.entry[2][1]*diff.z) / wScale;
        localOrigin.z = (t.rotate.entry[0][2]*diff.x + t.rotate.entry[1][2]*diff.y + t.rotate.entry[2][2]*diff.z) / wScale;

        // 2. Transform ray Direction to Node Local Space
        //    For DIRECTIONS: only rotate, do NOT divide by scale.
        //    (Dividing a direction by scale was the root cause of the invisibility bug —
        //     it made outDistance in the AABB test proportional to 1/scale, so when
        //     buttons were at 0.9x scale the returned worldDist was wrong.)
        RE::NiPoint3 localDir;
        localDir.x = t.rotate.entry[0][0]*rayDirWorld.x + t.rotate.entry[1][0]*rayDirWorld.y + t.rotate.entry[2][0]*rayDirWorld.z;
        localDir.y = t.rotate.entry[0][1]*rayDirWorld.x + t.rotate.entry[1][1]*rayDirWorld.y + t.rotate.entry[2][1]*rayDirWorld.z;
        localDir.z = t.rotate.entry[0][2]*rayDirWorld.x + t.rotate.entry[1][2]*rayDirWorld.y + t.rotate.entry[2][2]*rayDirWorld.z;

        // 3. AABB test in local space (X/Z = screen plane, Y = depth tolerance)
        AABB localAABB;

        float halfW = 0.0f;
        float halfH = 0.0f;
        float halfD = 0.0f;
        if (useVisualBounds) {
            // A normalized 3D preview is centred below this node. Let the hit box
            // inherit that node's transform so hover follows the visible item. Cap
            // its world size at the widget's original logical collision dimensions;
            // large Settings multipliers must not cover neighbouring controls.
            const float widgetLocalScale =
                _node->local.scale > 0.001f ? _node->local.scale : 1.0f;
            const float parentWorldScale = _node->world.scale / widgetLocalScale;
            const float requestedWidthWorld = _visualHitTestWidth * wScale;
            const float requestedHeightWorld = _visualHitTestHeight * wScale;
            const float requestedDepthWorld = _visualHitTestDepth * wScale;
            const float maxWidthWorld = _width * parentWorldScale;
            const float maxHeightWorld = _height * parentWorldScale;
            const float maxDepthWorld = 0.8f * parentWorldScale;

            halfW = (std::min)(requestedWidthWorld, maxWidthWorld) / (2.0f * wScale);
            halfH = (std::min)(requestedHeightWorld, maxHeightWorld) / (2.0f * wScale);
            halfD = (std::min)(requestedDepthWorld, maxDepthWorld) / (2.0f * wScale);
        } else {
            // Keep ordinary UI button collision world-size independent of hover scale.
            const float localS = _node->local.scale > 0.001f ? _node->local.scale : 1.0f;
            halfW = (_width * 0.5f) / localS;
            halfH = (_height * 0.5f) / localS;
            halfD = 0.4f / localS;
        }
        
        // Depth (Y) set to 0.4f (total 0.8f thickness) to be tighter than the previous 2.0f
        localAABB.min = { -halfW, -halfD, -halfH };
        localAABB.max = {  halfW,  halfD,  halfH };

        float localDist = 0.0f;
        if (!localAABB.intersectsRay(localOrigin, localDir, localDist)) return false;

        // Convert the local-space distance back to world-space distance.
        // Because localDir = R^T * worldDir (no scale applied), and the AABB
        // dimensions are in local space where 1 unit = wScale world units,
        // the world distance = localDist * wScale.
        outDistance = localDist * wScale;
        return outDistance >= 0.0f;
    }

    void VRUIWidget::setVisualHitTestBounds(RE::NiNode* node, float width, float height, float depth)
    {
        _visualHitTestNode = RE::NiPointer<RE::NiNode>(node);
        _visualHitTestWidth = width;
        _visualHitTestHeight = height;
        _visualHitTestDepth = depth;
    }

    void VRUIWidget::clearVisualHitTestBounds()
    {
        _visualHitTestNode = nullptr;
        _visualHitTestWidth = 0.0f;
        _visualHitTestHeight = 0.0f;
        _visualHitTestDepth = 0.0f;
    }

    void VRUIWidget::update(float deltaTime)
    {
        // 1. Handle Animation
        if (_animDelayTimer > 0.0f) {
            _animDelayTimer -= deltaTime;
            // Keep at zero scale while waiting
            if (_node) {
                _node->local.scale = 0.0f;
            }
        } 
        else if (_animProgress < 1.0f) {
            float animSpeed = 4.0f; // Seconds to full scale
            _animProgress += deltaTime * animSpeed;
            if (_animProgress > 1.0f) _animProgress = 1.0f;

            if (_node) {
                // Cubic Out easing: 1 - (1 - t)^3
                float t = _animProgress;
                float easedT = 1.0f - std::pow(1.0f - t, 3.0f);
                
                _node->local.scale = _baseScale * easedT;
                
                RE::NiUpdateData updateData;
                updateData.flags = RE::NiUpdateData::Flag::kDirty;
                _node->Update(updateData);
            _node->UpdateWorldBound();
            }
        }

        // 2. Update children safely (in case a child removes itself during update, e.g. when dropping a persistent button)
        auto childrenCopy = _children;
        for (auto& child : childrenCopy) {
            child->update(deltaTime);
        }
    }

    void VRUIWidget::startScaleAnimation(float delaySeconds)
    {
        _animDelayTimer = delaySeconds;
        _animProgress = 0.0f;
        if (_node) {
            _node->local.scale = 0.0f;
            RE::NiUpdateData updateData;
            updateData.flags = RE::NiUpdateData::Flag::kDirty;
            _node->Update(updateData);
            _node->UpdateWorldBound();
        }
    }

    void VRUIWidget::triggerEntranceAnimation(float& accumDelay)
    {
        for (auto& child : _children) {
            if (child && child->isVisible()) {
                child->triggerEntranceAnimation(accumDelay);
            }
        }
    }

    void VRUIWidget::onChildLayoutChanged(VRUIWidget* child)
    {
        // Default behavior: propagate up to parent if it exists
        if (_parent) {
            _parent->onChildLayoutChanged(this);
        }
    }

    void VRUIWidget::createNode()
    {
        _node.reset(RE::NiNode::Create(8));
        // Node name assignment omitted to avoid memory leaks in the Skyrim BSFixedString global pool
    }

    void VRUIWidget::initializeVisuals()
    {
        // Base implementation: no visuals to load.
        // Derived classes override this to load meshes.
    }

    // Recursively log the node tree for debugging
    static void logNodeTree(RE::NiAVObject* obj, int depth)
    {
        if (!obj) return;
        std::string indent(depth * 2, ' ');
        auto* node = obj->AsNode();
        if (node) {
            logger::trace("{}[NiNode] '{}' children={} scale={:.2f} pos=({:.1f},{:.1f},{:.1f})",
                indent, node->name.c_str(), node->GetChildren().size(), node->local.scale,
                node->local.translate.x, node->local.translate.y, node->local.translate.z);
            for (auto& child : node->GetChildren()) {
                if (child) {
                    logNodeTree(child.get(), depth + 1);
                }
            }
        } else {
            logger::trace("{}[NiAVObject] '{}' (geometry/shape)", indent, obj->name.c_str());
        }
    }

    void VRUIWidget::logNodeHierarchy(const std::string& context) const
    {
        logger::trace("DragonBoardVR: === Node Hierarchy [{}] ===", context);
        if (_node) {
            logNodeTree(_node.get(), 0);
        } else {
            logger::trace("  (null node)");
        }
    }

    // =====================================================================
    // Load a NIF mesh using BSModelDB::Demand (game's native pipeline)
    // =====================================================================

    RE::NiPointer<RE::NiNode> VRUIWidget::loadModelFromNif(const std::string& nifPath, bool applyUIShaderTweaks)
    {
        const std::string cacheKey = std::string(applyUIShaderTweaks ? "ui|" : "raw|") + nifPath;

        // First, check the cache
        auto it = _nifCache.find(cacheKey);
        if (it != _nifCache.end()) {
            // Found in cache, return a clone
            if (it->second) {
                auto* cloned = it->second->Clone();
                return RE::NiPointer<RE::NiNode>(cloned ? cloned->AsNode() : nullptr);
            }
        }

        RE::NiPointer<RE::NiNode> modelRoot;
        RE::BSModelDB::DBTraits::ArgsType args{};

        RE::BSResource::ErrorCode result = RE::BSResource::ErrorCode::kNone;
        
        std::string finalPath = nifPath;
        // Case-insensitive check and strip "meshes\" or "meshes/"
        if (finalPath.size() > 7) {
            std::string prefix = finalPath.substr(0, 7);
            for (auto& c : prefix) c = std::tolower(c);
            if (prefix == "meshes\\" || prefix == "meshes/") {
                finalPath = finalPath.substr(7);
            }
        }

        result = RE::BSModelDB::Demand(finalPath.c_str(), modelRoot, args);
        
        if (result != RE::BSResource::ErrorCode::kNone || !modelRoot) {
            logger::warn("DragonBoardVR: BSModelDB::Demand failed for '{}' (final='{}', error={})", 
                nifPath, finalPath, static_cast<int>(result));
            return nullptr;
        }

        // --- OPTIMIZATION: Clone and Sanitize ONCE before caching ---
        // CRITICAL: We MUST clone BEFORE sanitizing to avoid corrupting the engine's shared BSModelDB cache!
        auto* baseClone = modelRoot->Clone();
        if (baseClone && baseClone->AsNode()) {
            auto sanitizedNode = RE::NiPointer<RE::NiNode>(baseClone->AsNode());
            sanitizeModel(sanitizedNode.get(), applyUIShaderTweaks);
            
            // Store our specialized UI version in the cache
            _nifCache[cacheKey] = sanitizedNode;
            logger::trace("DragonBoardVR: Loaded NIF '{}' (sanitized clone, ui_tweaks={}) and cached it",
                nifPath, applyUIShaderTweaks);

            // Return a fresh clone of our UI-ready model
            auto* finalCloned = sanitizedNode->Clone();
            return RE::NiPointer<RE::NiNode>(finalCloned ? finalCloned->AsNode() : nullptr);
        }

        return nullptr;
    }

    RE::NiPointer<RE::NiNode> VRUIWidget::createQuadNode(
        const std::string& name, float width, float height,
        [[maybe_unused]] const RE::NiColorA& color)
    {
        // Create a container node for a quad panel.
        // For visual geometry, we load a built-in game mesh and rescale it.
        auto node = RE::NiPointer<RE::NiNode>(RE::NiNode::Create(2));
        if (!node) return nullptr;
        node->name = name;

        // Try loading a simple game marker mesh as a visual placeholder
        RE::NiPointer<RE::NiNode> meshNode;
        RE::BSModelDB::DBTraits::ArgsType args{};
        args.postProcess = false;  // Skip post-processing for simple use

        // Use a simple built-in mesh as the visual placeholder
    // We try DragonBoardVR\slot01.nif because we know it exists from the logs
    auto result = RE::BSModelDB::Demand("DragonBoardVR/slot01.nif", meshNode, args);
    if (result != RE::BSResource::ErrorCode::kNone) {
        result = RE::BSModelDB::Demand("markers\\movemarker01.nif", meshNode, args);
    }

        if (result == RE::BSResource::ErrorCode::kNone && meshNode) {
            // Clone and attach
            auto* cloned = meshNode->Clone();
            if (cloned && cloned->AsNode()) {
                auto* cloneNode = cloned->AsNode();
                cloneNode->local.scale = width * 0.1f;  // Scale to button size
                node->AttachChild(cloneNode);
                logger::trace("DragonBoardVR: Created visual quad '{}' with game mesh", name);
            }
        } else {
            logger::warn("DragonBoardVR: Couldn't load visual mesh for '{}', using empty node", name);
        }

        return node;
    }

    void VRUIWidget::sanitizeModel(RE::NiAVObject* a_obj, bool applyUIShaderTweaks)
    {
        if (!a_obj) return;

        // The cloned root must be visible, but world-item NIFs often contain
        // intentionally hidden alternate parts. Preserve those child culling
        // flags so neither the preview nor its measured bounds include them.
        a_obj->SetAppCulled(false);

        // 1. Strip Physics and Animations. Only DragonBoard-owned UI meshes
        // are safe to force visible recursively.
        RE::BSVisit::TraverseScenegraphObjects(a_obj, [applyUIShaderTweaks](RE::NiAVObject* obj) -> RE::BSVisit::BSVisitControl {
            obj->collisionObject = nullptr;
            obj->controllers = nullptr;
            if (applyUIShaderTweaks) {
                obj->SetAppCulled(false);
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });

        // 2. Shader/property sanitization.
        // Always disable shadow participation for UI-attached assets, even when we keep
        // their original material behavior. World-pinned tablets otherwise get pulled
        // into the normal scene shadow/light cost path.
        RE::BSVisit::TraverseScenegraphGeometries(a_obj, [applyUIShaderTweaks](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
            if (geom) {
                auto* prop = geom->lightingShaderProp_cast();
                if (prop) {
                    prop->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kCastShadows);
                    prop->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kReceiveShadows);
                }

                auto* effect = geom->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect].get();
                if (auto* shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect); shaderProp) {
                    shaderProp->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kCastShadows);
                    shaderProp->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kReceiveShadows);
                }

                if (applyUIShaderTweaks && prop) {
                    prop->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kMenuScreen);
                    prop->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kNoFade);
                    prop->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferWrite);
                    prop->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kZBufferTest);
                    prop->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kScreendoorAlphaFade);
                }
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }
}
