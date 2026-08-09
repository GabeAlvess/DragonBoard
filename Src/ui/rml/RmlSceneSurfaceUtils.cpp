#include "pch.h"

#include "ui/rml/RmlSceneSurfaceUtils.h"

#include <cstring>

namespace dragonboard::ui::rml
{
    RE::NiPointer<RE::NiSourceTexture> CreateIsolatedSourceTexture(
        const RE::NiSourceTexture& source)
    {
        auto* texture = RE::malloc<RE::NiSourceTexture>();
        if (!texture) return nullptr;

        std::memset(texture, 0, sizeof(*texture));
        std::memcpy(texture, std::addressof(source), sizeof(void*));
        std::memcpy(
            std::addressof(texture->formatPrefs),
            std::addressof(source.formatPrefs),
            sizeof(texture->formatPrefs));
        new (std::addressof(texture->name)) RE::BSFixedString(source.name);
        texture->unk28 = source.unk28;
        texture->unk2C = source.unk2C;
        texture->rendererTexture = source.rendererTexture;
        texture->flags = source.flags;
        texture->pad51 = source.pad51;
        texture->pad52 = source.pad52;
        texture->pad54 = source.pad54;
        return RE::NiPointer<RE::NiSourceTexture>(texture);
    }

    RmlSurfaceMaterialBinding IsolateRmlSurfaceMaterial(RE::BSGeometry& geometry)
    {
        if (auto* property = geometry.lightingShaderProp_cast()) {
            auto* sourceMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(
                property->GetBaseMaterial());
            if (!sourceMaterial || !sourceMaterial->diffuseTexture) return {};

            auto sourceTexture = CreateIsolatedSourceTexture(
                *sourceMaterial->diffuseTexture);
            auto* materialCopy = static_cast<RE::BSLightingShaderMaterialBase*>(
                sourceMaterial->Create());
            if (!sourceTexture || !materialCopy) return {};

            materialCopy->CopyMembers(sourceMaterial);
            materialCopy->diffuseTexture = sourceTexture;
            materialCopy->texCoordOffset[0] = { 0.0f, 0.0f };
            materialCopy->texCoordScale[0] = { 1.0f, 1.0f };
            materialCopy->texCoordOffset[1] = materialCopy->texCoordOffset[0];
            materialCopy->texCoordScale[1] = materialCopy->texCoordScale[0];
            materialCopy->hashKey = (std::numeric_limits<std::uint32_t>::max)();
            property->SetMaterial(materialCopy, true);

            auto* installedMaterial = static_cast<RE::BSLightingShaderMaterialBase*>(
                property->GetBaseMaterial());
            if (!installedMaterial) return {};
            installedMaterial->diffuseTexture = sourceTexture;
            installedMaterial->hashKey =
                (std::numeric_limits<std::uint32_t>::max)();
            ConfigureRmlSurfaceFullbright(*property);
            return {
                RE::NiPointer<RE::BSShaderProperty>(property),
                std::move(sourceTexture)
            };
        }

        auto& runtimeData = geometry.GetGeometryRuntimeData();
        auto* property = netimmerse_cast<RE::BSEffectShaderProperty*>(
            runtimeData.properties[RE::BSGeometry::States::kEffect].get());
        auto* sourceMaterial = property ? property->GetMaterial() : nullptr;
        if (!property || !sourceMaterial || !sourceMaterial->sourceTexture) return {};

        auto sourceTexture = CreateIsolatedSourceTexture(
            *sourceMaterial->sourceTexture);
        auto* materialCopy = static_cast<RE::BSEffectShaderMaterial*>(
            sourceMaterial->Create());
        if (!sourceTexture || !materialCopy) return {};

        materialCopy->CopyMembers(sourceMaterial);
        materialCopy->sourceTexture = sourceTexture;
        materialCopy->texCoordOffset[0] = { 0.0f, 0.0f };
        materialCopy->texCoordScale[0] = { 1.0f, 1.0f };
        materialCopy->texCoordOffset[1] = materialCopy->texCoordOffset[0];
        materialCopy->texCoordScale[1] = materialCopy->texCoordScale[0];
        materialCopy->hashKey = (std::numeric_limits<std::uint32_t>::max)();
        property->SetMaterial(materialCopy, true);

        auto* installedMaterial = property->GetMaterial();
        if (!installedMaterial) return {};
        installedMaterial->sourceTexture = sourceTexture;
        installedMaterial->hashKey =
            (std::numeric_limits<std::uint32_t>::max)();
        ConfigureRmlSurfaceFullbright(*property);
        return {
            RE::NiPointer<RE::BSShaderProperty>(property),
            std::move(sourceTexture)
        };
    }

    void RefreshRmlSurfaceShader(RE::BSShaderProperty& property)
    {
        if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(
                std::addressof(property))) {
            lighting->InvalidateTextures(0);
        }
        property.InvalidateMaterial();
        property.DoClearRenderPasses();
    }

    void ConfigureRmlSurfaceFullbright(RE::BSShaderProperty& property)
    {
        using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
        property.flags.set(Flag::kNoFade);
        property.flags.reset(Flag::kExternalEmittance);
        property.flags.reset(Flag::kReceiveShadows);
        property.flags.reset(Flag::kCastShadows);
        property.flags.reset(Flag::kSpecular);
        property.flags.reset(Flag::kEnvMap);
        property.flags.reset(Flag::kVertexLighting);
        property.flags.reset(Flag::kCharacterLighting);
        property.flags.reset(Flag::kSoftLighting);
        property.flags.reset(Flag::kRimLighting);
        property.flags.reset(Flag::kBackLighting);
        property.flags.reset(Flag::kEffectLighting);

        if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(
                std::addressof(property))) {
            property.flags.set(Flag::kOwnEmit);
            if (!lighting->emissiveColor) {
                lighting->emissiveColor = new RE::NiColor();
            }
            *lighting->emissiveColor = RE::NiColor(1.0f, 1.0f, 1.0f);
            lighting->emissiveMult = 1.0f;
            lighting->forcedDarkness = 0.0f;

            auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(
                lighting->GetBaseMaterial());
            if (material) {
                material->specularColor = RE::NiColor(0.0f, 0.0f, 0.0f);
                material->specularColorScale = 0.0f;
                material->specularPower = 0.0f;
                material->rimLightPower = 0.0f;
            }
        } else if (auto* effect = netimmerse_cast<RE::BSEffectShaderProperty*>(
                       std::addressof(property))) {
            auto* material = effect->GetMaterial();
            if (material) {
                material->baseColor = RE::NiColorA(1.0f, 1.0f, 1.0f, 1.0f);
                material->baseColorScale = 1.0f;
            }
        }
        RefreshRmlSurfaceShader(property);
    }
}