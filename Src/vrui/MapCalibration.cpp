#include "MapCalibration.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vrui
{
    namespace
    {
        struct NormalizedLandmark
        {
            float u;
            float v;
        };

        // Pixel positions measured on the correctly oriented 2216x1536 map
        // cropped from DragonBoardMat_Tex.dds. Order matches the developer UI:
        // Whiterun, Riften, Solitude, Falkreath and Windhelm. Windhelm is the
        // horse-head shield beside the White River; the northern crown shield
        // is Winterhold and must not be used for this calibration point.
        constexpr std::array<NormalizedLandmark, kMapCalibrationPointCount> kCityLandmarks{
            // Fine-tuned from perspective-corrected in-game captures. Samples
            // are actual world locations inside each region, not assumptions
            // that the player is standing at a city icon. Adjusting the fan
            // vertices preserves the piecewise interpolation without a global warp.
            NormalizedLandmark{ 1050.357f / 2216.0f, 856.230f / 1536.0f },
            NormalizedLandmark{ 1845.010f / 2216.0f, 1224.777f / 1536.0f },
            NormalizedLandmark{ 614.099f / 2216.0f, 348.655f / 1536.0f },
            NormalizedLandmark{ 772.317f / 2216.0f, 1226.369f / 1536.0f },
            NormalizedLandmark{ 1636.541f / 2216.0f, 649.668f / 1536.0f }
        };

        // A four-triangle fan around Whiterun covers the province using the
        // geographic order of the remaining calibration cities. Piecewise
        // barycentric interpolation passes through every captured landmark
        // exactly, unlike the previous global affine least-squares fit.
        constexpr std::array<std::array<std::size_t, 3>, 4> kCalibrationTriangles{
            std::array<std::size_t, 3>{ 0, 2, 4 },
            std::array<std::size_t, 3>{ 0, 4, 1 },
            std::array<std::size_t, 3>{ 0, 1, 3 },
            std::array<std::size_t, 3>{ 0, 3, 2 }
        };

        bool WorldBarycentric(
            float worldX,
            float worldY,
            const MapCalibrationPoint& a,
            const MapCalibrationPoint& b,
            const MapCalibrationPoint& c,
            std::array<double, 3>& weights)
        {
            const double denominator =
                (b.worldY - c.worldY) * (a.worldX - c.worldX) +
                (c.worldX - b.worldX) * (a.worldY - c.worldY);
            if (std::abs(denominator) <= 1.0e-6) return false;
            weights[0] =
                ((b.worldY - c.worldY) * (worldX - c.worldX) +
                    (c.worldX - b.worldX) * (worldY - c.worldY)) /
                denominator;
            weights[1] =
                ((c.worldY - a.worldY) * (worldX - c.worldX) +
                    (a.worldX - c.worldX) * (worldY - c.worldY)) /
                denominator;
            weights[2] = 1.0 - weights[0] - weights[1];
            return true;
        }

        bool Solve3x3(double matrix[3][3], const double values[3], std::array<double, 3>& result)
        {
            double augmented[3][4]{};
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    augmented[row][column] = matrix[row][column];
                }
                augmented[row][3] = values[row];
            }

            for (std::size_t pivot = 0; pivot < 3; ++pivot) {
                std::size_t best = pivot;
                for (std::size_t row = pivot + 1; row < 3; ++row) {
                    if (std::abs(augmented[row][pivot]) > std::abs(augmented[best][pivot])) best = row;
                }
                if (std::abs(augmented[best][pivot]) < 1.0e-9) return false;
                if (best != pivot) {
                    for (std::size_t column = pivot; column < 4; ++column) {
                        std::swap(augmented[pivot][column], augmented[best][column]);
                    }
                }

                const double divisor = augmented[pivot][pivot];
                for (std::size_t column = pivot; column < 4; ++column) {
                    augmented[pivot][column] /= divisor;
                }
                for (std::size_t row = 0; row < 3; ++row) {
                    if (row == pivot) continue;
                    const double factor = augmented[row][pivot];
                    for (std::size_t column = pivot; column < 4; ++column) {
                        augmented[row][column] -= factor * augmented[pivot][column];
                    }
                }
            }

            for (std::size_t row = 0; row < 3; ++row) result[row] = augmented[row][3];
            return true;
        }
    }

    bool FitMapCalibration(
        const std::array<MapCalibrationPoint, kMapCalibrationPointCount>& points,
        MapCalibrationTransform& transform)
    {
        transform = {};
        double normal[3][3]{};
        double targetX[3]{};
        double targetY[3]{};

        for (const auto& point : points) {
            if (!point.valid) continue;
            const double row[3]{ 1.0, point.worldX, point.worldY };
            for (std::size_t i = 0; i < 3; ++i) {
                targetX[i] += row[i] * point.mapU;
                targetY[i] += row[i] * point.mapV;
                for (std::size_t j = 0; j < 3; ++j) normal[i][j] += row[i] * row[j];
            }
            ++transform.pointCount;
        }
        if (transform.pointCount < 3) return false;

        double normalX[3][3]{};
        double normalY[3][3]{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                normalX[row][column] = normal[row][column];
                normalY[row][column] = normal[row][column];
            }
        }
        if (!Solve3x3(normalX, targetX, transform.x) ||
            !Solve3x3(normalY, targetY, transform.y)) {
            return false;
        }

        double squaredError = 0.0;
        for (const auto& point : points) {
            if (!point.valid) continue;
            const double predictedX = transform.x[0] + transform.x[1] * point.worldX +
                transform.x[2] * point.worldY;
            const double predictedY = transform.y[0] + transform.y[1] * point.worldX +
                transform.y[2] * point.worldY;
            const double dx = predictedX - point.mapU;
            const double dy = predictedY - point.mapV;
            squaredError += dx * dx + dy * dy;
        }
        transform.rmsError = static_cast<float>(
            std::sqrt(squaredError / static_cast<double>(transform.pointCount)));
        return true;
    }

    bool MapWorldToTextureUv(
        const std::array<MapCalibrationPoint, kMapCalibrationPointCount>& points,
        float worldX,
        float worldY,
        float& mapU,
        float& mapV,
        float* rmsError)
    {
        MapCalibrationTransform transform;
        if (!FitMapCalibration(points, transform) || !IsMapCalibrationUsable(transform)) return false;

        const std::array<std::size_t, 3>* selectedTriangle = nullptr;
        std::array<double, 3> selectedWeights{};
        double bestMinimumWeight = -std::numeric_limits<double>::infinity();
        for (const auto& triangle : kCalibrationTriangles) {
            std::array<double, 3> weights{};
            if (!WorldBarycentric(
                    worldX, worldY,
                    points[triangle[0]], points[triangle[1]], points[triangle[2]],
                    weights)) {
                continue;
            }
            const double minimumWeight = std::min({ weights[0], weights[1], weights[2] });
            if (minimumWeight >= -1.0e-6) {
                selectedTriangle = &triangle;
                selectedWeights = weights;
                break;
            }
            // Outside the calibrated hull, extrapolate using the nearest fan
            // triangle instead of snapping or reverting to unrelated bounds.
            if (minimumWeight > bestMinimumWeight) {
                bestMinimumWeight = minimumWeight;
                selectedTriangle = &triangle;
                selectedWeights = weights;
            }
        }
        if (!selectedTriangle) return false;

        mapU = 0.0f;
        mapV = 0.0f;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& point = points[(*selectedTriangle)[corner]];
            mapU += static_cast<float>(selectedWeights[corner] * point.mapU);
            mapV += static_cast<float>(selectedWeights[corner] * point.mapV);
        }
        if (rmsError) *rmsError = transform.rmsError;
        return std::isfinite(mapU) && std::isfinite(mapV);
    }

    bool IsMapCalibrationUsable(const MapCalibrationTransform& transform)
    {
        // Do not let a partial or contradictory calibration replace the known
        // moving legacy marker. Five points give us two residual checks beyond
        // the three parameters of each affine axis.
        if (transform.pointCount != kMapCalibrationPointCount ||
            !std::isfinite(transform.rmsError) || transform.rmsError > 0.06f) {
            return false;
        }

        const double horizontalResponse = std::hypot(transform.x[1], transform.x[2]);
        const double verticalResponse = std::hypot(transform.y[1], transform.y[2]);
        constexpr double kMinimumWorldToUvResponse = 1.0e-7;
        constexpr double kMaximumWorldToUvResponse = 1.0e-4;
        return horizontalResponse >= kMinimumWorldToUvResponse &&
            verticalResponse >= kMinimumWorldToUvResponse &&
            horizontalResponse <= kMaximumWorldToUvResponse &&
            verticalResponse <= kMaximumWorldToUvResponse;
    }

    bool GetMapCalibrationLandmarkUv(
        std::size_t cityIndex,
        float& mapU,
        float& mapV)
    {
        if (cityIndex >= kCityLandmarks.size()) return false;

        const auto landmark = kCityLandmarks[cityIndex];
        mapU = landmark.u;
        mapV = landmark.v;
        return true;
    }
}
