#pragma once

#include <array>
#include <cstddef>

namespace vrui
{
    inline constexpr std::size_t kMapCalibrationPointCount = 5;

    struct MapCalibrationPoint
    {
        bool valid = false;
        float worldX = 0.0f;
        float worldY = 0.0f;
        float mapU = 0.0f;
        float mapV = 0.0f;
    };

    struct MapCalibrationTransform
    {
        std::array<double, 3> x{};
        std::array<double, 3> y{};
        float rmsError = 0.0f;
        std::size_t pointCount = 0;
    };

    [[nodiscard]] bool FitMapCalibration(
        const std::array<MapCalibrationPoint, kMapCalibrationPointCount>& points,
        MapCalibrationTransform& transform);

    [[nodiscard]] bool IsMapCalibrationUsable(const MapCalibrationTransform& transform);

    // Returns a city landmark in normalized coordinates of the correctly
    // oriented map artwork. This is deliberately independent of RmlUi and of
    // the DragonBoard mesh transform.
    [[nodiscard]] bool GetMapCalibrationLandmarkUv(
        std::size_t cityIndex,
        float& mapU,
        float& mapV);

    [[nodiscard]] bool MapWorldToTextureUv(
        const std::array<MapCalibrationPoint, kMapCalibrationPointCount>& points,
        float worldX,
        float worldY,
        float& mapU,
        float& mapV,
        float* rmsError = nullptr);
}
