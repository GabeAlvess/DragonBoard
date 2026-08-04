#include "pch.h"

#include "ui/rml/RmlSurfaceGrabController.h"

#include "ui/input/GripThumbScale.h"

#include <algorithm>
#include <cmath>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr float kGrabSmoothSpeed = 14.0f;

        RE::NiPoint3 RotateVector(const RE::NiMatrix3& rotation, const RE::NiPoint3& value)
        {
            return rotation * value;
        }

        RE::NiPoint3 InverseRotateVector(
            const RE::NiMatrix3& rotation, const RE::NiPoint3& value)
        {
            return rotation.Transpose() * value;
        }

        RE::NiMatrix3 Orthonormalize(RE::NiMatrix3 matrix)
        {
            RE::NiPoint3 x(matrix.entry[0][0], matrix.entry[1][0], matrix.entry[2][0]);
            RE::NiPoint3 y(matrix.entry[0][1], matrix.entry[1][1], matrix.entry[2][1]);
            if (x.SqrLength() <= 1.0e-8f || y.SqrLength() <= 1.0e-8f) {
                return RE::NiMatrix3{};
            }
            x = x / x.Length();
            y = y - x * (x.x * y.x + x.y * y.y + x.z * y.z);
            if (y.SqrLength() <= 1.0e-8f) return RE::NiMatrix3{};
            y = y / y.Length();
            const RE::NiPoint3 z(
                x.y * y.z - x.z * y.y,
                x.z * y.x - x.x * y.z,
                x.x * y.y - x.y * y.x);
            matrix.entry[0][0] = x.x;
            matrix.entry[1][0] = x.y;
            matrix.entry[2][0] = x.z;
            matrix.entry[0][1] = y.x;
            matrix.entry[1][1] = y.y;
            matrix.entry[2][1] = y.z;
            matrix.entry[0][2] = z.x;
            matrix.entry[1][2] = z.y;
            matrix.entry[2][2] = z.z;
            return matrix;
        }
    }

    void RmlSurfaceGrabController::SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (!enabled) Reset();
    }

    void RmlSurfaceGrabController::Reset()
    {
        _grabbed = false;
        _thumbScaling = false;
        _holdSeconds = 0.0f;
    }

    bool RmlSurfaceGrabController::BeginGrab(RE::NiNode* surfaceNode, RE::NiNode* hand)
    {
        if (!surfaceNode || !surfaceNode->parent || !hand) return false;
        _grabOffsetLocalHand = InverseRotateVector(
            hand->world.rotate,
            surfaceNode->world.translate - hand->world.translate);
        _grabInitialHandRotation = hand->world.rotate;
        _grabInitialSurfaceWorldRotation = surfaceNode->world.rotate;
        _grabbed = true;
        _holdSeconds = 0.0f;
        return true;
    }

    RmlSurfaceGrabController::UpdateResult RmlSurfaceGrabController::Update(
        RE::NiNode* surfaceNode,
        const Input& input,
        float deltaTime)
    {
        UpdateResult result;
        if (!_enabled || !surfaceNode || !surfaceNode->parent) {
            Reset();
            return result;
        }

        if (!_grabbed) {
            const bool hoverAccepted = !input.requireHover || input.hovered;
            if (hoverAccepted && input.dominantGripDown && input.dominantHand) {
                _holdSeconds += std::max(deltaTime, 0.0f);
                if (_holdSeconds >= std::max(input.grabHoldSeconds, 0.0f) &&
                    BeginGrab(surfaceNode, input.dominantHand)) {
                    result.grabStarted = true;
                }
            } else {
                _holdSeconds = 0.0f;
            }
            return result;
        }

        if (!input.dominantGripDown || !input.dominantHand) {
            _grabbed = false;
            _thumbScaling = false;
            result.grabEnded = true;
            return result;
        }

        auto* parent = surfaceNode->parent->AsNode();
        if (!parent || std::abs(parent->world.scale) <= 1.0e-5f) return result;

        const float blend = std::clamp(
            1.0f - std::exp(-kGrabSmoothSpeed * std::max(deltaTime, 0.0f)),
            0.0f,
            1.0f);
        const RE::NiPoint3 targetWorld = input.dominantHand->world.translate +
            RotateVector(input.dominantHand->world.rotate, _grabOffsetLocalHand);
        const RE::NiMatrix3 targetWorldRotation = input.dominantHand->world.rotate *
            (_grabInitialHandRotation.Transpose() * _grabInitialSurfaceWorldRotation);
        const RE::NiPoint3 targetLocalPosition = InverseRotateVector(
            parent->world.rotate,
            targetWorld - parent->world.translate) / parent->world.scale;
        surfaceNode->local.translate +=
            (targetLocalPosition - surfaceNode->local.translate) * blend;

        const RE::NiMatrix3 targetLocalRotation =
            parent->world.rotate.Transpose() * targetWorldRotation;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                surfaceNode->local.rotate.entry[row][column] +=
                    (targetLocalRotation.entry[row][column] -
                     surfaceNode->local.rotate.entry[row][column]) * blend;
            }
        }
        surfaceNode->local.rotate = Orthonormalize(surfaceNode->local.rotate);

        const auto scaleResult = dragonboard::ui::input::ApplyGripThumbScale(
            surfaceNode->local.scale,
            input.thumbstickY,
            deltaTime,
            input.minimumScale,
            input.maximumScale);
        _thumbScaling = scaleResult.active;
        if (scaleResult.changed) {
            surfaceNode->local.scale = scaleResult.scale;
        }

        if (input.updateSceneGraph) {
            RE::NiUpdateData updateData;
            updateData.flags = RE::NiUpdateData::Flag::kDirty;
            surfaceNode->Update(updateData);
        }
        result.transformChanged = true;
        return result;
    }
}
