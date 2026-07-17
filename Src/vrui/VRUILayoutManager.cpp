#include "pch.h"
#include "VRUILayoutManager.h"
#include "VRMenuManager.h"
#include "VRUIWidget.h"
#include "VRUISettings.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <RE/S/Script.h>

namespace vrui {

    namespace
    {
        std::string getDefaultContainerIdForElement(const std::string& elementId)
        {
            if (elementId.rfind("Slot", 0) == 0) {
                return "IndependentSlots";
            }
            return "Dashboard";
        }

        void runtimeEulerToEditorEuler(const std::string& elementId, float runtimeX, float runtimeY, float runtimeZ,
                                       float& editorX, float& editorY, float& editorZ)
        {
            editorX = runtimeX;
            editorY = runtimeY;
            editorZ = runtimeZ;
        }

        void editorEulerToRuntimeEuler(const std::string& elementId, float editorX, float editorY, float editorZ,
                                       float& runtimeX, float& runtimeY, float& runtimeZ)
        {
            runtimeX = editorX;
            runtimeY = editorY;
            runtimeZ = editorZ;
        }

        void setTransformFromRuntimeMatrix(UIJSONTransform& transform, const std::string& elementId,
                                           const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale,
                                           bool invertBowZ = false)
        {
            transform.px = pos.x;
            transform.py = pos.y;
            transform.pz = pos.z;
            transform.scale = scale;

            transform.hasMatrix = true;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    transform.m[r][c] = rot.entry[r][c];
                }
            }

            float runtimeX = 0.0f;
            float runtimeY = 0.0f;
            float runtimeZ = 0.0f;
            VRUILayoutManager::getMatrixEuler(rot, runtimeX, runtimeY, runtimeZ);
            runtimeX *= (180.0f / 3.14159265f);
            runtimeY *= (180.0f / 3.14159265f);
            runtimeZ *= (180.0f / 3.14159265f);
            if (invertBowZ) {
                runtimeZ = -runtimeZ;
            }

