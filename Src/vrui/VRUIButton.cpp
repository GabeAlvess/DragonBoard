#include "pch.h"
#include "VRUIButton.h"
#include "ui/input/GripThumbScale.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <RE/B/BSLightingShaderProperty.h>
#include <RE/B/BSLightingShaderMaterial.h>
#include <RE/B/BSLightingShaderMaterialBase.h>
#include <RE/B/BSShaderTextureSet.h>
#include <RE/N/NiAlphaProperty.h>
#include <RE/B/BSEffectShaderProperty.h>
#include <RE/B/BSEffectShaderMaterial.h>
#include <RE/B/BSShaderTextureSet.h>
#include <RE/B/BSVisit.h>
#include <RE/B/BSGeometry.h>
#include <RE/N/NiNode.h>
#include "VRUISettings.h"
#include "integrations/higgs/PhysicalBoardController.h"
#include "VRMenuManager.h"
#include "VRUILayoutManager.h"
#include <format>
#include "higgsinterface001.h"
namespace vrui
{
    int VRUIButton::s_visualsLoadedThisFrame = 0;

    static RE::NiPoint3 rotateVector(const RE::NiMatrix3& mat, const RE::NiPoint3& vec) {
        return mat * vec;
    }
    
    static RE::NiPoint3 inverseRotateVector(const RE::NiMatrix3& mat, const RE::NiPoint3& vec) {
        // Transpose multiply for orthogonal matrix
        return mat.Transpose() * vec;
    }

    static RE::NiPoint3 worldToLocalPosition(const RE::NiNode* parentNode, const RE::NiPoint3& worldPos)
    {
        RE::NiPoint3 diff;
        diff.x = worldPos.x - parentNode->world.translate.x;
        diff.y = worldPos.y - parentNode->world.translate.y;
        diff.z = worldPos.z - parentNode->world.translate.z;

        const float parentScale = parentNode->world.scale > 0.0001f ? parentNode->world.scale : 1.0f;
        const RE::NiMatrix3 parentRotT = parentNode->world.rotate.Transpose();

        RE::NiPoint3 localPos;
        localPos.x = (parentRotT.entry[0][0] * diff.x + parentRotT.entry[0][1] * diff.y + parentRotT.entry[0][2] * diff.z) / parentScale;
        localPos.y = (parentRotT.entry[1][0] * diff.x + parentRotT.entry[1][1] * diff.y + parentRotT.entry[1][2] * diff.z) / parentScale;
        localPos.z = (parentRotT.entry[2][0] * diff.x + parentRotT.entry[2][1] * diff.y + parentRotT.entry[2][2] * diff.z) / parentScale;
        return localPos;
    }

    static RE::NiPoint3 localToWorldPosition(const RE::NiNode* parentNode, const RE::NiPoint3& localPos)
    {
        const float parentScale = parentNode->world.scale > 0.0001f ? parentNode->world.scale : 1.0f;
        return parentNode->world.translate + rotateVector(parentNode->world.rotate, localPos) * parentScale;
    }

    struct HiggsProximityVolume
    {
        RE::NiPoint3 center{};
        float radius{ 0.75f };
    };

    static HiggsProximityVolume resolveHiggsProximityVolume(
        const RE::NiNode* logicalNode,
        const RE::NiNode* primaryVisualNode)
    {
        const auto* visualNode = primaryVisualNode ? primaryVisualNode : logicalNode;

        HiggsProximityVolume volume;
        if (!visualNode) {
            return volume;
        }

        // The visual transform includes the item's saved offset, rotation and scale.
        // Its aggregate world bound therefore follows the geometry the player sees,
        // unlike the logical button origin used by the old fixed-radius test.
        volume.center = visualNode->world.translate;

        const auto& bound = visualNode->worldBound;
        const bool hasValidBound =
            std::isfinite(bound.center.x) &&
            std::isfinite(bound.center.y) &&
            std::isfinite(bound.center.z) &&
            std::isfinite(bound.radius) &&
            bound.radius > 0.001f;

        if (hasValidBound) {
            volume.center = bound.center;
            // Broken or unusually large NIF bounds must not make an item selectable
            // from across the board. Normalized pinned visuals remain below this cap.
            volume.radius = std::clamp(bound.radius, 0.25f, 2.0f);
        }

        return volume;
    }

    static std::string formatMatrixRows(const RE::NiMatrix3& mat)
    {
        return std::format(
            "[[{:.4f}, {:.4f}, {:.4f}], [{:.4f}, {:.4f}, {:.4f}], [{:.4f}, {:.4f}, {:.4f}]]",
            mat.entry[0][0], mat.entry[0][1], mat.entry[0][2],
            mat.entry[1][0], mat.entry[1][1], mat.entry[1][2],
            mat.entry[2][0], mat.entry[2][1], mat.entry[2][2]);
    }

    static float makePhaseSeed(const std::string& text)
    {
        std::uint32_t hash = 2166136261u;
        for (unsigned char ch : text) {
            hash ^= ch;
            hash *= 16777619u;
        }
        return static_cast<float>(hash % 10000u) * 0.001f;
    }

    static RE::NiMatrix3 orthonormalizeMatrix(RE::NiMatrix3 mat)
    {
        RE::NiPoint3 col0(mat.entry[0][0], mat.entry[1][0], mat.entry[2][0]);
        float mag0 = std::sqrt(col0.x * col0.x + col0.y * col0.y + col0.z * col0.z);
        if (mag0 > 0.0001f) {
            col0.x /= mag0;
            col0.y /= mag0;
            col0.z /= mag0;
        }

        RE::NiPoint3 col1(mat.entry[0][1], mat.entry[1][1], mat.entry[2][1]);
        float dot01 = col0.x * col1.x + col0.y * col1.y + col0.z * col1.z;
        col1.x -= dot01 * col0.x;
        col1.y -= dot01 * col0.y;
        col1.z -= dot01 * col0.z;
        float mag1 = std::sqrt(col1.x * col1.x + col1.y * col1.y + col1.z * col1.z);
        if (mag1 > 0.0001f) {
            col1.x /= mag1;
            col1.y /= mag1;
            col1.z /= mag1;
        }

        RE::NiPoint3 col2;
        col2.x = col0.y * col1.z - col0.z * col1.y;
        col2.y = col0.z * col1.x - col0.x * col1.z;
        col2.z = col0.x * col1.y - col0.y * col1.x;

        mat.entry[0][0] = col0.x; mat.entry[1][0] = col0.y; mat.entry[2][0] = col0.z;
        mat.entry[0][1] = col1.x; mat.entry[1][1] = col1.y; mat.entry[2][1] = col1.z;
        mat.entry[0][2] = col2.x; mat.entry[1][2] = col2.y; mat.entry[2][2] = col2.z;
        return mat;
    }

    static void extractItemEulerForSetXYZ(const RE::NiMatrix3& mat, float& x, float& y, float& z)
    {
        const float m02 = std::clamp(mat.entry[0][2], -1.0f, 1.0f);
        y = -std::asin(m02);

        const float cosY = std::cos(y);
        if (std::abs(cosY) > 1e-5f) {
            x = std::atan2(mat.entry[1][2], mat.entry[2][2]);
            z = std::atan2(mat.entry[0][1], mat.entry[0][0]);
        } else {
            // Gimbal-lock fallback: keep Z at zero and solve X from the remaining plane.
            x = std::atan2(-mat.entry[2][1], mat.entry[1][1]);
            z = 0.0f;
        }
    }

    VRUIButton::VRUIButton(const std::string& label, float width, float height)
        : VRUIWidget(label, width, height)
        , _label(label)
        , _buttonId(label)
        , _sublabel("")
        , _state(ButtonState::Normal)
        , _slotIndex(-1)
        , _wigglePhaseSeed(makePhaseSeed(label))
    {
        // Simple label buttons don't have heavy visuals to defer, but we respect the flag for structure
        initializeVisuals();
    }

    VRUIButton::VRUIButton(const std::string& label, const std::string& nifPath, const std::string& texturePath, float width, float height, bool deferInit)
        : VRUIWidget(label, width, height)
        , _label(label)
        , _buttonId(label)
        , _sublabel("")
        , _nifPath(nifPath)
        , _texturePath(texturePath)
        , _state(ButtonState::Normal)
        , _slotIndex(-1)
        , _isVisualsInitialized(!deferInit)
        , _wigglePhaseSeed(makePhaseSeed(label + nifPath))
    {
        if (!deferInit) {
            initializeVisuals();
        }
    }

    VRUIButton::VRUIButton(const std::string& label, const std::string& nifPath, const std::string& texturePath,
                           float width, float height,
                           float itemRotX, float itemRotY, float itemRotZ,
                           float itemXOffset, float itemYOffset, float itemZOffset,
                           float itemScaleMult, bool deferInit,
                           ItemUtils::ItemTransformSource transformSource)
        : VRUIWidget(label, width, height)
        , _label(label)
        , _buttonId(label)
        , _sublabel("")
        , _nifPath(nifPath)
        , _texturePath(texturePath)
        , _itemRotOverrideX(itemRotX)
        , _itemRotOverrideY(itemRotY)
        , _itemRotOverrideZ(itemRotZ)
        , _itemXOffset(itemXOffset)
        , _itemYOffset(itemYOffset)
        , _itemZOffset(itemZOffset)
        , _itemScaleMult(itemScaleMult)
        , _itemTransformSource(transformSource)
        , _state(ButtonState::Normal)
        , _slotIndex(-1)
        , _isVisualsInitialized(!deferInit)
        , _wigglePhaseSeed(makePhaseSeed(label + nifPath))
    {
        if (!deferInit) {
            initializeVisuals();
        }
    }

    VRUIButton::~VRUIButton()
    {
        dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton()
            .UpdatePinnedItemGrabPriority(this, false, false);
    }

