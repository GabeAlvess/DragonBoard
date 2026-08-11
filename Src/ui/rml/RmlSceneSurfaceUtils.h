#pragma once

#include <RE/Skyrim.h>

namespace dragonboard::ui::rml
{
    struct RmlSurfaceMaterialBinding
    {
        RE::NiPointer<RE::BSShaderProperty> shaderProperty;
        RE::NiPointer<RE::NiSourceTexture> sourceTexture;

        explicit operator bool() const noexcept
        {
            return shaderProperty && sourceTexture;
        }
    };

    [[nodiscard]] RE::NiPointer<RE::NiSourceTexture> CreateIsolatedSourceTexture(
        const RE::NiSourceTexture& source);
    [[nodiscard]] RmlSurfaceMaterialBinding IsolateRmlSurfaceMaterial(
        RE::BSGeometry& geometry,
        bool configureFullbright = true);
    void ConfigureRmlSurfaceFullbright(RE::BSShaderProperty& property);
    void ConfigureRmlSurfaceShadowReceiver(RE::BSShaderProperty& property);
    void RefreshRmlSurfaceShader(RE::BSShaderProperty& property);
}