            runtimeEulerToEditorEuler(elementId, runtimeX, runtimeY, runtimeZ,
                                      transform.rx, transform.ry, transform.rz);
        }

        void setTransformFromRuntimeEuler(UIJSONTransform& transform, const std::string& elementId,
                                          const RE::NiPoint3& pos, float runtimeRotX, float runtimeRotY, float runtimeRotZ, float scale)
        {
            transform.px = pos.x;
            transform.py = pos.y;
            transform.pz = pos.z;
            transform.scale = scale;

            RE::NiMatrix3 runtimeMatrix;
            VRUILayoutManager::setMatrixEuler(runtimeMatrix,
                                              runtimeRotX * kDegToRad,
                                              runtimeRotY * kDegToRad,
                                              runtimeRotZ * kDegToRad);
            transform.hasMatrix = true;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    transform.m[r][c] = runtimeMatrix.entry[r][c];
                }
            }

            runtimeEulerToEditorEuler(elementId, runtimeRotX, runtimeRotY, runtimeRotZ,
                                      transform.rx, transform.ry, transform.rz);
        }
    }

    std::vector<UIJSONContainer> VRUILayoutManager::_containers;
    std::string VRUILayoutManager::_filePath = "Data/SKSE/Plugins/DragonBoardVR_Layout.json";

    void VRUILayoutManager::loadLayout() {
        _containers.clear();

        logger::trace("DragonBoardVR: Loading layout from '{}'", _filePath);

        if (!std::filesystem::exists(_filePath)) {
            logger::warn("DragonBoardVR: Layout JSON not found at '{}'. Using defaults.", _filePath);
            return;
        }

        try {
            std::ifstream file(_filePath);
            nlohmann::json j;
            file >> j;

            if (j.contains("containers") && j["containers"].is_array()) {
                for (const auto& c : j["containers"]) {
                    UIJSONContainer container;
                    container.id = c.value("id", "");
                    container.type = c.value("type", "Free");

                    if (c.contains("transform")) {
                        auto& t = c["transform"];
                        if (t.contains("position")) {
                            container.transform.px = t["position"].value("x", 0.0f);
                            container.transform.py = t["position"].value("y", 0.0f);
                            container.transform.pz = t["position"].value("z", 0.0f);
                        }
                        if (t.contains("rotation")) {
                            container.transform.rx = t["rotation"].value("x", 0.0f);
                            container.transform.ry = t["rotation"].value("y", 0.0f);
                            container.transform.rz = t["rotation"].value("z", 0.0f);
                        }
                        container.transform.scale = t.value("scale", 1.0f);
                        container.transform.hasMatrix = t.value("hasMatrix", false);
                        if (t.contains("matrix") && t["matrix"].is_array() && t["matrix"].size() == 3) {
                            for (int r = 0; r < 3; ++r) {
                                if (t["matrix"][r].is_array() && t["matrix"][r].size() == 3) {
                                    for (int c = 0; c < 3; ++c) {
                                        container.transform.m[r][c] = t["matrix"][r][c].get<float>();
                                    }
                                }
                            }
                        }
                    }

                    if (c.contains("elements") && c["elements"].is_array()) {
                        for (const auto& el : c["elements"]) {
                            UIJSONElement elem;
                            elem.id = el.value("id", "");
                            elem.label = el.value("label", "");
                            elem.actionFunc = el.value("function", "");
                            elem.formID = (uint32_t)el.value("formID", 0);
                            elem.category = el.value("category", "");
                            elem.hideLabel = el.value("hideLabel", false);
                            elem.pinToWorld = el.value("pinToWorld", false);
                            elem.pinToHmdWorld = el.value("pinToHmdWorld", false);
                            elem.visualTransformComposed = el.value("visualTransformComposed", false);

                            if (el.contains("transform")) {
                                auto& t = el["transform"];
                                if (t.contains("position")) {
                                    elem.transform.px = t["position"].value("x", 0.0f);
                                    elem.transform.py = t["position"].value("y", 0.0f);
                                    elem.transform.pz = t["position"].value("z", 0.0f);
                                }
                                if (t.contains("rotation")) {
                                    elem.transform.rx = t["rotation"].value("x", 0.0f);
                                    elem.transform.ry = t["rotation"].value("y", 0.0f);
                                    elem.transform.rz = t["rotation"].value("z", 0.0f);
                                }
                                elem.transform.scale = t.value("scale", 1.0f);
                                elem.transform.hasMatrix = t.value("hasMatrix", false);
                                if (t.contains("matrix") && t["matrix"].is_array() && t["matrix"].size() == 3) {
                                    for (int r = 0; r < 3; ++r) {
                                        if (t["matrix"][r].is_array() && t["matrix"][r].size() == 3) {
                                            for (int c = 0; c < 3; ++c) {
                                                elem.transform.m[r][c] = t["matrix"][r][c].get<float>();
                                            }
                                        }
                                    }
                                }
                            }
                            
                            if (el.contains("visuals")) {
                                auto& v = el["visuals"];
                                elem.visuals.model = v.value("model", "");
                                elem.visuals.icon = v.value("icon", "");
                            }

                            container.elements.push_back(elem);
                        }
                    }
                    _containers.push_back(container);
                }
            }
            logger::trace("DragonBoardVR: Successfully parsed JSON layout with {} containers.", _containers.size());
        } catch (const std::exception& e) {
            logger::error("DragonBoardVR: Error reading layout JSON: {}", e.what());
        }
    }

    void VRUILayoutManager::saveLayout() {
        try {
            nlohmann::json root;
            nlohmann::json containersArray = nlohmann::json::array();

            for (const auto& c : _containers) {
                nlohmann::json jc;
                jc["id"] = c.id;
                jc["type"] = c.type;
                
                nlohmann::json jt;
                jt["position"] = { {"x", c.transform.px}, {"y", c.transform.py}, {"z", c.transform.pz} };
                jt["rotation"] = { {"x", c.transform.rx}, {"y", c.transform.ry}, {"z", c.transform.rz} };
                jt["scale"] = c.transform.scale;
                jt["hasMatrix"] = c.transform.hasMatrix;
                nlohmann::json jcm = nlohmann::json::array();
                for (int r = 0; r < 3; ++r) {
                    nlohmann::json row = nlohmann::json::array();
                    for (int col = 0; col < 3; ++col) {
                        row.push_back(c.transform.m[r][col]);
                    }
                    jcm.push_back(row);
                }
                jt["matrix"] = jcm;
                jc["transform"] = jt;

                nlohmann::json els = nlohmann::json::array();
                for (const auto& e : c.elements) {
                    nlohmann::json je;
                    je["id"] = e.id;
                    je["label"] = e.label;
                    je["function"] = e.actionFunc;
                    je["formID"] = e.formID;
                    je["category"] = e.category;
                    je["hideLabel"] = e.hideLabel;
                    je["pinToWorld"] = e.pinToWorld;
                    je["pinToHmdWorld"] = e.pinToHmdWorld;
                    je["visualTransformComposed"] = e.visualTransformComposed;

                    nlohmann::json jte;
                    jte["position"] = { {"x", e.transform.px}, {"y", e.transform.py}, {"z", e.transform.pz} };
                    jte["rotation"] = { {"x", e.transform.rx}, {"y", e.transform.ry}, {"z", e.transform.rz} };
                    jte["scale"] = e.transform.scale;
                    jte["hasMatrix"] = e.transform.hasMatrix;
                    nlohmann::json jem = nlohmann::json::array();
                    for (int r = 0; r < 3; ++r) {
                        nlohmann::json row = nlohmann::json::array();
                        for (int col = 0; col < 3; ++col) {
                            row.push_back(e.transform.m[r][col]);
                        }
                        jem.push_back(row);
                    }
                    jte["matrix"] = jem;
                    je["transform"] = jte;

                    nlohmann::json jve;
                    jve["model"] = e.visuals.model;
                    jve["icon"] = e.visuals.icon;
                    je["visuals"] = jve;

                    els.push_back(je);
                }
                jc["elements"] = els;
                containersArray.push_back(jc);
            }

            root["containers"] = containersArray;

            // Keep layout persistence ordered with the in-memory mutations.
            // Pin operations are batched, so this is now one bounded write
            // instead of three full-file writes for a single action.
            std::ofstream file(_filePath, std::ios::binary | std::ios::trunc);
            file << root.dump(4);
            logger::trace("DragonBoardVR: Successfully saved JSON layout.");
        } catch (const std::exception& e) {
            logger::error("DragonBoardVR: Error saving layout JSON: {}", e.what());
        }
    }

    std::optional<UIJSONContainer> VRUILayoutManager::getContainer(const std::string& id) {
        for (const auto& c : _containers) {
            if (c.id == id) return c;
        }
        return std::nullopt;
    }

    std::optional<UIJSONElement> VRUILayoutManager::getElement(const std::string& containerId, const std::string& elementId) {
        auto co = getContainer(containerId);
        if (co) {
            for (const auto& el : co->elements) {
                if (el.id == elementId) return el;
            }
        }
        return std::nullopt;
    }

    std::optional<UIJSONElement> VRUILayoutManager::findElementAnywhere(const std::string& elementId) {
        for (const auto& c : _containers) {
            for (const auto& el : c.elements) {
                if (el.id == elementId) {
                    return el;
                }
            }
        }
        return std::nullopt;
    }

    void VRUILayoutManager::updateElementTransform(const std::string& containerId, const std::string& elementId,
                                                   const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale) {
        for (auto& c : _containers) {
            if (c.id == containerId) {
                for (auto& e : c.elements) {
                    if (e.id == elementId) {
                        bool isBow = false;
                        if (e.formID != 0) {
                            if (auto* form = RE::TESForm::LookupByID(e.formID)) {
                                if (auto* weap = form->As<RE::TESObjectWEAP>()) {
                                    if (weap->GetWeaponType() == RE::WEAPON_TYPE::kBow) {
                                        isBow = true;
                                    }
                                }
                            }
                        }
                        setTransformFromRuntimeMatrix(e.transform, elementId, pos, rot, scale, isBow);
                        saveLayout();
                        return;
                    }
                }
            }
        }
    }

    std::vector<UIJSONElement> VRUILayoutManager::getContainerElements(const std::string& containerId) {
        for (const auto& c : _containers) {
            if (c.id == containerId) return c.elements;
        }
        return {};
    }

    void VRUILayoutManager::updateElementTransformAnywhere(const std::string& elementId,
                                                           const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale,
                                                           const std::string& nifPath, const std::string& category, uint32_t formID,
                                                           const std::string& actionFunc, const std::string& label,
                                                           std::optional<bool> pinToWorld,
                                                           std::optional<bool> pinToHmdWorld,
                                                           std::optional<bool> visualTransformComposed) {
        bool found = false;

        bool isBow = false;
        if (formID != 0) {
            if (auto* form = RE::TESForm::LookupByID(formID)) {
                if (auto* weap = form->As<RE::TESObjectWEAP>()) {
                    if (weap->GetWeaponType() == RE::WEAPON_TYPE::kBow) {
                        isBow = true;
                    }
                }
            }
        }

        for (auto& c : _containers) {
            for (auto& e : c.elements) {
                if (e.id == elementId) {
                    setTransformFromRuntimeMatrix(e.transform, elementId, pos, rot, scale, isBow);
                    if (!nifPath.empty()) e.visuals.model = nifPath;
                    if (!category.empty()) e.category = category;
                    if (formID != 0) e.formID = formID;
                    if (!actionFunc.empty()) e.actionFunc = actionFunc;
                    if (!label.empty()) e.label = label;
                    if (pinToWorld) e.pinToWorld = *pinToWorld;
                    if (pinToHmdWorld) e.pinToHmdWorld = *pinToHmdWorld;
                    if (visualTransformComposed) e.visualTransformComposed = *visualTransformComposed;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        if (!found) {
            UIJSONElement ne;
            ne.id = elementId;
            setTransformFromRuntimeMatrix(ne.transform, elementId, pos, rot, scale, isBow);
            ne.visuals.model = nifPath;
            ne.category = category;
            ne.formID = formID;
            ne.actionFunc = actionFunc;
            ne.label = label;
            if (pinToWorld) ne.pinToWorld = *pinToWorld;
            if (pinToHmdWorld) ne.pinToHmdWorld = *pinToHmdWorld;
            if (visualTransformComposed) ne.visualTransformComposed = *visualTransformComposed;

            const std::string targetContainerId = getDefaultContainerIdForElement(elementId);
            bool containerFound = false;
            for (auto& c : _containers) {
                if (c.id == targetContainerId) {
                    c.elements.push_back(ne);
                    containerFound = true;
                    break;
                }
            }
            if (!containerFound) {
                UIJSONContainer dc;
                dc.id = targetContainerId;
                dc.type = "Free";
                dc.elements.push_back(ne);
                _containers.push_back(dc);
            }
        }

        saveLayout();
    }

    void VRUILayoutManager::updateElementTransformAnywhereDirect(const std::string& elementId,
                                                                 const RE::NiPoint3& pos, float rotX, float rotY, float rotZ, float scale,
                                                                 const std::string& nifPath, const std::string& category, uint32_t formID,
                                                                 const std::string& actionFunc, const std::string& label,
                                                                 std::optional<bool> pinToWorld,
                                                                 std::optional<bool> pinToHmdWorld,
                                                                 std::optional<bool> visualTransformComposed) {
        bool found = false;

        for (auto& c : _containers) {
            for (auto& e : c.elements) {
                if (e.id == elementId) {
                    setTransformFromRuntimeEuler(e.transform, elementId, pos, rotX, rotY, rotZ, scale);
                    if (!nifPath.empty()) e.visuals.model = nifPath;
                    if (!category.empty()) e.category = category;
                    if (formID != 0) e.formID = formID;
                    if (!actionFunc.empty()) e.actionFunc = actionFunc;
                    if (!label.empty()) e.label = label;
                    if (pinToWorld) e.pinToWorld = *pinToWorld;
                    if (pinToHmdWorld) e.pinToHmdWorld = *pinToHmdWorld;
                    if (visualTransformComposed) e.visualTransformComposed = *visualTransformComposed;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        if (!found) {
            UIJSONElement ne;
            ne.id = elementId;
            setTransformFromRuntimeEuler(ne.transform, elementId, pos, rotX, rotY, rotZ, scale);
            ne.visuals.model = nifPath;
            ne.category = category;
            ne.formID = formID;
            ne.actionFunc = actionFunc;
            ne.label = label;
            if (pinToWorld) ne.pinToWorld = *pinToWorld;
            if (pinToHmdWorld) ne.pinToHmdWorld = *pinToHmdWorld;
            if (visualTransformComposed) ne.visualTransformComposed = *visualTransformComposed;

            const std::string targetContainerId = getDefaultContainerIdForElement(elementId);
            bool containerFound = false;
            for (auto& c : _containers) {
                if (c.id == targetContainerId) {
                    c.elements.push_back(ne);
                    containerFound = true;
                    break;
                }
            }
            if (!containerFound) {
                UIJSONContainer dc;
                dc.id = targetContainerId;
                dc.type = "Free";
                dc.elements.push_back(ne);
                _containers.push_back(dc);
            }
        }

        saveLayout();
    }

    void VRUILayoutManager::applyLayoutToWidget(VRUIWidget* widget, const std::string& elementId) {
        auto layout = findElementAnywhere(elementId);
        if (layout && widget) {
            widget->setLayoutId(elementId);
            widget->setLocalPosition(RE::NiPoint3(layout->transform.px, layout->transform.py, layout->transform.pz));
            RE::NiMatrix3 rot;
            if (layout->transform.hasMatrix) {
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) {
                        rot.entry[r][c] = layout->transform.m[r][c];
                    }
                }
            } else {
                float runtimeX = 0.0f;
                float runtimeY = 0.0f;
                float runtimeZ = 0.0f;
                editorEulerToRuntimeEuler(elementId, layout->transform.rx, layout->transform.ry, layout->transform.rz,
                                          runtimeX, runtimeY, runtimeZ);
                setMatrixEuler(rot, 
                    runtimeX * kDegToRad,
                    runtimeY * kDegToRad,
                    runtimeZ * kDegToRad);
            }
            widget->setLocalRotation(rot);
            widget->setLocalScale(layout->transform.scale);
        }
    }

    void VRUILayoutManager::setElementHideLabel(const std::string& elementId, bool hide) {
        for (auto& c : _containers) {
            for (auto& e : c.elements) {
                if (e.id == elementId) {
                    e.hideLabel = hide;
                    saveLayout();
                    return;
                }
            }
        }
    }

    void VRUILayoutManager::setElementPinToWorld(const std::string& elementId, bool pinToWorld)
    {
        for (auto& c : _containers) {
            for (auto& e : c.elements) {
                if (e.id == elementId) {
                    e.pinToWorld = pinToWorld;
                    saveLayout();
                    return;
                }
            }
        }
    }

    void VRUILayoutManager::setElementPinToHmdWorld(const std::string& elementId, bool pinToHmdWorld)
    {
        for (auto& c : _containers) {
            for (auto& e : c.elements) {
                if (e.id == elementId) {
                    e.pinToHmdWorld = pinToHmdWorld;
                    saveLayout();
                    return;
                }
            }
        }
    }

    void VRUILayoutManager::removeElementAnywhere(const std::string& elementId) {
        bool changedJSON = false;
        
        // 1. Remove from JSON containers
        for (auto& c : _containers) {
            auto it = std::remove_if(c.elements.begin(), c.elements.end(), [&](const UIJSONElement& e) {
                return e.id == elementId;
            });
            if (it != c.elements.end()) {
                c.elements.erase(it, c.elements.end());
                changedJSON = true;
            }
        }

        // 2. Remove from INI (FixedWidgets and Slots)
        auto& settings = VRUISettings::get();
        bool changedINI = false;

        // Check FixedWidgets
        auto itFixed = std::remove_if(settings.fixedWidgets.begin(), settings.fixedWidgets.end(), [&](const FixedWidgetItem& item) {
            return item.name == elementId; 
        });
        if (itFixed != settings.fixedWidgets.end()) {
            settings.fixedWidgets.erase(itFixed, settings.fixedWidgets.end());
            changedINI = true;
        }

        // Check Slots (if elementId is "Slot1", "Slot2", etc.)
        if (elementId.size() >= 5 && elementId.substr(0, 4) == "Slot") {
            try {
                int slotIdx = std::stoi(elementId.substr(4)) - 1;
                if (slotIdx >= 0 && slotIdx < VRUISettings::kMaxSlots) {
                    settings.slotActions[slotIdx] = "None";
                    settings.slotNifs[slotIdx] = "";
                    settings.slotTextures[slotIdx] = "";
                    changedINI = true;
                }
            } catch (...) {}
        }

        if (changedINI) {
            VRMenuManager::get().requestSettingsSave();
            logger::trace("DragonBoardVR: Element '{}' removed from INI", elementId);
        }

        if (changedJSON) {
            saveLayout();
            logger::trace("DragonBoardVR: Element '{}' removed from JSON", elementId);
        }
    }

    void VRUILayoutManager::registerDefaultLayout(const std::string& containerId, const std::string& elementId,
                                                  const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale) {
        if (findElementAnywhere(elementId)) {
            return; // Already exists
        }

        UIJSONElement ne;
        ne.id = elementId;
        setTransformFromRuntimeMatrix(ne.transform, elementId, pos, rot, scale);

        bool containerFound = false;
        for (auto& c : _containers) {
            if (c.id == containerId) {
                c.elements.push_back(ne);
                containerFound = true;
                break;
            }
        }

        if (!containerFound) {
            UIJSONContainer dc;
            dc.id = containerId;
            dc.type = "Free"; // Default newly created containers to Free
            dc.elements.push_back(ne);
            _containers.push_back(dc);
        }

        saveLayout();
    }

    void VRUILayoutManager::registerDefaultContainer(const std::string& containerId, const std::string& type,
                                                     const RE::NiPoint3& pos, const RE::NiMatrix3& rot, float scale) {
        bool found = false;
        for (const auto& c : _containers) {
            if (c.id == containerId) {
                found = true;
                break;
            }
        }
        if (found) return;

        UIJSONContainer dc;
        dc.id = containerId;
        dc.type = type;
        dc.transform.px = pos.x; dc.transform.py = pos.y; dc.transform.pz = pos.z;
        dc.transform.scale = scale;
        dc.transform.hasMatrix = true;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                dc.transform.m[r][c] = rot.entry[r][c];
            }
        }

        float degX, degY, degZ;
        getMatrixEuler(rot, degX, degY, degZ);
        dc.transform.rx = degX * (180.0f / 3.14159265f);
        dc.transform.ry = degY * (180.0f / 3.14159265f);
        dc.transform.rz = degZ * (180.0f / 3.14159265f);

        _containers.push_back(dc);
        saveLayout();
    }

    void VRUILayoutManager::setMatrixEuler(RE::NiMatrix3& mat, float x, float y, float z) {
        // User requested inversion of the Y-axis (Roll) to fix the 30-degree rotation shift
        mat.SetEulerAnglesXYZ(x, -y, z);
    }

    void VRUILayoutManager::getMatrixEuler(const RE::NiMatrix3& mat, float& x, float& y, float& z) {
        mat.ToEulerAnglesXYZ(x, y, z);
        // User requested inversion of the Y-axis (Roll) to fix the 30-degree rotation shift
        y = -y;
    }

}