    void VRUIButton::initializeVisuals()
    {
        if (!_node) return;

        _primaryVisualNode = nullptr;
        _animatedVisualRoot = nullptr;
        _primaryVisualReferenceScale = 1.0f;
        bool successfullyLoaded = false;
        bool loadedWorldItemVisual = false;
        auto& settings = VRUISettings::get();
        _animatedVisualRoot = RE::NiPointer<RE::NiNode>(RE::NiNode::Create());
        if (_animatedVisualRoot) {
            _animatedVisualRoot->name = "AnimatedVisualRoot";
            _node->AttachChild(_animatedVisualRoot.get());
        }
        std::string pathLowerForClassification = _nifPath;
        std::transform(pathLowerForClassification.begin(), pathLowerForClassification.end(), pathLowerForClassification.begin(),
            [](unsigned char c){ return std::tolower(c); });
        const bool isInternalUiMesh =
            pathLowerForClassification.find("dragonboardvr/iconplane") != std::string::npos ||
            pathLowerForClassification.find("dragonboardvr/slot") != std::string::npos ||
            pathLowerForClassification.find("dragonboardvr/unknow") != std::string::npos ||
            pathLowerForClassification.find("dragonboardvr/tablet") != std::string::npos;
        const bool likelyWorldItemPath =
            pathLowerForClassification.find("weapons") != std::string::npos ||
            pathLowerForClassification.find("shield") != std::string::npos ||
            pathLowerForClassification.find("clutter") != std::string::npos ||
            pathLowerForClassification.find("armor") != std::string::npos ||
            pathLowerForClassification.find("jewelry") != std::string::npos ||
            pathLowerForClassification.find("alchemy") != std::string::npos ||
            pathLowerForClassification.find("food") != std::string::npos ||
            pathLowerForClassification.find("clothes") != std::string::npos ||
            pathLowerForClassification.find("book") != std::string::npos ||
            pathLowerForClassification.find("scroll") != std::string::npos ||
            pathLowerForClassification.find("key") != std::string::npos ||
            pathLowerForClassification.find("misc") != std::string::npos ||
            pathLowerForClassification.find("ingredients") != std::string::npos ||
            pathLowerForClassification.find("ingredient") != std::string::npos ||
            pathLowerForClassification.find("skooma") != std::string::npos ||
            pathLowerForClassification.find("flora") != std::string::npos ||
            pathLowerForClassification.find("plants") != std::string::npos ||
            pathLowerForClassification.find("flowers") != std::string::npos ||
            pathLowerForClassification.find("nature") != std::string::npos ||
            pathLowerForClassification.find("landscape") != std::string::npos ||
            pathLowerForClassification.find("vegetable") != std::string::npos ||
            pathLowerForClassification.find("fruit") != std::string::npos ||
            pathLowerForClassification.find("light") != std::string::npos ||
            pathLowerForClassification.find("torch") != std::string::npos ||
            pathLowerForClassification.find("item") != std::string::npos ||
            pathLowerForClassification.find("artifact") != std::string::npos ||
            pathLowerForClassification.find("magic") != std::string::npos ||
            pathLowerForClassification.find("spell") != std::string::npos ||
            pathLowerForClassification.find("vfx") != std::string::npos ||
            pathLowerForClassification.find("compass") != std::string::npos;

        const bool applyUIShaderTweaks = isInternalUiMesh && !likelyWorldItemPath;
        auto attachOverlayMesh = [&](const std::string& nifPath) {
            if (nifPath.empty() || !_node) {
                return;
            }

            std::string overlayLower = nifPath;
            std::transform(overlayLower.begin(), overlayLower.end(), overlayLower.begin(),
                [](unsigned char c){ return std::tolower(c); });

            const bool overlayUiMesh =
                overlayLower.find("dragonboardvr/iconplane") != std::string::npos ||
                overlayLower.find("dragonboardvr/slot") != std::string::npos ||
                overlayLower.find("dragonboardvr/unknow") != std::string::npos ||
                overlayLower.find("dragonboardvr/tablet") != std::string::npos;
            const bool overlayWorldItem =
                overlayLower.find("weapons") != std::string::npos ||
                overlayLower.find("shield") != std::string::npos ||
                overlayLower.find("clutter") != std::string::npos ||
                overlayLower.find("armor") != std::string::npos ||
                overlayLower.find("jewelry") != std::string::npos ||
                overlayLower.find("alchemy") != std::string::npos ||
                overlayLower.find("food") != std::string::npos ||
                overlayLower.find("clothes") != std::string::npos ||
                overlayLower.find("book") != std::string::npos ||
                overlayLower.find("scroll") != std::string::npos ||
                overlayLower.find("key") != std::string::npos ||
                overlayLower.find("misc") != std::string::npos ||
                overlayLower.find("ingredients") != std::string::npos ||
                overlayLower.find("ingredient") != std::string::npos ||
                overlayLower.find("magic") != std::string::npos ||
                overlayLower.find("spell") != std::string::npos ||
                overlayLower.find("vfx") != std::string::npos;

            auto overlay = loadModelFromNif(nifPath, overlayUiMesh && !overlayWorldItem);
            if (!overlay) {
                logger::warn("DragonBoardVR: Button '{}' failed to load overlay NIF '{}'", _buttonId, nifPath);
                return;
            }

            VRUIWidget::sanitizeModel(overlay.get(), overlayUiMesh && !overlayWorldItem);
            if (overlayUiMesh && !overlayWorldItem) {
                VRUIWidget::normalizePhysicalMaterialLighting(overlay.get(), false);
            }
            VRUIModelHelper::normalizeAndCenterModel(overlay.get());

            float overlayTargetSize = ((_width < _height) ? _width : _height) * 0.8f;
            float overlayUserMultiplier = settings.buttonMeshScale;
            if (overlayUserMultiplier < 0.1f) {
                overlayUserMultiplier = 1.0f;
            }
            overlay->local.scale *= (overlayTargetSize * overlayUserMultiplier * _overlayScaleMult);

            constexpr float kMeshRotX = 90.0f  * kDegToRad;
            constexpr float kMeshRotY =  0.0f  * kDegToRad;
            constexpr float kMeshRotZ = 180.0f * kDegToRad;
            overlay->local.rotate.SetEulerAnglesXYZ(kMeshRotX, kMeshRotY, kMeshRotZ);

            float scaleX = _width / 2.0f;
            float scaleZ = _height / 2.0f;
            for (int i = 0; i < 3; ++i) {
                overlay->local.rotate.entry[i][0] *= scaleX;
                overlay->local.rotate.entry[i][2] *= scaleZ;
            }

            overlay->local.translate.x += _overlayOffsetX;
            overlay->local.translate.y += _overlayOffsetY;
            overlay->local.translate.z += _overlayOffsetZ;

            RE::NiUpdateData updateData;
            overlay->Update(updateData);
            getVisualParentNode()->AttachChild(overlay.get());
        };

        if (!_nifPath.empty()) {
            auto loaded = loadModelFromNif(_nifPath, applyUIShaderTweaks);
            if (loaded && _node) {
                // 1. Strip unsafe runtime data while preserving hidden child
                // geometry on external world-item NIFs.
                VRUIWidget::sanitizeModel(loaded.get(), applyUIShaderTweaks);
                if (isInternalUiMesh && !likelyWorldItemPath) {
                    VRUIWidget::normalizePhysicalMaterialLighting(loaded.get(), false);
                }

                std::string pathLower = _nifPath;
                std::transform(pathLower.begin(), pathLower.end(), pathLower.begin(), 
                               [](unsigned char c){ return std::tolower(c); });

                const bool hasWeapons = pathLower.find("weapons") != std::string::npos;
                const bool hasShield = pathLower.find("shield") != std::string::npos;
                const bool hasClutter = pathLower.find("clutter") != std::string::npos;
                const bool hasArmor = pathLower.find("armor") != std::string::npos;
                const bool hasJewelry = pathLower.find("jewelry") != std::string::npos;
                const bool hasAlchemy = pathLower.find("alchemy") != std::string::npos;
                const bool hasFood = pathLower.find("food") != std::string::npos;
                const bool hasClothes = pathLower.find("clothes") != std::string::npos;
                const bool hasBook = pathLower.find("book") != std::string::npos;
                const bool hasScroll = pathLower.find("scroll") != std::string::npos;
                const bool hasKey = pathLower.find("key") != std::string::npos;
                const bool hasMisc = pathLower.find("misc") != std::string::npos;
                const bool hasIngredients = pathLower.find("ingredients") != std::string::npos;
                const bool hasIngredient = pathLower.find("ingredient") != std::string::npos;
                const bool hasSkooma = pathLower.find("skooma") != std::string::npos;
                const bool hasFlora = pathLower.find("flora") != std::string::npos;
                const bool hasPlants = pathLower.find("plants") != std::string::npos;
                const bool hasFlowers = pathLower.find("flowers") != std::string::npos;
                const bool hasNature = pathLower.find("nature") != std::string::npos;
                const bool hasLandscape = pathLower.find("landscape") != std::string::npos;
                const bool hasVegetable = pathLower.find("vegetable") != std::string::npos;
                const bool hasFruit = pathLower.find("fruit") != std::string::npos;
                const bool hasLight = pathLower.find("light") != std::string::npos;
                const bool hasTorch = pathLower.find("torch") != std::string::npos;
                const bool hasItem = pathLower.find("item") != std::string::npos;
                const bool hasArtifact = pathLower.find("artifact") != std::string::npos;
                const bool hasMagic = pathLower.find("magic") != std::string::npos;
                const bool hasSpell = pathLower.find("spell") != std::string::npos;
                const bool hasVfx = pathLower.find("vfx") != std::string::npos;
                const bool hasCompass = pathLower.find("compass") != std::string::npos;
                const bool hasPotion = pathLower.find("potion") != std::string::npos;

                // Only DragonBoard's own UI meshes are safe to treat as flat
                // planes. Spell inventory-art NIFs frequently live under
                // paths such as DLC folders that contain none of the keywords
                // above. Treating those external 3D meshes as UI planes puts
                // non-uniform scale into the rotation matrix and makes pinned
                // magic appear stretched after FixedWidgetPresenter rebuilds
                // it on the dashboard.
                const bool isWorldItem = !isInternalUiMesh ||
                                         hasWeapons || hasShield || hasClutter || hasArmor || hasJewelry ||
                                         hasAlchemy || hasFood || hasClothes || hasBook || hasScroll ||
                                         hasKey || hasMisc || hasIngredients || hasIngredient || hasSkooma ||
                                         hasFlora || hasPlants || hasFlowers || hasNature || hasLandscape ||
                                         hasVegetable || hasFruit || hasLight || hasTorch || hasItem ||
                                         hasArtifact || hasMagic || hasSpell || hasVfx || hasCompass;

                RE::NiMatrix3 presentationRotation{};
                bool usedInventoryMarker = false;
                float inventoryMarkerZoom = 1.0f;
                if (isWorldItem) {
                    constexpr float kItemRotDefault = -90.0f;
                    const float rotX = std::isnan(_itemRotOverrideX) ? kItemRotDefault : _itemRotOverrideX;
                    const float rotY = std::isnan(_itemRotOverrideY) ? kItemRotDefault : _itemRotOverrideY;
                    const float rotZ = std::isnan(_itemRotOverrideZ) ? kItemRotDefault : _itemRotOverrideZ;
                    presentationRotation.SetEulerAnglesXYZ(
                        rotX * kDegToRad, rotY * kDegToRad, rotZ * kDegToRad);

                    // A player's item/category correction always wins. The NIF
                    // marker is only an automatic fallback for untouched items.
                    if (settings.useNifInventoryMarkerRotation &&
                        !ItemUtils::isExplicitOverride(_itemTransformSource)) {
                        usedInventoryMarker = VRUIModelHelper::getInventoryMarkerTransform(
                            loaded.get(), presentationRotation, inventoryMarkerZoom);
                    }
                }

                // Keep automatic centering on its own node. The editable parent
                // can then change position/rotation/scale without destroying the
                // pivot correction calculated from visible geometry.
                RE::NiPointer<RE::NiNode> visualContent = loaded;
                if (isWorldItem && settings.normalizeItemVisuals) {
                    auto normalizedVisual = RE::NiPointer<RE::NiNode>(RE::NiNode::Create());
                    if (normalizedVisual) {
                        normalizedVisual->name = "AutoNormalizedVisual";
                        normalizedVisual->AttachChild(loaded.get());
                        VRUIModelHelper::normalizeAndCenterWorldModel(
                            normalizedVisual.get(), presentationRotation);
                        visualContent = normalizedVisual;
                    } else {
                        VRUIModelHelper::normalizeAndCenterModel(loaded.get());
                    }
                } else {
                    VRUIModelHelper::normalizeAndCenterModel(loaded.get());
                }

                // 2. Final uniform slot scaling.
                float buttonTargetSize = ((_width < _height) ? _width : _height) * 0.8f;
                float userMultiplier = settings.buttonMeshScale;

                if (isWorldItem) {
                    loadedWorldItemVisual = true;
                    // Normalization establishes a consistent visible base size.
                    // The Settings category scale is a player-authored total
                    // multiplier applied on top of that normalized result.
                    float specificMult = settings.itemMiscScale;
                    if (hasWeapons || hasShield) {
                        specificMult = settings.itemWeaponScale;
                    } else if (hasArmor || hasClothes || hasJewelry) {
                        specificMult = settings.itemArmorScale;
                    } else if (hasAlchemy || hasPotion || hasSkooma) {
                        specificMult = settings.itemPotionScale;
                    } else if (hasFood || hasIngredient || hasIngredients) {
                        specificMult = settings.itemFoodScale;
                    }
                    userMultiplier = settings.itemMeshScale * specificMult;
                }

                // If scale is the old default (0.02 or 0.05), treat as 1.0 relative
                if (userMultiplier < 0.1f) {
                    userMultiplier = 1.0f; 
                }

                // Keep the automatic pivot correction on the loaded NIF itself and
                // apply all editable transforms to a separate parent.  Applying
                // position/rotation directly to `loaded` used to overwrite the
                // translation produced by normalizeAndCenterModel(), so models with
                // off-centre authoring pivots jumped to different places in a slot.
                auto visualTransform = RE::NiPointer<RE::NiNode>(RE::NiNode::Create());
                if (!visualTransform) {
                    logger::warn("DragonBoardVR: Button '{}' could not create primary visual transform", _label);
                    return;
                }
                visualTransform->name = "PrimaryVisualTransform";
                visualTransform->AttachChild(visualContent.get());

                // The child is normalized to one model-space unit.  The parent
                // supplies the slot size and remains the node edited/grabbed by the
                // user, preserving the normalized child's scale and centre offset.
                visualTransform->local.scale = buttonTargetSize * userMultiplier;
                const float referenceVisualScale = visualTransform->local.scale;

                // 3. Rotation logic
                if (!isWorldItem) {
                    // UI Planes: fixed rotation (90° X, 0° Y, 180° Z) — these are the only values that work
                    constexpr float kMeshRotX = 90.0f  * kDegToRad;
                    constexpr float kMeshRotY =  0.0f  * kDegToRad;
                    constexpr float kMeshRotZ = 180.0f * kDegToRad;
                    visualTransform->local.rotate.SetEulerAnglesXYZ(kMeshRotX, kMeshRotY, kMeshRotZ);

                    float scaleX = _width / 2.0f;
                    float scaleZ = _height / 2.0f;
                    for (int i = 0; i < 3; ++i) {
                        visualTransform->local.rotate.entry[i][0] *= scaleX;
                        visualTransform->local.rotate.entry[i][2] *= scaleZ;
                    }
                } else {
                    visualTransform->local.rotate = presentationRotation;

                    // With automatic fitting, legacy per-type transform
                    // compensations are ignored. Explicit INI item/category
                    // corrections remain final and multiply the Settings scale.
                    const bool applyStoredCorrection =
                        ItemUtils::isExplicitOverride(_itemTransformSource) ||
                        !settings.normalizeItemVisuals;
                    if (applyStoredCorrection && _itemScaleMult != 1.0f) {
                        visualTransform->local.scale *= _itemScaleMult;
                    }
                    if (applyStoredCorrection) {
                        visualTransform->local.translate.x += _itemXOffset;
                        visualTransform->local.translate.y += _itemYOffset;
                        visualTransform->local.translate.z += _itemZOffset;
                    }
                }

                visualTransform->local.translate.x += _visualOffsetX;
                visualTransform->local.translate.y += _visualOffsetY;
                visualTransform->local.translate.z += _visualOffsetZ;
                
                getVisualParentNode()->AttachChild(visualTransform.get());
                _primaryVisualNode = visualTransform;
                _primaryVisualReferenceScale = referenceVisualScale;

                if (_primaryVisualIdentityOnLoad) {
                    _primaryVisualNode->local.translate = { 0.0f, 0.0f, 0.0f };
                    _primaryVisualNode->local.rotate = RE::NiMatrix3{};
                    _primaryVisualNode->local.scale = 1.0f;
                }

                RE::NiUpdateData finalUpdate;
                visualTransform->Update(finalUpdate);

                successfullyLoaded = true;
                logger::trace(
                    "DragonBoardVR: Button '{}' loaded NIF world={} normalized={} "
                    "source={} marker={} markerZoom={:.3f} finalScale={:.3f}",
                    _label, isWorldItem, isWorldItem && settings.normalizeItemVisuals,
                    static_cast<int>(_itemTransformSource), usedInventoryMarker,
                    inventoryMarkerZoom, visualTransform->local.scale);
            }
        }

        if (!_nifPath.empty() && !successfullyLoaded) {
            // Placeholder logic (Marker_Error/IconPlane)
            RE::NiPointer<RE::NiNode> meshNode;
            RE::BSModelDB::DBTraits::ArgsType args{};

            static const char* meshPaths[] = {
                "DragonBoardVR\\IconPlane.nif", 
                "markers\\movemarker01.nif"
            };

            for (auto* path : meshPaths) {
                auto result = RE::BSModelDB::Demand(path, meshNode, args);
                if (result == RE::BSResource::ErrorCode::kNone && meshNode) {
                    auto* cloned = meshNode->Clone();
                    if (cloned && _node) {
                        VRUIWidget::sanitizeModel(cloned, true);
                        VRUIWidget::normalizePhysicalMaterialLighting(cloned, false);
                        VRUIModelHelper::normalizeAndCenterModel(cloned);

                        float buttonTargetSize = (_width < _height) ? _width : _height;
                        buttonTargetSize *= 0.8f;
                        
                        float userMultiplier = settings.buttonMeshScale;
                        if (userMultiplier < 0.1f) userMultiplier = 1.0f;

                        // Unified scale: Button Fitting * User Multiplier
                        cloned->local.scale *= (buttonTargetSize * userMultiplier);
                        _primaryVisualReferenceScale = cloned->local.scale;

                        // Force rotation for placeholders (flat planes): 90° X, 0° Y, 180° Z
                        constexpr float kMeshRotX = 90.0f  * kDegToRad;
                        constexpr float kMeshRotY =  0.0f  * kDegToRad;
                        constexpr float kMeshRotZ = 180.0f * kDegToRad;
                        cloned->local.rotate.SetEulerAnglesXYZ(kMeshRotX, kMeshRotY, kMeshRotZ);

                        // Apply non-uniform scaling relative to standard button dimensions
                        float scaleX = _width / 2.0f;
                        float scaleZ = _height / 2.0f;
                        for (int i = 0; i < 3; ++i) {
                            cloned->local.rotate.entry[i][0] *= scaleX;
                            cloned->local.rotate.entry[i][2] *= scaleZ;
                        }

                        cloned->local.translate.x += _visualOffsetX;
                        cloned->local.translate.y += _visualOffsetY;
                        cloned->local.translate.z += _visualOffsetZ;

                        RE::NiUpdateData updateData;
                        cloned->Update(updateData);
                        getVisualParentNode()->AttachChild(cloned);
                        _primaryVisualNode = RE::NiPointer<RE::NiNode>(cloned ? cloned->AsNode() : nullptr);

                        successfullyLoaded = true;
                        logger::trace("DragonBoardVR: Button '{}' using placeholder '{}' finalScale={:.3f}", 
                            _label, path, cloned->local.scale);
                        break;
                    }
                }
            }
        }

        if (!successfullyLoaded) {
            if (_nifPath.empty()) {
                logger::trace("DragonBoardVR: Button '{}' is text-only (no visual mesh requested)", _label);
            } else {
                logger::warn("DragonBoardVR: Button '{}' has no visual mesh", _label);
            }
        }

        attachOverlayMesh(_overlayNifPath);

        // Apply custom texture only to UI-style meshes.
        // World-item NIFs (shields, modded gear, etc.) often use complex shader setups;
        // forcing vertex-alpha/texture overrides on them can cause lighting flicker.
        if (!_texturePath.empty() && _node && isInternalUiMesh && !loadedWorldItemVisual && !likelyWorldItemPath) {
            auto* textureSet = RE::BSShaderTextureSet::Create();
            if (textureSet) {
                textureSet->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, _texturePath.c_str());

                bool textureApplied = false;

                // Find geometries to apply the texture and fix transparency
                RE::BSVisit::TraverseScenegraphGeometries(_node.get(), [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                    if (geom) {
                        // 1. Fix Transparency (NiAlphaProperty)
                        auto& runtimeData = geom->GetGeometryRuntimeData();
                        auto* alphaProp = netimmerse_cast<RE::NiAlphaProperty*>(runtimeData.properties[RE::BSGeometry::States::kProperty].get());
                        if (alphaProp) {
                            alphaProp->SetAlphaBlending(true);
                            alphaProp->SetAlphaTesting(false);
                            alphaProp->SetSrcBlendMode(RE::NiAlphaProperty::AlphaFunction::kSrcAlpha);
                            alphaProp->SetDestBlendMode(RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha);
                        }

                        // 2. Apply Texture and Fix Shader Flags
                        auto* lightingProp = geom->lightingShaderProp_cast();
                        if (lightingProp) {
                            // SLSF1_Vertex_Alpha (3rd bit in EShaderPropertyFlag8) is required for transparency in many cases
                            lightingProp->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kVertexAlpha, true);
                            
                            if (lightingProp->GetBaseMaterial()) {
                                auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(lightingProp->GetBaseMaterial());
                                if (material) {
                                    RE::NiPointer<RE::BSTextureSet> texPtr(textureSet);
                                    material->SetTextureSet(texPtr);
                                    
                                    // Apply texture
                                    textureApplied = true;
                                }
                            }
                        }

                        // Support for BSEffectShaderProperty (common for UI/Transparent elements)
                        auto* effect = runtimeData.properties[RE::BSGeometry::States::kEffect].get();
                        if (effect) {
                            auto* effectProp = netimmerse_cast<RE::BSEffectShaderProperty*>(effect);
                            if (effectProp && effectProp->GetMaterial()) {
                                effectProp->GetMaterial()->sourceTexturePath = _texturePath;
                                textureApplied = true;
                            }
                        }
                    }
                    return RE::BSVisit::BSVisitControl::kContinue;
                });
                
                if (textureApplied) {
                    logger::trace("DragonBoardVR: Button '{}' applied custom texture '{}'", _label, _texturePath);
                } else {
                    logger::warn("DragonBoardVR: Button '{}' failed to apply texture '{}' (No BSLightingShaderProperty found on your custom NIF!)", _label, _texturePath);
                    auto fallbackModel = loadModelFromNif("DragonBoardVR\\IconPlane.nif");
                    if (fallbackModel) {
                        for (std::uint16_t i = 0; i < _node->GetChildren().size(); ++i) {
                            if (_node->GetChildren()[i]) {
                                _node->DetachChildAt(i);
                            }
                        }
                        sanitizeModel(fallbackModel.get(), true);
                        normalizePhysicalMaterialLighting(fallbackModel.get(), false);
                        VRUIModelHelper::normalizeAndCenterModel(fallbackModel.get());
                        
                        float buttonTargetSize = (_width < _height) ? _width : _height;
                        buttonTargetSize *= 0.8f;
                        
                        float userMultiplier = settings.buttonMeshScale;
                        if (userMultiplier < 0.1f) userMultiplier = 1.0f;
                        fallbackModel->local.scale = buttonTargetSize * userMultiplier;
                        
                        constexpr float kMeshRotX = 90.0f  * kDegToRad;
                        constexpr float kMeshRotY =  0.0f  * kDegToRad;
                        constexpr float kMeshRotZ = 180.0f * kDegToRad;
                        fallbackModel->local.rotate.SetEulerAnglesXYZ(kMeshRotX, kMeshRotY, kMeshRotZ);
                        
                        float scaleX = _width / 2.0f;
                        float scaleZ = _height / 2.0f;
                        for (int i = 0; i < 3; ++i) {
                            fallbackModel->local.rotate.entry[i][0] *= scaleX;
                            fallbackModel->local.rotate.entry[i][2] *= scaleZ;
                        }
                        getVisualParentNode()->AttachChild(fallbackModel.get());
                    }
                }
            } else {
                logger::error("DragonBoardVR: Button '{}' failed to create BSShaderTextureSet '{}'", _label, _texturePath);
            }
        }

        // Log the resulting node hierarchy for debugging
        logNodeHierarchy("Button '" + _label + "' after initializeVisuals");

    }

    RE::NiNode* VRUIButton::getVisualParentNode() const
    {
        if (_animatedVisualRoot) {
            return _animatedVisualRoot.get();
        }
        return _node.get();
    }

    RE::NiNode* VRUIButton::getVisualAttachmentNode() const
    {
        return getVisualParentNode();
    }

    void VRUIButton::setPrimaryVisualTransform(const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scaleMult)
    {
        if (!_primaryVisualNode) {
            return;
        }

        _primaryVisualNode->local.translate = {
            pos.x + _visualOffsetX,
            pos.y + _visualOffsetY,
            pos.z + _visualOffsetZ
        };
        _primaryVisualNode->local.rotate = rot;
        _primaryVisualNode->local.scale = _primaryVisualReferenceScale * scaleMult;

        RE::NiUpdateData updateData;
        updateData.flags = RE::NiUpdateData::Flag::kDirty;
        // The preview contains a single item now. Updating the widget root once
        // propagates the edited child transform without repeatedly rebuilding
        // bounds for the whole NIF hierarchy on every slider tick.
        if (_node) {
            _node->Update(updateData);
        } else {
            _primaryVisualNode->Update(updateData);
        }
    }

    void VRUIButton::setPrimaryVisualIdentityOnLoad(bool enabled)
    {
        _primaryVisualIdentityOnLoad = enabled;
        if (!enabled || !_primaryVisualNode) return;

        _primaryVisualNode->local.translate = { 0.0f, 0.0f, 0.0f };
        _primaryVisualNode->local.rotate = RE::NiMatrix3{};
        _primaryVisualNode->local.scale = 1.0f;
        RE::NiUpdateData updateData;
        updateData.flags = RE::NiUpdateData::Flag::kDirty;
        if (_node) _node->Update(updateData);
    }

    bool VRUIButton::getPrimaryVisualTransform(
        RE::NiPoint3& pos,
        RE::NiMatrix3& rot,
        float& scaleMult) const
    {
        if (!_primaryVisualNode) {
            return false;
        }

        pos = {
            _primaryVisualNode->local.translate.x - _visualOffsetX,
            _primaryVisualNode->local.translate.y - _visualOffsetY,
            _primaryVisualNode->local.translate.z - _visualOffsetZ
        };
        rot = _primaryVisualNode->local.rotate;
        scaleMult = _primaryVisualReferenceScale > 0.0001f ?
            _primaryVisualNode->local.scale / _primaryVisualReferenceScale :
            _itemScaleMult;
        return true;
    }

    void VRUIButton::setWorldLockedToHeadSpace(bool enabled, const RE::NiPoint3& worldPos,
                                               const RE::NiMatrix3& worldRot, float worldScale,
                                               const RE::NiPoint3&)
    {
        _worldLockedToHeadSpace = enabled;
        if (enabled) {
            _lockedWorldPos = worldPos;
            _lockedWorldRot = worldRot;
            _lockedWorldScale = worldScale;
            if (auto* headNode = VRMenuManager::get().getHeadNode()) {
                _headAnchorLocalPos = worldToLocalPosition(headNode, worldPos);
                _headAnchorLocalRot = headNode->world.rotate.Transpose() * worldRot;
                _hasHeadAnchorLocalTransform = true;
            } else {
                _hasHeadAnchorLocalTransform = false;
            }
        } else {
            _hasHeadAnchorLocalTransform = false;
        }
    }

    void VRUIButton::updateHeadLockedWorldTransform()
    {
        if (!_worldLockedToHeadSpace || _isGrabbed || !_node || !_node->parent) {
            return;
        }

        auto* parentNode = _node->parent->AsNode();
        if (!parentNode) {
            return;
        }

        auto* headNode = VRMenuManager::get().getHeadNode();
        if (!headNode || !_hasHeadAnchorLocalTransform) {
            return;
        }

        _lockedWorldPos = localToWorldPosition(headNode, _headAnchorLocalPos);
        _lockedWorldRot = headNode->world.rotate * _headAnchorLocalRot;

        const RE::NiPoint3 localPos = worldToLocalPosition(parentNode, _lockedWorldPos);
        const RE::NiMatrix3 parentRotT = parentNode->world.rotate.Transpose();

        _node->local.translate = localPos;
        _node->local.rotate = parentRotT * _lockedWorldRot;
        _node->local.scale = _currentScale;

        RE::NiUpdateData updateData;
        updateData.flags = RE::NiUpdateData::Flag::kDirty;
        _node->Update(updateData);
        _node->UpdateWorldBound();
    }

    void VRUIButton::updateAmbientWiggle(float deltaTime)
    {
        if (!_animatedVisualRoot) {
            return;
        }

        auto& settings = VRUISettings::get();
        const bool suppressWiggle = !settings.enableWorldPinWiggle || !_ambientWiggleEnabled || _isGrabbed ||
            _state == ButtonState::Pressed || _isLaserHovered;

        if (suppressWiggle) {
            _animatedVisualRoot->local.translate = { 0.0f, 0.0f, 0.0f };
            _animatedVisualRoot->local.rotate.SetEulerAnglesXYZ(0.0f, 0.0f, 0.0f);
            return;
        }

        _wiggleTime += deltaTime;
        const float speed = std::max(0.01f, settings.wiggleSpeed);
        const float t = _wiggleTime * speed + _wigglePhaseSeed;
        const float posBob = std::sin(t * 1.25f) * settings.wigglePosAmplitude +
            std::sin(t * 2.1f + 0.8f) * settings.wigglePosAmplitude * 0.35f;
        const float sideDrift = std::sin(t * 0.9f + 1.7f) * settings.wiggleSideAmplitude +
            std::sin(t * 1.7f + 0.25f) * settings.wiggleSideAmplitude * 0.35f;
        const float rotX = std::sin(t * 1.05f + 0.4f) * settings.wiggleRotAmplitude * 0.6f;
        const float rotZ = std::sin(t * 1.45f + 1.1f) * settings.wiggleRotAmplitude;

        _animatedVisualRoot->local.translate = { sideDrift, 0.0f, posBob };
        _animatedVisualRoot->local.rotate.SetEulerAnglesXYZ(rotX * kDegToRad, 0.0f, rotZ * kDegToRad);
    }


    void VRUIButton::setState(ButtonState newState)
    {
        if (_state == newState) return;

        auto oldState = _state;
        _state = newState;

        // Set target scale for smooth interpolation (actual scale applied in update())
        switch (newState) {
        case ButtonState::Normal:
            _targetScale = _baseScale;
            break;
        case ButtonState::Hovered:
            _targetScale = _baseScale * 1.1f;
            break;
        case ButtonState::Pressed:
            _targetScale = _baseScale * 0.9f;
            break;
        }
    }

    void VRUIButton::setLocalScale(float scale)
    {
        VRUIWidget::setLocalScale(scale);
        
        // Synchronize animation variables so we don't snap back to 1.0 or whatever the previous scale was
        _baseScale = scale;
        _currentScale = scale;
        _targetScale = (_state == ButtonState::Hovered) ? scale * 1.1f : 
                       ((_state == ButtonState::Pressed) ? scale * 0.9f : scale);
    }

    void VRUIButton::update(float deltaTime)
    {
        if (!_isVisualsInitialized) {
            if (_deferInitTimer > 0.0f) {
                _deferInitTimer -= deltaTime;
            } else if (s_visualsLoadedThisFrame < 1) { // Staggered loading budget: reduced to 1 per frame to eliminate VRIK frame drops
                initializeVisuals();
                _isVisualsInitialized = true;
                s_visualsLoadedThisFrame++;
            }
            // Skip the rest of the update logic until initialized
            return;
        }

        // Call base to advance _animProgress and update children
        VRUIWidget::update(deltaTime);

        // If grabbed, force the target and current scale to be the base scale so hover effect is suppressed.
        // Thumb scaling drives the grabbed node scale directly.
        if (_isGrabbed && !_isThumbScaling) {
            _targetScale = _baseScale;
            _currentScale = _baseScale;
        }

        // Smooth scale interpolation (prevents hitbox flicker from instant scale jumps)
        if (_node && fabsf(_currentScale - _targetScale) > 0.001f) {
            float lerpSpeed = 10.0f; // Higher = faster response
            float blend = deltaTime * lerpSpeed;
            if (blend > 1.0f) blend = 1.0f;
            _currentScale += (_targetScale - _currentScale) * blend;
            
            // Only push the hover scale once the pop-in entrance animation has completely finished
            if (_animProgress >= 1.0f) {
                _node->local.scale = _currentScale;
                RE::NiUpdateData updateData;
                _node->Update(updateData);
            }
        } else if (_node && _animProgress >= 1.0f && _node->local.scale != _currentScale) {
            // Failsafe: push final scale once animation finishes just in case they drifted
            _node->local.scale = _currentScale;
            RE::NiUpdateData updateData;
            _node->Update(updateData);
        }

        updateHeadLockedWorldTransform();
        updateAmbientWiggle(deltaTime);

        auto& settings = VRUISettings::get();
        const bool boardPositionAdjustment =
            VRMenuManager::get().isPositionAdjustmentActive();
        const bool pinMutationLocked = _isDashboardPinned && settings.lockPins;
        if ((pinMutationLocked || boardPositionAdjustment) && _isGrabbed) {
            if (_node) {
                _node->local.translate = _grabInitialLocalButtonPos;
                _node->local.rotate = _grabInitialLocalButtonRot;
                _node->local.scale = _grabInitialLocalButtonScale;
            }
            if (_persistItemRotationOnGrab && _primaryVisualNode) {
                _primaryVisualNode->local.translate = _grabInitialEditableLocalPos;
                _primaryVisualNode->local.rotate = _grabInitialEditableLocalRot;
                _primaryVisualNode->local.scale = _grabInitialEditableLocalScale;
            }
            if (_node) {
                RE::NiUpdateData updateData;
                updateData.flags = RE::NiUpdateData::Flag::kDirty;
                _node->Update(updateData);
                _node->UpdateWorldBound();
            }
            _isGrabbed = false;
            _isThumbScaling = false;
            _grabTimer = 0.0f;
            VRMenuManager::get().clearGrabbedWidget(this);
            logger::trace(
                "DragonBoardVR: active widget grab cancelled for '{}' (pinLocked={}, "
                "boardPositionAdjustment={}).",
                _label,
                pinMutationLocked,
                boardPositionAdjustment);
        }
        if (settings.bEnableButtonEditMode && !pinMutationLocked &&
            !boardPositionAdjustment && _node) {
            if (_isGrabbed) {
                if (!VRMenuManager::get().isDominantGripButtonDown()) {
                    releaseGrab();
                } else {
                    auto* hand = VRMenuManager::get().getDominantHandNode();
                    if (hand && _node && _node->parent) {
                        RE::NiNode* editableNode = (_persistItemRotationOnGrab && _primaryVisualNode) ? _primaryVisualNode.get() : _node.get();
                        RE::NiNode* editableParent = (editableNode && editableNode->parent) ? editableNode->parent->AsNode() : nullptr;
                        if (!editableNode || !editableParent) {
                            return;
                        }

                        constexpr float kGrabSmoothSpeed = 14.0f;
                        float grabBlend = 1.0f - std::exp(-kGrabSmoothSpeed * deltaTime);
                        grabBlend = std::clamp(grabBlend, 0.0f, 1.0f);

                        // 1. Calculate the exact world position and rotation targets based on grab offset
                        RE::NiPoint3 targetWorld = hand->world.translate + rotateVector(hand->world.rotate, _grabOffsetLocalHand);
                        RE::NiMatrix3 targetRot = hand->world.rotate * (_grabInitialHandRot.Transpose() * _grabInitialEditableWorldRot);

                        // 2. Transform the target into _node->parent's local space
                        RE::NiPoint3 deltaFromParent = targetWorld - editableParent->world.translate;
                        float pScale = editableParent->world.scale;
                        if (pScale > 0.0001f) {
                            RE::NiPoint3 targetLocalPos = inverseRotateVector(editableParent->world.rotate, deltaFromParent) / pScale;
                            editableNode->local.translate += (targetLocalPos - editableNode->local.translate) * grabBlend;
                        }
                        
                        // 3. Parent-space local rotation directly from targetRot
                        RE::NiMatrix3 targetLocalRot = editableParent->world.rotate.Transpose() * targetRot;
                        for (int r = 0; r < 3; ++r) {
                            for (int c = 0; c < 3; ++c) {
                                editableNode->local.rotate.entry[r][c] +=
                                    (targetLocalRot.entry[r][c] - editableNode->local.rotate.entry[r][c]) * grabBlend;
                            }
                        }
                        editableNode->local.rotate = orthonormalizeMatrix(editableNode->local.rotate);

                        // === Optional Rotation Snapping ===
                        auto& settings = VRUISettings::get();
                        // Skip snapping for pinned items (those with a layoutId) as requested
                        if (settings.bEnableRotationSnapping && settings.bEnableButtonEditMode &&
                            _layoutId.empty() && !_persistItemRotationOnGrab) {
                            // Find nearest node
                            RE::NiAVObject* bestSibling = nullptr;
                            float bestDistSq = settings.fSnapDistanceThreshold * settings.fSnapDistanceThreshold;
                            
                            if (_node->parent) {
                                for (auto& siblingPtr : _node->parent->GetChildren()) {
                                    if (siblingPtr && siblingPtr.get() != _node.get()) {
                                        float distSq = (_node->local.translate - siblingPtr->local.translate).SqrLength();
                                        if (distSq < bestDistSq) {
                                            bestDistSq = distSq;
                                            bestSibling = siblingPtr.get();
                                        }
                                    }
                                }
                            }
                            
                            if (bestSibling) {
                                _node->local.rotate = bestSibling->local.rotate;
                            }
                        }

                        // 4. Scale every grabbed scalable item/widget with the
                        // vertical thumbstick axis from the same grip hand.
                        auto& menuManager = VRMenuManager::get();
                        float thumbX = 0.0f;
                        float thumbY = 0.0f;
                        menuManager.getDominantThumbstick(thumbX, thumbY);

                        constexpr float kMinScale = 0.05f;
                        constexpr float kMaxScale = 20.0f;
                        const auto scaleResult =
                            dragonboard::ui::input::ApplyGripThumbScale(
                                editableNode->local.scale,
                                thumbY,
                                deltaTime,
                                kMinScale,
                                kMaxScale);
                        const bool scaleStarted =
                            scaleResult.active && !_isThumbScaling;
                        _isThumbScaling = scaleResult.active;

                        if (scaleResult.changed) {
                            editableNode->local.scale = scaleResult.scale;
                            if (editableNode == _node.get()) {
                                _currentScale = scaleResult.scale;
                                _targetScale = scaleResult.scale;
                                _baseScale = scaleResult.scale;
                            }
                        }

                        if (scaleStarted) {
                            menuManager.triggerHaptic(true, 0.35f, 0.05f);
                        }
                        if (_isThumbScaling) {
                            _scaleHapticTimer += deltaTime;
                            if (_scaleHapticTimer >= 0.12f) {
                                menuManager.triggerHaptic(true, 0.15f, 0.025f);
                                _scaleHapticTimer = 0.0f;
                            }
                        } else {
                            _scaleHapticTimer = 0.0f;
                        }

                        RE::NiUpdateData grabUpdateData;
                        grabUpdateData.flags = RE::NiUpdateData::Flag::kDirty;
                        editableNode->Update(grabUpdateData);
                        // Bounds are not needed while the RmlUi interaction zone
                        // owns hover. Complex armor and modded NIFs can make a
                        // per-frame UpdateWorldBound() stall the game thread.
                    }
                }
            } else {
                if (_state == ButtonState::Hovered) {
                    if (VRMenuManager::get().isDominantGripButtonDown()) {
                        _grabTimer += deltaTime;
                        if (_grabTimer >= 1.0f) { // 1.0s hold to grab
                            startGrab();
                        }
                    } else {
                        _grabTimer = 0.0f;
                    }
                } else {
                    _grabTimer = 0.0f;
                }
            }

            // Apply custom floating pose if not grabbed (Only if NOT managed by JSON layout)
            if (!_isGrabbed && _layoutId.empty() && _slotIndex >= 0 && _slotIndex < VRUISettings::kMaxSlots && settings.slotFloating[_slotIndex]) {
                // Make sure the button remains visible when floating
                // (grid layout hides it via slotFloatingCache when rendering the current page)
                if (!isVisible()) setVisible(true);
                _node->local.translate.x = settings.slotPosX[_slotIndex];
                _node->local.translate.y = settings.slotPosY[_slotIndex];
                _node->local.translate.z = settings.slotPosZ[_slotIndex];
                
                // Scale override
                _targetScale = settings.slotScaleUser[_slotIndex];
                if (_state == ButtonState::Hovered) _targetScale *= 1.1f;
                if (_state == ButtonState::Pressed) _targetScale *= 0.9f;
            }
        } else {
            if (_isGrabbed) releaseGrab();
            _grabTimer = 0.0f;
        }

        // --- HIGGS Dashboard Proximity Equip ---
        bool leftPinPriorityActive = false;
        bool rightPinPriorityActive = false;
        if (!boardPositionAdjustment && _isDashboardPinned &&
            _onPressHandler && _node && isVisible()) {
            bool isDomGripDown = VRMenuManager::get().isDominantGripButtonDown();
            bool isOffGripDown = VRMenuManager::get().isOffhandGripButtonDown();

            auto* domHand = VRMenuManager::get().getDominantHandNode();
            auto* offHand = VRMenuManager::get().getNonDominantHandNode();

            bool isDomLeft = VRMenuManager::get().isDominantHandLeft();
            bool isOffLeft = VRMenuManager::get().isMenuHandLeft();
            // --- Compute palm positions ---
            RE::NiPoint3 domPalmPos{};
            if (domHand) {
                const char* domMagicName = isDomLeft ? "NPC L MagicNode [LMag]" : "NPC R MagicNode [RMag]";
                auto* domMagic = domHand->GetObjectByName(domMagicName);
                domPalmPos = (domMagic && domMagic->AsNode()) ? domMagic->AsNode()->world.translate : domHand->world.translate;
            }

            RE::NiPoint3 offPalmPos{};
            if (offHand) {
                const char* offMagicName = isOffLeft ? "NPC L MagicNode [LMag]" : "NPC R MagicNode [RMag]";
                auto* offMagic = offHand->GetObjectByName(offMagicName);
                offPalmPos = (offMagic && offMagic->AsNode()) ? offMagic->AsNode()->world.translate : offHand->world.translate;
            }

            // --- Determine which hand (if any) is in proximity ---
            // Measure from the visible mesh surface rather than from the logical
            // button origin. This follows per-item visual offsets and scales.
            constexpr float kProximityPadding = 0.5f;
            const auto proximityVolume = resolveHiggsProximityVolume(
                _node.get(),
                _primaryVisualNode.get());
            const float proximityDistance = proximityVolume.radius + kProximityPadding;

            bool domInRange = domHand && (domPalmPos - proximityVolume.center).Length() <= proximityDistance
                              && g_higgsInterface && !g_higgsInterface->IsHoldingObject(isDomLeft);
            bool offInRange = offHand && (offPalmPos - proximityVolume.center).Length() <= proximityDistance
                              && g_higgsInterface && !g_higgsInterface->IsHoldingObject(isOffLeft);

            if (VRMenuManager::get().isPhysicalBoardActive()) {
                leftPinPriorityActive =
                    (domInRange && isDomLeft) || (offInRange && isOffLeft);
                rightPinPriorityActive =
                    (domInRange && !isDomLeft) || (offInRange && !isOffLeft);
            }

            // Priority: dominant hand first when both are in range
            bool inProximity     = domInRange || offInRange;
            bool proximityIsLeft = domInRange ? isDomLeft : isOffLeft;

            // --- Hover-scale and entry haptic ---
            if (inProximity) {
                // Apply hover scale (only if not already in a full state hover / grab)
                if (_state != ButtonState::Hovered && _state != ButtonState::Pressed && !_isGrabbed) {
                    _targetScale = _baseScale * 1.1f;
                }

                // One-shot haptic on entry: true = dominant hand, false = menu hand
                if (!_wasInHiggsProximity) {
                    _wasInHiggsProximity  = true;
                    _higgsProximityIsLeft = proximityIsLeft;
                    bool hapticIsDominant = (proximityIsLeft == isDomLeft); // dominant if the approaching hand IS the dominant hand
                    VRMenuManager::get().triggerHaptic(hapticIsDominant, 0.3f, 0.06f);
                }
            } else {
                // Restore scale when hand leaves range
                if (_wasInHiggsProximity) {
                    _wasInHiggsProximity = false;
                    if (_state == ButtonState::Normal && !_isGrabbed) {
                        _targetScale = _baseScale;
                    }
                }
            }

            // --- Equip on grip press (edge-trigger) ---
            // Dominant hand
            if (isDomGripDown && !_wasDominantGripDown && domInRange) {
                _onPressHandler(this, isDomLeft ? EquipHand::kLeft : EquipHand::kRight);
                VRMenuManager::get().triggerHaptic(true, 0.6f, 0.12f); // dominant hand
            }

            // Non-dominant hand
            if (isOffGripDown && !_wasNonDominantGripDown && offInRange) {
                _onPressHandler(this, isOffLeft ? EquipHand::kLeft : EquipHand::kRight);
                VRMenuManager::get().triggerHaptic(false, 0.6f, 0.12f); // menu (non-dominant) hand
            }

            _wasDominantGripDown    = isDomGripDown;
            _wasNonDominantGripDown = isOffGripDown;
        }

        dragonboard::integrations::higgs::PhysicalBoardController::GetSingleton()
            .UpdatePinnedItemGrabPriority(
                this,
                leftPinPriorityActive,
                rightPinPriorityActive);

        // --- Drag-to-Drop Generic Processing ---
        if (!boardPositionAdjustment && _onGripDragHandler && _node &&
            VRMenuManager::get().isMenuOpen()) {
            bool isGripDown = VRMenuManager::get().isDominantGripButtonDown();
            auto* hand = VRMenuManager::get().getDominantHandNode();
            auto* headNode = VRMenuManager::get().getHeadNode();

            if (hand) {
                // Calculate position relative to HMD (head) to cancel out ALL locomotion (walk, jump, duck) and body rotation
                RE::NiPoint3 localHandPos = hand->world.translate;
                if (headNode) {
                    localHandPos = headNode->world.rotate.Transpose() * (hand->world.translate - headNode->world.translate);
                }

                if (_state == ButtonState::Hovered && isGripDown && !_isGripDragging) {
                    _isGripDragging = true;
                    _gripDragStartHandPos = localHandPos;
                } else if (!isGripDown && _isGripDragging) {
                    _isGripDragging = false;
                }

                if (_isGripDragging) {
                    float dist = (localHandPos - _gripDragStartHandPos).Length();
                    if (dist > 10.0f) { // threshold, e.g. 10 units
                        _isGripDragging = false; // fire once
                        _onGripDragHandler(this, EquipHand::kRight);
                        setState(ButtonState::Normal);
                        VRMenuManager::get().triggerHaptic(false, 0.5f, 0.4f);
                    }
                }
            }
        }
    }

    void VRUIButton::onRayEnter()
    {
        _isLaserHovered = true;
        if (_state != ButtonState::Pressed) {
            setState(ButtonState::Hovered);
        }
        if (_onHoverHandler) {
            _onHoverHandler(this, true);
        }
        _grabTimer = 0.0f;
        logger::trace("DragonBoardVR: [HOVER ENTER] Button '{}'", _label);
    }

    void VRUIButton::onRayExit()
    {
        _isLaserHovered = false;
        if (_state != ButtonState::Pressed) {
            setState(ButtonState::Normal);
        }
        if (_onHoverHandler) {
            _onHoverHandler(this, false);
        }
        _grabTimer = 0.0f;
        logger::trace("DragonBoardVR: [HOVER EXIT] Button '{}'", _label);
    }

    void VRUIButton::onTriggerPress(EquipHand hand)
    {
        setState(ButtonState::Pressed);
        if (_onPressHandler) {
            _onPressHandler(this, hand);
        }
        logger::trace("DragonBoardVR: [PRESS] Button '{}' (Hand: {})", _label, (int)hand);
    }

    void VRUIButton::onTriggerLongPress(EquipHand hand)
    {
        setState(ButtonState::Pressed);
        if (_onLongPressHandler) {
            _onLongPressHandler(this, hand);
        }
        logger::trace("DragonBoardVR: [LONG PRESS] Button '{}' (Hand: {})", _label, (int)hand);
    }

    void VRUIButton::onSecondaryPress()
    {
        if (_isDashboardPinned && VRUISettings::get().lockPins) {
            logger::trace("DragonBoardVR: ignored secondary press on locked pin '{}'.", _label);
            return;
        }
        if (_onSecondaryPressHandler) {
            _onSecondaryPressHandler(this, EquipHand::kRight);
        }
        logger::trace("DragonBoardVR: [SECONDARY PRESS] Button '{}'", _label);
    }

    void VRUIButton::onSecondaryLongPress()
    {
        if (_isDashboardPinned && VRUISettings::get().lockPins) {
            logger::trace("DragonBoardVR: ignored secondary long press on locked pin '{}'.", _label);
            return;
        }
        if (_onSecondaryLongPressHandler) {
            _onSecondaryLongPressHandler(this, EquipHand::kRight);
        }
        logger::trace("DragonBoardVR: [SECONDARY LONG PRESS] Button '{}'", _label);
    }

    void VRUIButton::onTriggerRelease(EquipHand hand)
    {
        setState(ButtonState::Normal);
        if (_onReleaseHandler) {
            _onReleaseHandler(this, hand);
        }
        logger::trace("DragonBoardVR: [RELEASE] Button '{}' (Hand: {})", _label, (int)hand);
    }

    void VRUIButton::setEquipped(bool equipped)
    {
        _isEquipped = equipped;
    }

    void VRUIButton::startGrab()
    {
        if (_isDashboardPinned && VRUISettings::get().lockPins) {
            _grabTimer = 0.0f;
            logger::trace("DragonBoardVR: ignored grab request for locked pin '{}'.", _label);
            return;
        }
        if (VRMenuManager::get().hasGrabbedWidget()) {
            auto grabbed = VRMenuManager::get().getGrabbedWidget();
            if (grabbed && grabbed.get() != this) {
                return;
            }
        }

        _isGrabbed = true;
        _grabHandIsLeft = VRMenuManager::get().isDominantHandLeft();
        _scaleHapticTimer = 0.0f;
        _grabTimer = 0.0f;
        VRMenuManager::get().setGrabbedWidget(shared_from_this());
        VRMenuManager::get().clearHover();
        
        auto* hand = VRMenuManager::get().getDominantHandNode();
        if (hand && _node) {
            RE::NiNode* editableNode = (_persistItemRotationOnGrab && _primaryVisualNode) ? _primaryVisualNode.get() : _node.get();
            _grabInitialHandPos = hand->world.translate;
            _grabInitialButtonPos = _node->world.translate;
            _grabInitialLocalButtonPos = _node->local.translate;
            _grabInitialLocalButtonScale = _baseScale;
            _grabInitialLocalButtonRot = _node->local.rotate;
            _grabInitialEditableLocalPos = editableNode ? editableNode->local.translate : RE::NiPoint3(0.0f, 0.0f, 0.0f);
            _grabInitialEditableLocalScale = editableNode ? editableNode->local.scale : 1.0f;
            _grabInitialEditableLocalRot = editableNode ? editableNode->local.rotate : RE::NiMatrix3();
            _grabInitialEditableWorldRot = editableNode ? editableNode->world.rotate : RE::NiMatrix3();
            RE::NiPoint3 worldDiff = (editableNode ? editableNode->world.translate : _node->world.translate) - hand->world.translate;
            _grabOffsetLocalHand = inverseRotateVector(hand->world.rotate, worldDiff);
            _grabInitialHandRot = hand->world.rotate;
            _grabInitialButtonRot = _node->world.rotate;
        } else {
            _grabInitialHandPos = RE::NiPoint3(0.0f, 0.0f, 0.0f);
            _grabInitialButtonPos = RE::NiPoint3(0.0f, 0.0f, 0.0f);
            _grabInitialLocalButtonPos = RE::NiPoint3(0.0f, 0.0f, 0.0f);
            _grabInitialLocalButtonScale = 1.0f;
            _grabInitialLocalButtonRot = RE::NiMatrix3();
            _grabOffsetLocalHand = RE::NiPoint3(0.0f, 0.0f, 0.0f);
            _grabInitialHandRot = RE::NiMatrix3();
            _grabInitialButtonRot = RE::NiMatrix3();
            _grabInitialEditableLocalPos = RE::NiPoint3(0.0f, 0.0f, 0.0f);
            _grabInitialEditableLocalScale = 1.0f;
            _grabInitialEditableLocalRot = RE::NiMatrix3();
            _grabInitialEditableWorldRot = RE::NiMatrix3();
        }
        
        VRMenuManager::get().triggerHaptic(true, 1.0f, 0.2f);
        std::string msg = std::format("Grabbed Button: {}", _label);
        // RE::DebugNotification(msg.c_str());
    }

    void VRUIButton::releaseGrab()
    {
        if (!_isGrabbed) return;
        _grabTimer = 0.0f;
        _isThumbScaling = false;
        _scaleHapticTimer = 0.0f;
        VRMenuManager::get().triggerHaptic(true, 1.0f, 0.2f);
        
        if (_node) {
            if (_persistItemRotationOnGrab && _primaryVisualNode && _primaryVisualNode->parent) {
                if (auto* hand = VRMenuManager::get().getDominantHandNode()) {
                    RE::NiPoint3 targetWorld = hand->world.translate + rotateVector(hand->world.rotate, _grabOffsetLocalHand);
                    RE::NiMatrix3 targetRot = hand->world.rotate * (_grabInitialHandRot.Transpose() * _grabInitialEditableWorldRot);

                    auto* editableParent = _primaryVisualNode->parent->AsNode();
                    RE::NiPoint3 deltaFromParent = targetWorld - editableParent->world.translate;
                    float pScale = editableParent->world.scale;
                    if (pScale > 0.0001f) {
                        _primaryVisualNode->local.translate = inverseRotateVector(editableParent->world.rotate, deltaFromParent) / pScale;
                    }

                    _primaryVisualNode->local.rotate = orthonormalizeMatrix(editableParent->world.rotate.Transpose() * targetRot);

                    RE::NiUpdateData updateData;
                    updateData.flags = RE::NiUpdateData::Flag::kDirty;
                    _primaryVisualNode->Update(updateData);
                    _primaryVisualNode->UpdateWorldBound();
                }
            }

            auto& settings = VRUISettings::get();
            float rx = 0, ry = 0, rz = 0;
            VRUILayoutManager::getMatrixEuler(_node->local.rotate, rx, ry, rz);
            // Convert radians to degrees for INI
            rx /= kDegToRad; ry /= kDegToRad; rz /= kDegToRad;

            // 1. Slot-specific persistence/deletion must run BEFORE the generic layoutId path.
            // Slots now often already have a layoutId from JSON, but they still need the
            // drag-far-away delete behavior.
            if (_slotIndex >= 0 && _slotIndex < VRUISettings::kMaxSlots) {
                std::string slotId = std::format("Slot{:02d}", _slotIndex + 1);
                float dist = (_node->world.translate - _grabInitialButtonPos).Length();
                if (dist > 20.0f) {
                    // Delete slot (exceeded 20 units — drag off to discard)
                    settings.slotActions[_slotIndex] = "None";
                    settings.slotFloating[_slotIndex] = false;
                    settings.slotFloatingCache[_slotIndex] = false;
                    _visible = false;
                    _node->SetAppCulled(true);
                    VRUILayoutManager::removeElementAnywhere(slotId);
                } else {
                    settings.slotFloating[_slotIndex] = true;
                    settings.slotFloatingCache[_slotIndex] = true;
                    _layoutId = slotId;
                    VRUILayoutManager::updateElementTransformAnywhere(
                        slotId,
                        _node->local.translate,
                        _node->local.rotate,
                        _node->local.scale,
                        _nifPath,
                        "",
                        0,
                        settings.slotActions[_slotIndex],
                        settings.slotLabels[_slotIndex]);
                }
            }
            // 2. Generic JSON layout save (PRIMARY SOURCE OF TRUTH for non-slot layout widgets)
            // Save local-space coordinates (relative to parent container), because
            // refreshFixedWidgets restores them via setLocalPosition/setLocalRotation.
            else if (!_layoutId.empty()) {
                if (_worldLockedToHeadSpace) {
                    _lockedWorldPos = _node->world.translate;
                    _lockedWorldRot = _node->world.rotate;
                    _lockedWorldScale = _node->world.scale;
                    if (auto* headNode = VRMenuManager::get().getHeadNode()) {
                        _headAnchorLocalPos = worldToLocalPosition(headNode, _lockedWorldPos);
                        _headAnchorLocalRot = headNode->world.rotate.Transpose() * _lockedWorldRot;
                        _hasHeadAnchorLocalTransform = true;
                    }
                    VRUILayoutManager::updateElementTransformAnywhere(_layoutId,
                        _lockedWorldPos, _lockedWorldRot, _lockedWorldScale);
                    VRUILayoutManager::setElementPinToRightHand(_layoutId, true);
                    VRUILayoutManager::setElementPinToWorld(_layoutId, false);
                } else {
                    VRUILayoutManager::updateElementTransformAnywhere(_layoutId, 
                        _node->local.translate, _node->local.rotate, _node->local.scale);
                }
            }
            else if (_fixedWidgetIndex >= 0 && _fixedWidgetIndex < (int)settings.fixedWidgets.size()) {
            auto& item = settings.fixedWidgets[_fixedWidgetIndex];
            item.posX = _node->local.translate.x;
            item.posY = _node->local.translate.y;
            item.posZ = _node->local.translate.z;
            item.rotX = rx;
            item.rotY = ry;
            item.rotZ = rz;
            item.scale = _node->local.scale;
            VRMenuManager::get().requestSettingsSave();
        }
        else if (_persistItemRotationOnGrab && _itemOverrideFormID != 0 && _primaryVisualNode) {
            RE::NiMatrix3 effectiveRot = _primaryVisualNode->local.rotate;
            float effectiveRx = 0.0f;
            float effectiveRy = 0.0f;
            float effectiveRz = 0.0f;
            if (_persistItemRotationUsesLayoutEuler) {
                // ToEulerAnglesXYZ() does not round-trip SetEulerAnglesXYZ() with
                // Skyrim VR's matrix convention. Extract the raw NiMatrix angles
                // explicitly, then convert raw Y to the layout's inverted Y.
                float rawY = 0.0f;
                extractItemEulerForSetXYZ(
                    effectiveRot, effectiveRx, rawY, effectiveRz);
                effectiveRy = -rawY;
            } else {
                extractItemEulerForSetXYZ(
                    effectiveRot, effectiveRx, effectiveRy, effectiveRz);
            }

            RE::NiMatrix3 reconstructedRot{};
            if (_persistItemRotationUsesLayoutEuler) {
                VRUILayoutManager::setMatrixEuler(
                    reconstructedRot, effectiveRx, effectiveRy, effectiveRz);
            } else {
                reconstructedRot.SetEulerAnglesXYZ(
                    effectiveRx, effectiveRy, effectiveRz);
            }

            RE::NiPoint3 localDelta = _primaryVisualNode->local.translate - _grabInitialEditableLocalPos;
            float scaleRatio = (_grabInitialEditableLocalScale > 0.0001f)
                ? (_primaryVisualNode->local.scale / _grabInitialEditableLocalScale)
                : 1.0f;
            float effectivePosX = _persistItemPosX + localDelta.x;
            float effectivePosY = _persistItemPosY + localDelta.y;
            float effectivePosZ = _persistItemPosZ + localDelta.z;
            float effectiveScale = _persistItemScale * scaleRatio;

            logger::trace(
                "DragonBoardVR: Item override grab save formID={:08X} label='{}' "
                "effectiveDeg=({:.2f}, {:.2f}, {:.2f}) "
                "pos=({:.2f}, {:.2f}, {:.2f}) scale={:.3f}",
                _itemOverrideFormID,
                _label,
                effectiveRx / kDegToRad, effectiveRy / kDegToRad, effectiveRz / kDegToRad,
                effectivePosX, effectivePosY, effectivePosZ,
                effectiveScale);
            logger::trace(
                "DragonBoardVR: Item override grab matrices formID={:08X} label='{}' "
                "visualLocal={} reconstructed={}",
                _itemOverrideFormID,
                _label,
                formatMatrixRows(effectiveRot),
                formatMatrixRows(reconstructedRot));

            ItemOffsetData data;
            data.posX = effectivePosX;
            data.posY = effectivePosY;
            data.posZ = effectivePosZ;
            data.rotX = effectiveRx / kDegToRad;
            data.rotY = effectiveRy / kDegToRad;
            data.rotZ = effectiveRz / kDegToRad;
            data.scale = effectiveScale;
            ItemUtils::setItemOverride(RE::TESForm::LookupByID(_itemOverrideFormID), data);
            setItemRotationPersistence(
                _itemOverrideFormID,
                data.posX,
                data.posY,
                data.posZ,
                data.scale,
                data.rotX,
                data.rotY,
                data.rotZ);
            _itemXOffset = data.posX;
            _itemYOffset = data.posY;
            _itemZOffset = data.posZ;
            _itemScaleMult = data.scale;
            _itemRotOverrideX = data.rotX;
            _itemRotOverrideY = data.rotY;
            _itemRotOverrideZ = data.rotZ;
            // Persist through the existing debounce instead of blocking the
            // release frame with synchronous file I/O. The current one-item
            // preview already displays the final transform, so no grid refresh
            // is required here.
            VRMenuManager::get().requestSettingsSave();
        }
        else if (_slotIndex == -1) {
            bool changed = true;
            const std::string& buttonId = _buttonId.empty() ? _label : _buttonId;
            
            if (buttonId == "Status") {
                settings.bStatusPosX = _node->local.translate.x; settings.bStatusPosY = _node->local.translate.y; settings.bStatusPosZ = _node->local.translate.z;
                settings.bStatusRotX = rx; settings.bStatusRotY = ry; settings.bStatusRotZ = rz;
                settings.bStatusScale = _node->local.scale;
            } else if (buttonId == "Inventory") {
                settings.bInvPosX = _node->local.translate.x; settings.bInvPosY = _node->local.translate.y; settings.bInvPosZ = _node->local.translate.z;
                settings.bInvRotX = rx; settings.bInvRotY = ry; settings.bInvRotZ = rz;
                settings.bInvScale = _node->local.scale;
            } else if (buttonId == "Magic") {
                settings.bMagicPosX = _node->local.translate.x; settings.bMagicPosY = _node->local.translate.y; settings.bMagicPosZ = _node->local.translate.z;
                settings.bMagicRotX = rx; settings.bMagicRotY = ry; settings.bMagicRotZ = rz;
                settings.bMagicScale = _node->local.scale;
            } else if (buttonId == "System") {
                settings.bSysPosX = _node->local.translate.x; settings.bSysPosY = _node->local.translate.y; settings.bSysPosZ = _node->local.translate.z;
                settings.bSysRotX = rx; settings.bSysRotY = ry; settings.bSysRotZ = rz;
                settings.bSysScale = _node->local.scale;
            } else if (buttonId == "Save") {
                settings.bSavePosX = _node->local.translate.x; settings.bSavePosY = _node->local.translate.y; settings.bSavePosZ = _node->local.translate.z;
                settings.bSaveRotX = rx; settings.bSaveRotY = ry; settings.bSaveRotZ = rz;
                settings.bSaveScale = _node->local.scale;
            } else if (buttonId == "Home") {
                settings.bHomePosX = _node->local.translate.x; settings.bHomePosY = _node->local.translate.y; settings.bHomePosZ = _node->local.translate.z;
                settings.bHomeRotX = rx; settings.bHomeRotY = ry; settings.bHomeRotZ = rz;
                settings.bHomeScale = _node->local.scale;
            } else if (buttonId == "Mods") {
                settings.bModsPosX = _node->local.translate.x; settings.bModsPosY = _node->local.translate.y; settings.bModsPosZ = _node->local.translate.z;
                settings.bModsRotX = rx; settings.bModsRotY = ry; settings.bModsRotZ = rz;
                settings.bModsScale = _node->local.scale;
            } else if (buttonId == "Journal") {
                settings.bFavPosX = _node->local.translate.x; settings.bFavPosY = _node->local.translate.y; settings.bFavPosZ = _node->local.translate.z;
                settings.bFavRotX = rx; settings.bFavRotY = ry; settings.bFavRotZ = rz;
                settings.bFavScale = _node->local.scale;
            } else if (buttonId == "Map") {
                settings.bMapPosX = _node->local.translate.x; settings.bMapPosY = _node->local.translate.y; settings.bMapPosZ = _node->local.translate.z;
                settings.bMapRotX = rx; settings.bMapRotY = ry; settings.bMapRotZ = rz;
                settings.bMapScale = _node->local.scale;
            } else if (buttonId == "Dev") {
                settings.bDevPosX = _node->local.translate.x; settings.bDevPosY = _node->local.translate.y; settings.bDevPosZ = _node->local.translate.z;
                settings.bDevRotX = rx; settings.bDevRotY = ry; settings.bDevRotZ = rz;
                settings.bDevScale = _node->local.scale;
            } else if (buttonId == "Add Function") {
                settings.bAddFuncPosX = _node->local.translate.x; settings.bAddFuncPosY = _node->local.translate.y; settings.bAddFuncPosZ = _node->local.translate.z;
                settings.bAddFuncRotX = rx; settings.bAddFuncRotY = ry; settings.bAddFuncRotZ = rz;
                settings.bAddFuncScale = _node->local.scale;
            } else if (getName() == "GoldDisplay") {
                settings.bGoldPosX = _node->local.translate.x; settings.bGoldPosY = _node->local.translate.y; settings.bGoldPosZ = _node->local.translate.z;
                settings.bGoldRotX = rx; settings.bGoldRotY = ry; settings.bGoldRotZ = rz;
                settings.bGoldScale = _node->local.scale;
            } else {
                changed = false;
            }
            if (changed) {
                VRMenuManager::get().requestSettingsSave();
            }
        }
        else if (_canBePersistent && _parent && _node) {
            auto bgPanel = VRMenuManager::get().findPanelByName("Persistent_Panel");
            if (bgPanel && bgPanel.get() != _parent) {
                
                // Calculate position relative to Persistent_Panel BEFORE unparenting
                RE::NiPoint3 worldPos = _node->world.translate;
                RE::NiMatrix3 worldRot = _node->world.rotate;
                
                // Grab strong pointer before detachment
                auto self = std::dynamic_pointer_cast<VRUIButton>(shared_from_this());
                
                if (auto* container = dynamic_cast<VRUIContainer*>(_parent)) {
                    container->removeElement(self); // Removes from dynamic container safely
                }
                
                _isPersistent = true;
                bgPanel->addElement(self);
                
                // Now recalculate local translate/rot inside Persistent_Panel!
                RE::NiNode* newParentNode = bgPanel->getNode();
                if (newParentNode) {
                    float pScale = newParentNode->world.scale;
                    RE::NiPoint3 delta = worldPos - newParentNode->world.translate;
                    
                    RE::NiPoint3 localPos;
                    // Apply inverse rotation
                    auto& rootRot = newParentNode->world.rotate;
                    localPos.x = (rootRot.entry[0][0]*delta.x + rootRot.entry[1][0]*delta.y + rootRot.entry[2][0]*delta.z) / (pScale > 0.0001f ? pScale : 1.0f);
                    localPos.y = (rootRot.entry[0][1]*delta.x + rootRot.entry[1][1]*delta.y + rootRot.entry[2][1]*delta.z) / (pScale > 0.0001f ? pScale : 1.0f);
                    localPos.z = (rootRot.entry[0][2]*delta.x + rootRot.entry[1][2]*delta.y + rootRot.entry[2][2]*delta.z) / (pScale > 0.0001f ? pScale : 1.0f);

                    _node->local.translate = localPos;
                    _node->local.rotate = rootRot.Transpose() * worldRot;
                    RE::NiUpdateData updateData;
                    _node->Update(updateData);
                }
            }
        }
        }
        _isGrabbed = false;
        VRMenuManager::get().clearGrabbedWidget(this);

        if (_onGrabReleaseHandler) {
            _onGrabReleaseHandler(this);
        }

        // An owning editor callback keeps its live preview and source state in
        // sync. Rebuilding all dynamic containers here would immediately
        // reapply a second transform and make the item jump after release.
    }

    void VRUIButton::setDashboardPinned(bool pinned)
    {
        if (_isDashboardPinned == pinned) {
            return;
        }

        _isDashboardPinned = pinned;
        _wasInHiggsProximity = false;
        _higgsProximityIsLeft = false;
        _wasDominantGripDown = pinned ?
            VRMenuManager::get().isDominantGripButtonDown() : false;
        _wasNonDominantGripDown = pinned ?
            VRMenuManager::get().isOffhandGripButtonDown() : false;

        if (!pinned && _state == ButtonState::Normal && !_isGrabbed) {
            _targetScale = _baseScale;
        }
    }

    void VRUIButton::triggerEntranceAnimation(float& accumDelay)
    {
        if (!_noPopAnimation && _visible) {
            startScaleAnimation(accumDelay);
            accumDelay += 0.025f;
        }
        VRUIWidget::triggerEntranceAnimation(accumDelay);
    }
}
