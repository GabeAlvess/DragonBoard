#pragma once

#include <string>
#include <vector>
#include <optional>
#include <RE/N/NiPoint3.h>
#include <RE/N/NiMatrix3.h>

namespace vrui {
    struct UIJSONTransform {
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        float rx = 0.0f;
        float ry = 0.0f;
        float rz = 0.0f;
        float scale = 1.0f;
        bool hasMatrix = false;
        float m[3][3] = { {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f} };
    };

    struct UIJSONVisuals {
        std::string model;
        std::string icon;
        uint32_t color = 0x333333;
        float w = 10.0f, h = 10.0f, d = 2.0f;
    };

    struct UIJSONElement {
        std::string id;
        std::string label;
        std::string actionFunc;
        uint32_t formID = 0;
        std::string category;
        bool hideLabel = false;
        bool pinToWorld = false;
        bool pinToRightHand = false;
        bool legacyHmdPin = false;
        bool visualTransformComposed = false;
        UIJSONTransform transform;
        UIJSONVisuals visuals;
    };

    struct UIJSONContainer {
        std::string id;
        std::string type;
        UIJSONTransform transform;
        UIJSONVisuals visuals;
        std::vector<UIJSONElement> elements;
    };

    class VRUILayoutManager {
    public:
        static void loadLayout();
        static void saveLayout();

        static std::optional<UIJSONContainer> getContainer(const std::string& id);
        static std::optional<UIJSONElement> getElement(const std::string& containerId, const std::string& elementId);
        static std::optional<UIJSONElement> findElementAnywhere(const std::string& elementId);
        static std::vector<UIJSONElement> getContainerElements(const std::string& containerId);

        static void updateElementTransform(const std::string& containerId, const std::string& elementId,
                                           const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale);
        
        static void updateElementTransformAnywhere(const std::string& elementId,
                                                   const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale,
                                                   const std::string& nifPath = "", const std::string& category = "", uint32_t formID = 0,
                                                   const std::string& actionFunc = "", const std::string& label = "",
                                                   std::optional<bool> pinToWorld = std::nullopt,
                                                   std::optional<bool> pinToRightHand = std::nullopt,
                                                   std::optional<bool> visualTransformComposed = std::nullopt);

        static void updateElementTransformAnywhereDirect(const std::string& elementId,
                                                         const RE::NiPoint3& pos, float rotX, float rotY, float rotZ, float scale,
                                                         const std::string& nifPath = "", const std::string& category = "", uint32_t formID = 0,
                                                         const std::string& actionFunc = "", const std::string& label = "",
                                                         std::optional<bool> pinToWorld = std::nullopt,
                                                         std::optional<bool> pinToRightHand = std::nullopt,
                                                         std::optional<bool> visualTransformComposed = std::nullopt);

        static void setElementHideLabel(const std::string& elementId, bool hide);
        static void setElementPinToWorld(const std::string& elementId, bool pinToWorld);
        static void setElementPinToRightHand(const std::string& elementId, bool pinToRightHand);
        static void removeElementAnywhere(const std::string& elementId);

        static void registerDefaultLayout(const std::string& containerId, const std::string& elementId,
                                          const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale);
        static void registerDefaultContainer(const std::string& containerId, const std::string& type,
                                             const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale);

        static void applyLayoutToWidget(class VRUIWidget* widget, const std::string& elementId);
        static void setMatrixEuler(RE::NiMatrix3& mat, float pitch, float yaw, float roll);
        static void getMatrixEuler(const RE::NiMatrix3& mat, float& pitch, float& yaw, float& roll);

    private:
        static std::vector<UIJSONContainer> _containers;
        static std::string _filePath;
    };
}
