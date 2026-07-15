#include "VRUISlider.h"
#include "VRUISettings.h"
#include "VRMenuManager.h"
#include <cmath>

namespace vrui
{
    VRUISlider::VRUISlider(const std::string& name, float minValue, float maxValue, float defaultValue, float width, float height)
        : VRUIWidget(name, width, height)
        , _minValue(minValue)
        , _maxValue(maxValue)
        , _currentValue(defaultValue)
    {
        initializeVisuals();
    }

    void VRUISlider::initializeVisuals()
    {
        auto& settings = VRUISettings::get();
        // Button Mesh Default Orientations
        constexpr float radX = 90.0f  * kDegToRad;
        constexpr float radY =  0.0f  * kDegToRad;
        constexpr float radZ = 180.0f * kDegToRad;

        // 1. Create Background Track (Segmented Bar)
        _backgroundTrack = RE::NiPointer<RE::NiNode>(RE::NiNode::Create());
        _backgroundTrack->name = _name + "_track";
        
        int segments = 40;
        float segmentStep = _width / (float)segments;

        for (int i = 0; i < segments; ++i) {
            auto segment = createQuadNode(_name + "_seg_" + std::to_string(i), segmentStep * 1.1f, _height * 0.3f, { 0.15f, 0.15f, 0.15f, 0.9f });
            if (segment) {
                float x = -_width * 0.5f + (i * segmentStep) + (segmentStep * 0.5f);
                segment->local.translate.x = x;
                segment->local.rotate.SetEulerAnglesXYZ(radX, radY, radZ);
                segment->local.scale = settings.buttonMeshScale; // Match general mesh scale
                _backgroundTrack->AttachChild(segment.get());
            }
        }

        if (_node && _backgroundTrack) {
            _node->AttachChild(_backgroundTrack.get());
        }

        // 2. Create Handle (the slider knob)
        _handle = createQuadNode(_name + "_handle", _height * 1.5f, _height * 1.5f, { 1.0f, 1.0f, 1.0f, 1.0f });
        
        if (_node && _handle) {
            _handle->local.rotate.SetEulerAnglesXYZ(radX, radY, radZ);
            _handle->local.scale = settings.buttonMeshScale * 1.2f; // Slightly larger handle
            _node->AttachChild(_handle.get());
        }

        updateHandlePosition();
    }

    void VRUISlider::setValue(float value, bool triggerCallback)
    {
        float clamped = std::clamp(value, _minValue, _maxValue);
        if (std::abs(_currentValue - clamped) > 0.0001f) {
            _currentValue = clamped;
            updateHandlePosition();
            if (triggerCallback && _onValueChanged) {
                _onValueChanged(_currentValue);
            }
        }
    }

    void VRUISlider::onRayEnter()
    {
        _isHovered = true;
        if (_handle) _handle->local.scale = VRUISettings::get().buttonMeshScale * 1.2f; // Visual feedback
    }

    void VRUISlider::onRayExit()
    {
        _isHovered = false;
        if (!_isDragging && _handle) {
            _handle->local.scale = VRUISettings::get().buttonMeshScale;
        }
    }

    void VRUISlider::onTriggerPress(EquipHand hand)
    {
        _isDragging = true;
    }

    void VRUISlider::onTriggerRelease(EquipHand hand)
    {
        _isDragging = false;
        if (!_isHovered && _handle) {
            _handle->local.scale = VRUISettings::get().buttonMeshScale;
        }
    }

    void VRUISlider::update(float deltaTime)
    {
        VRUIWidget::update(deltaTime);

        if (_isDragging) {
            auto& manager = VRMenuManager::get();
            RE::NiPoint3 origin = manager.getLaserOrigin();
            RE::NiPoint3 dir = manager.getLaserDirection();

            setValue(calculateValueFromRay(origin, dir));
        }
    }

    void VRUISlider::updateHandlePosition()
    {
        if (!_handle) return;

        float range = _maxValue - _minValue;
        float percent = (range > 0.0001f) ? (_currentValue - _minValue) / range : 0.5f;
        
        // Map 0-1 to local coordinate X from -width/2 to +width/2
        float localX = (percent - 0.5f) * _width;
        
        _handle->local.translate.x = localX;
        _handle->local.translate.y = 0.2f; // Slightly in front of track
        _handle->local.translate.z = 0.0f;
        
        RE::NiUpdateData updateData;
        _handle->Update(updateData);
    }

    float VRUISlider::calculateValueFromRay(const RE::NiPoint3& worldOrigin, const RE::NiPoint3& worldDir)
    {
        if (!_node) return _currentValue;
        if (std::abs(_width) <= 0.0001f) return _currentValue;

        const auto& t = _node->world;
        float safeScale = (std::abs(t.scale) > 0.0001f) ? t.scale : 1.0f;
        
        // 1. Transform ray to Node Local Space
        RE::NiPoint3 diff = worldOrigin - t.translate;
        
        RE::NiPoint3 localOrigin;
        localOrigin.x = (t.rotate.entry[0][0]*diff.x + t.rotate.entry[1][0]*diff.y + t.rotate.entry[2][0]*diff.z) / safeScale;
        localOrigin.y = (t.rotate.entry[0][1]*diff.x + t.rotate.entry[1][1]*diff.y + t.rotate.entry[2][1]*diff.z) / safeScale;
        localOrigin.z = (t.rotate.entry[0][2]*diff.x + t.rotate.entry[1][2]*diff.y + t.rotate.entry[2][2]*diff.z) / safeScale;

        RE::NiPoint3 localDir;
        localDir.x = (t.rotate.entry[0][0]*worldDir.x + t.rotate.entry[1][0]*worldDir.y + t.rotate.entry[2][0]*worldDir.z); // Direction doesn't need scale
        localDir.y = (t.rotate.entry[0][1]*worldDir.x + t.rotate.entry[1][1]*worldDir.y + t.rotate.entry[2][1]*worldDir.z);
        localDir.z = (t.rotate.entry[0][2]*worldDir.x + t.rotate.entry[1][2]*worldDir.y + t.rotate.entry[2][2]*worldDir.z);

        // 2. Intersect with the local XY plane (where Z=0)
        // Ray: P = localOrigin + t * localDir
        // Plane: P.y = 0 (since our widgets are oriented such that Y is depth)
        // localOrigin.y + t * localDir.y = 0  => t = -localOrigin.y / localDir.y
        
        if (std::abs(localDir.y) < 0.0001f) return _currentValue;

        float hitT = -localOrigin.y / localDir.y;
        if (hitT < 0.0f) return _currentValue;

        RE::NiPoint3 hitLoc = localOrigin + localDir * hitT;

        // 3. Map hitLoc.X to value
        // Normalize X from [-width/2, +width/2] to [0, 1]
        float percent = (hitLoc.x / _width) + 0.5f;
        percent = std::clamp(percent, 0.0f, 1.0f);

        return _minValue + percent * (_maxValue - _minValue);
    }
}
