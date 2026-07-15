#include "ui/rml/RmlPanelHost.h"
#include "ui/rml/DragonBoardRmlUi.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUIItemEditPanel.h"
#include "vrui/VRUISettings.h"
#include "ui/pointer/PointerVisualController.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <Windows.h>

#include <d3d11.h>
#include <RE/R/Renderer.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr std::uint32_t kPanelWidth = 1920;
        constexpr std::uint32_t kPanelHeight = 1080;
        constexpr float kSceneScreenSizeScale = 0.85f;

        using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
        PresentFn g_originalPresent = nullptr;
        std::chrono::steady_clock::time_point g_lastPresent;
        bool g_lastPresentValid = false;

        HRESULT WINAPI RmlPresent(
            IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
        {
            const auto now = std::chrono::steady_clock::now();
            float dt = 1.0f / 90.0f;
            if (g_lastPresentValid) {
                dt = std::clamp(
                    std::chrono::duration<float>(now - g_lastPresent).count(),
                    1.0f / 240.0f,
                    0.1f);
            }
            g_lastPresent = now;
            g_lastPresentValid = true;

            try {
                RmlPanelHost::GetSingleton().RenderPresentThread(dt);
            } catch (const std::exception& e) {
                logger::error("DragonBoardVR: RmlUi Present exception: {}", e.what());
            }
            return g_originalPresent(swapChain, syncInterval, flags);
        }

    }

    RmlPanelHost& RmlPanelHost::GetSingleton()
    {
        static RmlPanelHost singleton;
        return singleton;
    }

    DragonBoardVR_API::PanelHandle RmlPanelHost::RegisterExternalPanel(
        const DragonBoardVR_API::PanelDescriptor& descriptor) noexcept
    {
        try {
            if (!descriptor.id || !*descriptor.id ||
                !descriptor.documentPath || !*descriptor.documentPath) {
                return DragonBoardVR_API::InvalidPanel;
            }

            std::scoped_lock lock(_externalMutex);
            for (const auto& [handle, client] : _externalPanels) {
                (void)handle;
                if (client.id == descriptor.id) {
                    logger::warn(
                        "DragonBoardVR API: RmlUi panel id '{}' is already registered.",
                        descriptor.id);
                    return DragonBoardVR_API::InvalidPanel;
                }
            }

            auto handle = _nextExternalPanel.fetch_add(1, std::memory_order_relaxed);
            if (handle == DragonBoardVR_API::InvalidPanel) {
                handle = _nextExternalPanel.fetch_add(1, std::memory_order_relaxed);
            }
            _externalPanels.emplace(handle, ExternalPanelClient{
                descriptor.id,
                descriptor.documentPath,
                descriptor.onEvent,
                descriptor.userData });
            _renderCommands.push_back(RenderCommand{
                RenderCommandType::kRegister,
                handle,
                descriptor.id,
                descriptor.documentPath });
            RequestRmlWarmup();
            return handle;
        } catch (const std::exception& e) {
            logger::error("DragonBoardVR API: failed to register RmlUi panel: {}", e.what());
            return DragonBoardVR_API::InvalidPanel;
        }
    }

    void RmlPanelHost::UnregisterExternalPanel(
        DragonBoardVR_API::PanelHandle panel) noexcept
    {
        try {
            {
                std::scoped_lock lock(_externalMutex);
                if (_externalPanels.erase(panel) == 0) return;
                _renderCommands.push_back(RenderCommand{
                    RenderCommandType::kUnregister, panel });
            }
            if (_activeExternalPanel.load() == panel) HideExternalPanel(panel);
        } catch (...) {
            logger::error("DragonBoardVR API: failed to unregister RmlUi panel {}.", panel);
        }
    }

    bool RmlPanelHost::ShowExternalPanel(
        DragonBoardVR_API::PanelHandle panel) noexcept
    {
        try {
            {
                std::scoped_lock lock(_externalMutex);
                if (!_externalPanels.contains(panel)) return false;
                _renderCommands.push_back(RenderCommand{ RenderCommandType::kShow, panel });
            }
            if (!EnsurePresentHookInstalled()) return false;
            auto& manager = vrui::VRMenuManager::get();
            if (!manager.isMenuOpen()) manager.toggleMenu();
            ResetPanelInput();
            _activeExternalPanel.store(panel);
            _localPanelMode.store(LocalPanelMode::kExternal);
            _visible.store(true);
            return true;
        } catch (...) {
            return false;
        }
    }

    void RmlPanelHost::HideExternalPanel(
        DragonBoardVR_API::PanelHandle panel) noexcept
    {
        if (_activeExternalPanel.load() == panel) {
            _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
            Close();
        }
    }

    bool RmlPanelHost::IsExternalPanelVisible(
        DragonBoardVR_API::PanelHandle panel) const noexcept
    {
        return panel != DragonBoardVR_API::InvalidPanel && _visible.load() &&
            _localPanelMode.load() == LocalPanelMode::kExternal &&
            _activeExternalPanel.load() == panel;
    }

    bool RmlPanelHost::SetExternalElementText(
        DragonBoardVR_API::PanelHandle panel,
        const char* elementId,
        const char* text) noexcept
    {
        return QueueElementCommand(
            RenderCommandType::kSetText, panel, elementId, text ? text : "");
    }

    bool RmlPanelHost::SetExternalElementAttribute(
        DragonBoardVR_API::PanelHandle panel,
        const char* elementId,
        const char* name,
        const char* value) noexcept
    {
        return QueueElementCommand(
            RenderCommandType::kSetAttribute,
            panel,
            elementId,
            name,
            value,
            value != nullptr);
    }

    bool RmlPanelHost::SetExternalElementClass(
        DragonBoardVR_API::PanelHandle panel,
        const char* elementId,
        const char* className,
        bool enabled) noexcept
    {
        return QueueElementCommand(
            RenderCommandType::kSetClass, panel, elementId, className, nullptr, enabled);
    }

    bool RmlPanelHost::QueueElementCommand(
        RenderCommandType type,
        DragonBoardVR_API::PanelHandle panel,
        const char* first,
        const char* second,
        const char* third,
        bool enabled) noexcept
    {
        try {
            if (!first || !*first || !second) return false;
            if (type != RenderCommandType::kSetText && !*second) return false;
            std::scoped_lock lock(_externalMutex);
            if (!_externalPanels.contains(panel)) return false;
            _renderCommands.push_back(RenderCommand{
                type,
                panel,
                first,
                second,
                third ? third : "",
                enabled });
            return true;
        } catch (...) {
            return false;
        }
    }

    RmlPanelHost::~RmlPanelHost()
    {
        // Restore the engine-owned placeholder before releasing the bridge.
        if (_screenSourceTexture && _sceneTextureBridge) {
            _screenSourceTexture->rendererTexture = _originalRendererTexture;
        }
        _sceneTextureBridge.reset();
        _rmlUi.reset();
        if (_panelShaderResource) _panelShaderResource->Release();
        if (_panelRenderTarget) _panelRenderTarget->Release();
        if (_panelRenderTexture) _panelRenderTexture->Release();
        if (_context) _context->Release();
        if (_device) _device->Release();
    }

    bool RmlPanelHost::OpenSettings()
    {
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: RmlUi Settings render hook unavailable.");
            return false;
        }
        if ((_rmlWarmupAttempted.load() && !_rendererReady.load()) ||
            (_rendererReady.load() && (!_rmlUi || !_rmlUi->IsSettingsReady()))) {
            logger::error("DragonBoardVR: RmlUi Settings document is unavailable.");
            return false;
        }

        CaptureSettingsGameThread();
        ResetPanelInput();
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        _localPanelMode.store(LocalPanelMode::kSettings);
        _rmlSettingsSyncPending.store(true);
        _visible.store(true);
        return true;
    }

    bool RmlPanelHost::OpenDeveloper()
    {
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: RmlUi Developer render hook unavailable.");
            return false;
        }
        if ((_rmlWarmupAttempted.load() && !_rendererReady.load()) ||
            (_rendererReady.load() && (!_rmlUi || !_rmlUi->IsDeveloperReady()))) {
            logger::error("DragonBoardVR: RmlUi Developer document is unavailable.");
            return false;
        }

        LoadDevCommandsGameThread();
        CaptureDevGameInfoGameThread();
        ResetPanelInput();
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        _devInfoRefreshAccumulator = 0.0f;
        _localPanelMode.store(LocalPanelMode::kDeveloper);
        _rmlDeveloperSyncPending.store(true);
        _visible.store(true);
        return true;
    }

    bool RmlPanelHost::OpenItemEdit(vrui::VRUIItemEditPanel* editor)
    {
        if (!editor) return false;
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: RmlUi Item Editor render hook unavailable.");
            return false;
        }
        if ((_rmlWarmupAttempted.load() && !_rendererReady.load()) ||
            (_rendererReady.load() && (!_rmlUi || !_rmlUi->IsItemEditReady()))) {
            logger::error("DragonBoardVR: RmlUi Item Editor document is unavailable.");
            return false;
        }

        const auto state = editor->getEditState();
        ResetPanelInput();
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        {
            std::scoped_lock lock(_itemEditMutex);
            _itemEditBackend = editor;
            _itemEditDraft.category = state.category;
            _itemEditDraft.itemName = state.itemName;
            _itemEditDraft.modelPath = state.modelPath;
            _itemEditDraft.sourcePanel = state.sourcePanel;
            _itemEditDraft.formID = state.formID;
            _itemEditDraft.posX = state.posX;
            _itemEditDraft.posY = state.posY;
            _itemEditDraft.posZ = state.posZ;
            _itemEditDraft.rotX = state.rotX;
            _itemEditDraft.rotY = state.rotY;
            _itemEditDraft.rotZ = state.rotZ;
            _itemEditDraft.scale = state.scale;
            _itemEditDraft.magicItem = state.magicItem;
            _itemEditDraft.boardPinnedToWorld = state.boardPinnedToWorld;
            _itemEditDraft.labelHidden = state.labelHidden;
            _itemEditDraft.canPinToWorld = state.canPinToWorld;
        }
        _localPanelMode.store(LocalPanelMode::kItemEdit);
        _rmlItemEditSyncPending.store(true);
        _itemEditApplyPending.store(false);
        _itemEditActionPending.store(ItemEditAction::kNone);
        _visible.store(true);
        logger::info("DragonBoardVR: opened RmlUi item editor for {:08X} '{}'.", state.formID, state.itemName);
        return true;
    }

    void RmlPanelHost::RequestRmlWarmup()
    {
        if (_rendererReady.load(std::memory_order_acquire) ||
            _rmlWarmupAttempted.load(std::memory_order_acquire)) {
            return;
        }

        _rmlWarmupRequested.store(true, std::memory_order_release);
        if (EnsurePresentHookInstalled()) {
            logger::info(
                "DragonBoardVR: RmlUi warm-up armed for the next valid Present; no scene NIF will be attached.");
        } else {
            logger::info(
                "DragonBoardVR: RmlUi warm-up deferred because the Skyrim swap chain is not ready yet.");
        }
    }

    bool RmlPanelHost::IsDeveloperOpen() const
    {
        return _visible.load() && _localPanelMode.load() == LocalPanelMode::kDeveloper;
    }

    void RmlPanelHost::Close()
    {
        ResetPanelInput();
        _visible.store(false);
    }

    void RmlPanelHost::OnDominantVrButtonEvent(
        bool triggerButton,
        bool gripButton,
        bool pressed)
    {
        if (!_visible.load(std::memory_order_acquire)) return;

        // This state belongs only to the flat RmlUi panel. The global
        // DragonBoard input state is deliberately left untouched so activation
        // chords such as Grip + Y continue to receive the original VR events.
        if (triggerButton) {
            const bool previous = _triggerDown.exchange(
                pressed, std::memory_order_acq_rel);
            if (previous != pressed) {
                logger::info(
                    "DragonBoardVR: local panel dominant trigger {} (raw grip match={}).",
                    pressed ? "down" : "up",
                    gripButton);
            }
        } else if (gripButton) {
            const bool previous = _gripDown.exchange(
                pressed, std::memory_order_acq_rel);
            if (previous != pressed) {
                logger::info(
                    "DragonBoardVR: local panel dominant grip {}.",
                    pressed ? "down" : "up");
            }
        }
    }

    void RmlPanelHost::ResetPanelInput()
    {
        _triggerDown.store(false, std::memory_order_release);
        _gripDown.store(false, std::memory_order_release);
        _stickX.store(0.0f, std::memory_order_release);
        _stickY.store(0.0f, std::memory_order_release);
    }

    void RmlPanelHost::ApplyRenderCommandsPresentThread()
    {
        if (!_rmlUi) return;
        std::deque<RenderCommand> commands;
        {
            std::scoped_lock lock(_externalMutex);
            commands.swap(_renderCommands);
        }

        for (auto& command : commands) {
            bool succeeded = false;
            switch (command.type) {
            case RenderCommandType::kRegister:
                succeeded = _rmlUi->RegisterPanel(
                    command.panel, std::move(command.first), std::move(command.second));
                break;
            case RenderCommandType::kUnregister:
                succeeded = _rmlUi->UnregisterPanel(command.panel);
                break;
            case RenderCommandType::kShow:
                succeeded = _rmlUi->ShowPanel(command.panel);
                break;
            case RenderCommandType::kSetText:
                succeeded = _rmlUi->SetElementText(
                    command.panel, command.first.c_str(), command.second.c_str());
                break;
            case RenderCommandType::kSetAttribute:
                succeeded = _rmlUi->SetElementAttribute(
                    command.panel,
                    command.first.c_str(),
                    command.second.c_str(),
                    command.enabled ? command.third.c_str() : nullptr);
                break;
            case RenderCommandType::kSetClass:
                succeeded = _rmlUi->SetElementClass(
                    command.panel,
                    command.first.c_str(),
                    command.second.c_str(),
                    command.enabled);
                break;
            }
            if (!succeeded) {
                logger::warn(
                    "DragonBoardVR: RmlUi render command {} failed for external panel {}.",
                    static_cast<unsigned>(command.type),
                    command.panel);
            }
        }
    }

    void RmlPanelHost::CollectExternalEventsPresentThread()
    {
        if (!_rmlUi) return;
        std::deque<ExternalEvent> events;
        while (auto event = _rmlUi->ConsumePanelEvent()) {
            events.push_back(ExternalEvent{
                event->panel,
                event->type == DragonBoardRmlUi::PanelEventType::kChange ?
                    DragonBoardVR_API::PanelEventType::Change :
                    DragonBoardVR_API::PanelEventType::Click,
                std::move(event->elementId),
                std::move(event->value),
                event->numericValue });
        }
        if (events.empty()) return;
        std::scoped_lock lock(_externalMutex);
        while (!events.empty()) {
            _externalEvents.push_back(std::move(events.front()));
            events.pop_front();
        }
    }

    void RmlPanelHost::DispatchExternalEventsGameThread()
    {
        std::deque<ExternalEvent> events;
        {
            std::scoped_lock lock(_externalMutex);
            events.swap(_externalEvents);
        }

        while (!events.empty()) {
            auto event = std::move(events.front());
            events.pop_front();
            DragonBoardVR_API::PanelEventCallback callback = nullptr;
            void* userData = nullptr;
            {
                std::scoped_lock lock(_externalMutex);
                const auto it = _externalPanels.find(event.panel);
                if (it != _externalPanels.end()) {
                    callback = it->second.callback;
                    userData = it->second.userData;
                }
            }
            if (!callback) continue;

            const DragonBoardVR_API::PanelEvent apiEvent{
                event.panel,
                event.type,
                event.elementId.c_str(),
                event.value.c_str(),
                event.numericValue };
            try {
                callback(&apiEvent, userData);
            } catch (...) {
                logger::error(
                    "DragonBoardVR API: external RmlUi callback threw for panel {}.",
                    event.panel);
            }
        }
    }

    void RmlPanelHost::UpdateGameThread(float deltaTime)
    {
        auto& manager = vrui::VRMenuManager::get();

        UpdateSurfaceGameThread();
        DispatchExternalEventsGameThread();
        const auto hapticCue = static_cast<dragonboard::ui::rml::DragonBoardRmlUi::HapticCue>(
            _pendingRmlHapticCue.exchange(0, std::memory_order_acq_rel));
        if (hapticCue != dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kNone) {
            const auto& settings = vrui::VRUISettings::get();
            float intensity = settings.hapticIntensity;
            float duration = settings.hapticDuration;
            bool enabled = settings.hapticOnPress;
            switch (hapticCue) {
            case dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kHover:
                enabled = settings.hapticOnHover;
                intensity *= 0.45f;
                duration *= 0.65f;
                break;
            case dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kSliderTick:
                intensity *= 0.35f;
                duration *= 0.55f;
                break;
            case dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kStrong:
                intensity = std::min(1.0f, intensity * 1.8f);
                duration *= 1.5f;
                break;
            case dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError:
                intensity = std::min(1.0f, intensity * 1.35f);
                duration *= 1.25f;
                break;
            case dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kPress:
            case dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kNone:
                break;
            }
            if (enabled) {
                manager.triggerHaptic(true, intensity, duration);
            }
        }
        if (_visible.load()) {
            float stickX = 0.0f;
            float stickY = 0.0f;
            manager.getDominantThumbstick(stickX, stickY);
            _stickX.store(stickX);
            _stickY.store(stickY);

            if (_localPanelMode.load() == LocalPanelMode::kDeveloper) {
                _devInfoRefreshAccumulator += std::clamp(deltaTime, 0.0f, 0.5f);
                if (_devInfoRefreshAccumulator >= 0.5f) {
                    _devInfoRefreshAccumulator = 0.0f;
                    CaptureDevGameInfoGameThread();
                }
            }
        }
        dragonboard::ui::pointer::PointerVisualController::SetReticleSuppressed(
            manager,
            _scenePanelVisible && _pointerInHostedPanel && manager.isMenuOpen());

        if (_applyPending.exchange(false)) {
            ApplyDraftGameThread();
            vrui::VRMenuManager::get().refreshActivePanels();
        }

        if (_savePending.exchange(false)) {
            ApplyDraftGameThread();
            vrui::VRMenuManager::get().saveSettingsNow();
        }

        if (_itemEditApplyPending.exchange(false)) {
            ApplyItemEditDraftGameThread();
        }
        if (const auto action = _itemEditActionPending.exchange(ItemEditAction::kNone);
            action != ItemEditAction::kNone) {
            ExecuteItemEditActionGameThread(action);
        }

        if (_devCommandPending.exchange(false)) {
            std::string command;
            std::string label;
            bool dangerous = false;
            {
                std::scoped_lock lock(_devMutex);
                command = std::move(_pendingDevCommand);
                label = std::move(_pendingDevCommandLabel);
                dangerous = _pendingDevCommandDangerous;
            }
            if (!command.empty()) {
                if (dangerous) {
                    manager.closeMenu();
                }
                manager.executeConsoleCommand(
                    command,
                    dangerous,
                    dangerous ? "DragonBoardVR: [DEV][DEFERRED] Executed:" :
                                "DragonBoardVR: [DEV] Executed action:");
                logger::info("DragonBoardVR: Dev command requested: '{}' ({})", label, command);
            }
        }

    }

    void RmlPanelHost::UpdateSurfaceGameThread()
    {
        auto& manager = vrui::VRMenuManager::get();
        const bool hostActive = manager.isMenuOpen() && _visible.load();
        if (!hostActive) {
            if (_screenNode) {
                _screenNode->SetAppCulled(true);
            }
            _scenePanelVisible = false;
            _pointerInHostedPanel = false;
            if (_visible.load() && !manager.isMenuOpen()) {
                Close();
            }
            return;
        }

        // Follow the panel that owns the visible DragonBoard mesh.  The
        // persistent panel is only the fixed-button layer and can have a
        // different animation/smoothing state while the player is moving.
        auto surfacePanel = manager.findPanelByName("Background_Panel");
        if (!surfacePanel || !surfacePanel->getNode()) {
            surfacePanel = manager.findPanelByName("Persistent_Panel");
        }
        if (!surfacePanel || !surfacePanel->getNode()) {
            surfacePanel = manager.findPanelByName("MainPanel");
        }
        if (!surfacePanel || !surfacePanel->getNode()) {
            return;
        }

        // Pose comes from the visible board, but its panel intentionally has
        // no UI children and therefore reports a 1x1 layout.  Keep the
        // physical surface dimensions tied to the fixed-button layout that
        // defines the board's usable area.
        auto layoutPanel = manager.findPanelByName("Persistent_Panel");
        if (!layoutPanel) {
            layoutPanel = surfacePanel;
        }

        auto* surfaceNode = surfacePanel->getNode();
        RE::NiPoint3 worldPosition = surfaceNode->world.translate;
        RE::NiMatrix3 worldRotation = surfaceNode->world.rotate;
        float worldScale = surfaceNode->world.scale;

        // The hand-held DragonBoard is attached directly to the MagicNode.
        // Rebuild its world pose from that authoritative parent and the panel's
        // local transform instead of trusting a possibly stale cached `world`
        // value from the intermediate panel node.
        auto* menuHandNode = manager.getMenuHandNode();
        const bool magicNodeAnchored = !manager.isBoardWorldPinned() && menuHandNode &&
            surfaceNode->parent == menuHandNode;
        if (magicNodeAnchored) {
            const auto& anchor = menuHandNode->world;
            const auto& local = surfaceNode->local;
            const RE::NiPoint3 scaledLocal(
                local.translate.x * anchor.scale,
                local.translate.y * anchor.scale,
                local.translate.z * anchor.scale);
            const RE::NiPoint3 rotatedLocal(
                anchor.rotate.entry[0][0] * scaledLocal.x + anchor.rotate.entry[0][1] * scaledLocal.y + anchor.rotate.entry[0][2] * scaledLocal.z,
                anchor.rotate.entry[1][0] * scaledLocal.x + anchor.rotate.entry[1][1] * scaledLocal.y + anchor.rotate.entry[1][2] * scaledLocal.z,
                anchor.rotate.entry[2][0] * scaledLocal.x + anchor.rotate.entry[2][1] * scaledLocal.y + anchor.rotate.entry[2][2] * scaledLocal.z);
            worldPosition = anchor.translate + rotatedLocal;
            worldRotation = anchor.rotate * local.rotate;
            worldScale = anchor.scale * local.scale;
        }

        const float effectiveScreenScale = kSceneScreenSizeScale;
        const float width = std::max(
            1.0f,
            layoutPanel->getWidth() * worldScale * effectiveScreenScale);
        const float height = std::max(
            1.0f,
            layoutPanel->getHeight() * worldScale * effectiveScreenScale);

        // The board mesh's front-face correction is a local 180-degree turn.
        const RE::NiPoint3 right(
            -worldRotation.entry[0][0],
            -worldRotation.entry[1][0],
            -worldRotation.entry[2][0]);
        const RE::NiPoint3 up(
            worldRotation.entry[0][2],
            worldRotation.entry[1][2],
            worldRotation.entry[2][2]);

        const RE::NiPoint3 rayOrigin = manager.getLaserOrigin();
        const RE::NiPoint3 rayDirection = manager.getLaserDirection();
        bool pointerInPanel = false;
        const RE::NiPoint3 normal(
            right.y * up.z - right.z * up.y,
            right.z * up.x - right.x * up.z,
            right.x * up.y - right.y * up.x);
        const float denominator =
            rayDirection.x * normal.x + rayDirection.y * normal.y + rayDirection.z * normal.z;
        if (std::abs(denominator) > 1e-5f) {
            const RE::NiPoint3 toSurface(
                worldPosition.x - rayOrigin.x,
                worldPosition.y - rayOrigin.y,
                worldPosition.z - rayOrigin.z);
            const float distance =
                (toSurface.x * normal.x + toSurface.y * normal.y + toSurface.z * normal.z) /
                denominator;
            if (std::isfinite(distance) && distance > 0.0f) {
                const RE::NiPoint3 hit(
                    rayOrigin.x + rayDirection.x * distance - worldPosition.x,
                    rayOrigin.y + rayDirection.y * distance - worldPosition.y,
                    rayOrigin.z + rayDirection.z * distance - worldPosition.z);
                const float localX = (hit.x * right.x + hit.y * right.y + hit.z * right.z) / width;
                const float localY = (hit.x * up.x + hit.y * up.y + hit.z * up.z) / height;
                const float u = localX + 0.5f;
                const float v = 0.5f - localY;
                if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                    pointerInPanel = true;
                    _pointerU = u;
                    _pointerV = v;
                }
            }
        }
        _pointerInHostedPanel = pointerInPanel;
        _scenePanelVisible = UpdateScenePanelGameThread(surfacePanel->getBackgroundNode());
    }

    bool RmlPanelHost::UpdateScenePanelGameThread(RE::NiNode* backgroundNode)
    {
        if (!backgroundNode) {
            return false;
        }

        // Texture creation and rendering happen on Present. The game thread
        // only publishes the already-created texture to the scene material.
        if (!_visible.load() || !_rendererReady.load() ||
            !_panelRenderTexture || !_panelShaderResource) {
            if (_screenNode) {
                _screenNode->SetAppCulled(true);
            }
            return false;
        }

        if (!_screenNode) {
            _screenNode = vrui::VRUIWidget::loadModelFromNif("DragonBoardVR\\ImGuiScreen.nif", false);
            if (!_screenNode) {
                return false;
            }

            _screenNode->name = "DragonBoardVR_RmlScreen";
            // IconPlane2's geometry is a 170.666-unit XY square. Fit a 16:9
            // screen inside the Tablet.nif face and lift it just above the
            // parchment to avoid z-fighting.
            _screenNode->local.translate = { 0.0f, 0.0f, 0.72f };
            _screenNode->local.rotate = RE::NiMatrix3{};
            _screenNode->local.scale = 1.0f;

            RE::BSVisit::TraverseScenegraphGeometries(
                _screenNode.get(),
                [&](RE::BSGeometry* geometry) -> RE::BSVisit::BSVisitControl {
                    if (_screenSourceTexture || !geometry) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }
                    auto* property = geometry->lightingShaderProp_cast();
                    auto* baseMaterial = property ? property->GetBaseMaterial() : nullptr;
                    auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(baseMaterial);
                    if (!material || !material->diffuseTexture) {
                        return RE::BSVisit::BSVisitControl::kContinue;
                    }

                    _screenSourceTexture = material->diffuseTexture;
                    _originalRendererTexture = _screenSourceTexture->rendererTexture;

                    // The legacy-named screen NIF is a dedicated SSE quad with a complete
                    // 0..1 UV range. Keep the runtime material transform at
                    // identity so both triangles sample the same panel texture.
                    material->texCoordScale[0] = { 1.0f, 1.0f };
                    material->texCoordOffset[0] = { 0.0f, 0.0f };
                    material->texCoordScale[1] = material->texCoordScale[0];
                    material->texCoordOffset[1] = material->texCoordOffset[0];
                    return RE::BSVisit::BSVisitControl::kStop;
                });

            if (!_screenSourceTexture || !_originalRendererTexture) {
                logger::warn("DragonBoardVR: ImGuiScreen.nif has no usable dedicated diffuse texture.");
                _screenNode = nullptr;
                _screenSourceTexture = nullptr;
                return false;
            }
        }

        constexpr float kPlaneExtent = 170.666656f;
        const float screenWidth = 18.0f * kSceneScreenSizeScale;
        const float screenHeight = screenWidth * 9.0f / 16.0f;
        _screenNode->local.rotate.entry[0][0] = screenWidth / kPlaneExtent;
        _screenNode->local.rotate.entry[1][1] = screenHeight / kPlaneExtent;

        if (!_sceneTextureBridge) {
            if (_screenSourceTexture && _sceneTextureBridge) {
                _screenSourceTexture->rendererTexture = _originalRendererTexture;
            }
            _sceneTextureBridge.reset();

            _sceneTextureBridge = std::make_unique<RE::BSGraphics::Texture>();
            _sceneTextureBridge->texture = _panelRenderTexture;
            _sceneTextureBridge->unk08 = 0;
            _sceneTextureBridge->resourceView = _panelShaderResource;
            _screenSourceTexture->rendererTexture = _sceneTextureBridge.get();

            logger::info(
                "DragonBoardVR: RmlUi texture bound to scene screen ({}x{}).",
                kPanelWidth,
                kPanelHeight);
        }

        if (_screenNode->parent != backgroundNode) {
            if (_screenNode->parent) {
                _screenNode->parent->DetachChild(_screenNode.get());
            }
            backgroundNode->AttachChild(_screenNode.get());
            logger::info(
                "DragonBoardVR: RmlUi scene screen attached to tablet node '{}'.",
                backgroundNode->name.c_str());
        }

        _screenNode->SetAppCulled(false);
        RE::NiUpdateData updateData;
        _screenNode->Update(updateData);
        _screenNode->UpdateWorldBound();
        return _sceneTextureBridge != nullptr;
    }

    bool RmlPanelHost::EnsurePresentHookInstalled()
    {
        if (g_originalPresent) return true;

        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) return false;
        auto& rendererData = renderer->GetRuntimeData();
        auto* swapChain = reinterpret_cast<IDXGISwapChain*>(rendererData.renderWindows[0].swapChain);
        if (!swapChain) return false;

        auto** vtable = *reinterpret_cast<void***>(swapChain);
        if (!vtable || !vtable[8]) return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
            return false;
        }
        g_originalPresent = reinterpret_cast<PresentFn>(vtable[8]);
        vtable[8] = reinterpret_cast<void*>(&RmlPresent);
        DWORD ignored = 0;
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), &vtable[8], sizeof(void*));

        logger::info(
            "DragonBoardVR: RmlUi Present hook installed (next={}).",
            reinterpret_cast<void*>(g_originalPresent));
        return true;
    }

    void RmlPanelHost::RenderPresentThread(float deltaTime)
    {
        if (_rmlWarmupRequested.exchange(false, std::memory_order_acq_rel) &&
            !_rendererReady.load(std::memory_order_acquire)) {
            _rmlWarmupAttempted.store(true, std::memory_order_release);
            const auto started = std::chrono::steady_clock::now();
            const bool initialized = InitializeRenderer();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (initialized) {
                logger::info(
                    "DragonBoardVR: RmlUi warm-up completed in {} ms before a local panel was opened.",
                    elapsedMs);
            } else {
                logger::warn(
                    "DragonBoardVR: RmlUi warm-up failed after {} ms.",
                    elapsedMs);
            }
        }
        if (_rendererReady.load(std::memory_order_acquire) && !_visible.load()) {
            ApplyRenderCommandsPresentThread();
        }
        if (_visible.load()) {
            RenderPanel(deltaTime);
        }
    }

    bool RmlPanelHost::InitializeRenderer()
    {
        if (_rendererReady.load()) return true;

        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) return false;
        auto& rendererData = renderer->GetRuntimeData();
        auto* device = reinterpret_cast<ID3D11Device*>(rendererData.forwarder);
        auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData.context);
        if (!device || !context) return false;

        device->AddRef();
        context->AddRef();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kPanelWidth;
        desc.Height = kPanelHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) ||
            FAILED(device->CreateRenderTargetView(texture, nullptr, &rtv)) ||
            FAILED(device->CreateShaderResourceView(texture, nullptr, &srv))) {
            logger::error("DragonBoardVR: failed to create the 1920x1080 RmlUi render texture.");
            if (srv) srv->Release();
            if (rtv) rtv->Release();
            if (texture) texture->Release();
            context->Release();
            device->Release();
            return false;
        }

        _rmlUi = std::make_unique<dragonboard::ui::rml::DragonBoardRmlUi>();
        if (!_rmlUi->Initialize(device, context)) {
            logger::error("DragonBoardVR: RmlUi runtime initialization failed.");
            _rmlUi.reset();
            srv->Release();
            rtv->Release();
            texture->Release();
            context->Release();
            device->Release();
            return false;
        }

        _device = device;
        _context = context;
        _panelRenderTexture = texture;
        _panelRenderTarget = rtv;
        _panelShaderResource = srv;
        _rendererReady.store(true);
        logger::info("DragonBoardVR: RmlUi panel host initialized at 1920x1080.");
        return true;
    }

    void RmlPanelHost::RenderPanel(float deltaTime)
    {
        if (!_visible.load() || !InitializeRenderer()) return;
        ApplyRenderCommandsPresentThread();

        const auto panelMode = _localPanelMode.load();
        const bool settingsRmlActive = panelMode == LocalPanelMode::kSettings &&
            _rmlUi && _rmlUi->IsSettingsReady();
        const bool developerRmlActive = panelMode == LocalPanelMode::kDeveloper &&
            _rmlUi && _rmlUi->IsDeveloperReady();
        const bool itemEditRmlActive = panelMode == LocalPanelMode::kItemEdit &&
            _rmlUi && _rmlUi->IsItemEditReady();
        const auto externalPanel = _activeExternalPanel.load();
        const bool externalRmlActive = panelMode == LocalPanelMode::kExternal &&
            _rmlUi && _rmlUi->IsPanelReady(externalPanel);
        if (settingsRmlActive || developerRmlActive || itemEditRmlActive || externalRmlActive) {
            if (settingsRmlActive) {
                _rmlUi->ShowSettings();
                if (_rmlSettingsSyncPending.exchange(false)) SyncRmlSettingsFromDraft();
            } else if (developerRmlActive) {
                _rmlUi->ShowDeveloper();
                if (_rmlDeveloperSyncPending.exchange(false)) SyncRmlDeveloperCommands();
                SyncRmlDeveloperInfo();
            } else if (itemEditRmlActive) {
                _rmlUi->ShowItemEdit();
                if (_rmlItemEditSyncPending.exchange(false)) SyncRmlItemEdit();
            } else {
                _rmlUi->ShowPanel(externalPanel);
            }
            _rmlUi->ProcessInput(
                _pointerInHostedPanel.load(),
                _pointerU.load(),
                _pointerV.load(),
                _triggerDown.load(),
                _gripDown.load(),
                _stickX.load(),
                _stickY.load(),
                static_cast<int>(kPanelWidth),
                static_cast<int>(kPanelHeight));

            const auto hapticCue = _rmlUi->ConsumeHapticCue();
            const auto requested = static_cast<std::uint8_t>(hapticCue);
            auto pending = _pendingRmlHapticCue.load(std::memory_order_relaxed);
            while (requested > pending &&
                   !_pendingRmlHapticCue.compare_exchange_weak(
                       pending, requested,
                       std::memory_order_release,
                       std::memory_order_relaxed)) {
            }

            if (settingsRmlActive) {
                if (auto change = _rmlUi->ConsumeSliderChange()) {
                    ApplyRmlSliderChange(change->id, change->value);
                }
                if (_rmlUi->ConsumeEditModeToggleRequested()) {
                    bool enabled = false;
                    {
                        std::scoped_lock lock(_draftMutex);
                        _draft.editModeEnabled = !_draft.editModeEnabled;
                        enabled = _draft.editModeEnabled;
                    }
                    _rmlUi->SetEditModeEnabled(enabled);
                    _applyPending.store(true);
                    logger::info(
                        "DragonBoardVR: item edit mode {} from RmlUi settings.",
                        enabled ? "enabled" : "disabled");
                }
                if (_rmlUi->ConsumeDeveloperPanelToggleRequested()) {
                    bool enabled = false;
                    {
                        std::scoped_lock lock(_draftMutex);
                        _draft.showDevButton = !_draft.showDevButton;
                        enabled = _draft.showDevButton;
                    }
                    _rmlUi->SetDeveloperButtonEnabled(enabled);
                    _applyPending.store(true);
                }

                const bool triggerDown = _triggerDown.load();
                if (!triggerDown && _rmlPreviousTriggerDown && _deferredRmlTransformApply) {
                    _deferredRmlTransformApply = false;
                    _applyPending.store(true);
                    logger::info(
                        "DragonBoardVR: committed deferred panel transform after slider release.");
                }
                _rmlPreviousTriggerDown = triggerDown;
            } else if (developerRmlActive) {
                if (auto commandIndex = _rmlUi->ConsumeDeveloperCommandRequested()) {
                std::optional<DevCommandEntry> command;
                {
                    std::scoped_lock lock(_devMutex);
                    if (*commandIndex < _devCommands.size()) command = _devCommands[*commandIndex];
                }
                if (command) QueueDevCommand(*command);
                }
            } else if (itemEditRmlActive) {
                if (auto change = _rmlUi->ConsumeSliderChange()) {
                    ApplyRmlItemEditSliderChange(change->id, change->value);
                }
                const auto rmlAction = _rmlUi->ConsumeItemEditAction();
                using RmlItemAction = dragonboard::ui::rml::DragonBoardRmlUi::ItemEditAction;
                switch (rmlAction) {
                case RmlItemAction::kApplyItem: _itemEditActionPending.store(ItemEditAction::kApplyItem); break;
                case RmlItemAction::kApplyCategory: _itemEditActionPending.store(ItemEditAction::kApplyCategory); break;
                case RmlItemAction::kReset: _itemEditActionPending.store(ItemEditAction::kReset); break;
                case RmlItemAction::kBack: _itemEditActionPending.store(ItemEditAction::kBack); break;
                case RmlItemAction::kPinDashboard: _itemEditActionPending.store(ItemEditAction::kPinDashboard); break;
                case RmlItemAction::kPinLeftHand: _itemEditActionPending.store(ItemEditAction::kPinLeftHand); break;
                case RmlItemAction::kPinWorld: _itemEditActionPending.store(ItemEditAction::kPinWorld); break;
                case RmlItemAction::kToggleLabel: _itemEditActionPending.store(ItemEditAction::kToggleLabel); break;
                case RmlItemAction::kNone: break;
                }
            } else {
                CollectExternalEventsPresentThread();
            }

            const bool rendered = _rmlUi->Render(
                _panelRenderTarget,
                static_cast<int>(kPanelWidth),
                static_cast<int>(kPanelHeight));
            if (_rmlUi->ConsumeCloseRequested()) {
                Close();
            }
            if (settingsRmlActive && _rmlUi->ConsumeSaveRequested()) {
                _savePending.store(true);
            }
            if (rendered) {
                _presentFrameMs = std::clamp(deltaTime, 1.0f / 240.0f, 0.1f) * 1000.0f;
                _presentFps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
                _panelDrawCalls = _rmlUi->GetLastDrawCallCount();
                return;
            }

            logger::error("DragonBoardVR: RmlUi frame failed; closing the active panel.");
            Close();
            return;
        }
        logger::error("DragonBoardVR: requested RmlUi document is unavailable.");
        Close();
    }

    void RmlPanelHost::SyncRmlSettingsFromDraft()
    {
        if (!_rmlUi || !_rmlUi->IsSettingsReady()) return;
        std::scoped_lock lock(_draftMutex);
        _rmlUi->SetSliderValue("menuScale", _draft.menuScale);
        _rmlUi->SetSliderValue("buttonSpacingX", _draft.buttonSpacingX);
        _rmlUi->SetSliderValue("buttonSpacingY", _draft.buttonSpacingY);
        _rmlUi->SetSliderValue("menuOffsetX", _draft.menuOffsetX);
        _rmlUi->SetSliderValue("menuOffsetY", _draft.menuOffsetY);
        _rmlUi->SetSliderValue("menuOffsetZ", _draft.menuOffsetZ);
        _rmlUi->SetSliderValue("menuRotX", _draft.menuRotX);
        _rmlUi->SetSliderValue("menuRotY", _draft.menuRotY);
        _rmlUi->SetSliderValue("menuRotZ", _draft.menuRotZ);
        _rmlUi->SetSliderValue("buttonMeshScale", _draft.buttonMeshScale);
        _rmlUi->SetSliderValue("itemMeshScale", _draft.itemMeshScale);
        _rmlUi->SetSliderValue("containerGridOffsetZ", _draft.containerGridOffsetZ);
        _rmlUi->SetSliderValue("reticleScale", _draft.reticleScale);
        _rmlUi->SetSliderValue("itemWeaponScale", _draft.itemWeaponScale);
        _rmlUi->SetSliderValue("itemArmorScale", _draft.itemArmorScale);
        _rmlUi->SetSliderValue("itemPotionScale", _draft.itemPotionScale);
        _rmlUi->SetSliderValue("itemFoodScale", _draft.itemFoodScale);
        _rmlUi->SetSliderValue("itemMiscScale", _draft.itemMiscScale);
        _rmlUi->SetSliderValue("labelScale", _draft.labelScale);
        _rmlUi->SetSliderValue("labelSpacing", _draft.labelSpacing);
        _rmlUi->SetSliderValue("labelXOffset", _draft.labelXOffset);
        _rmlUi->SetSliderValue("labelYOffset", _draft.labelYOffset);
        _rmlUi->SetSliderValue("labelZOffset", _draft.labelZOffset);
        _rmlUi->SetEditModeEnabled(_draft.editModeEnabled);
        _rmlUi->SetDeveloperButtonEnabled(_draft.showDevButton);
    }

    void RmlPanelHost::ApplyRmlSliderChange(std::string_view id, float value)
    {
        bool changed = true;
        const bool movesHostedPanel =
            id == "menuScale" ||
            id.starts_with("menuOffset") ||
            id.starts_with("menuRot");
        {
            std::scoped_lock lock(_draftMutex);
            if (id == "menuScale") _draft.menuScale = value;
            else if (id == "buttonSpacingX") _draft.buttonSpacingX = value;
            else if (id == "buttonSpacingY") _draft.buttonSpacingY = value;
            else if (id == "menuOffsetX") _draft.menuOffsetX = value;
            else if (id == "menuOffsetY") _draft.menuOffsetY = value;
            else if (id == "menuOffsetZ") _draft.menuOffsetZ = value;
            else if (id == "menuRotX") _draft.menuRotX = value;
            else if (id == "menuRotY") _draft.menuRotY = value;
            else if (id == "menuRotZ") _draft.menuRotZ = value;
            else if (id == "buttonMeshScale") _draft.buttonMeshScale = value;
            else if (id == "itemMeshScale") _draft.itemMeshScale = value;
            else if (id == "containerGridOffsetZ") _draft.containerGridOffsetZ = value;
            else if (id == "reticleScale") _draft.reticleScale = value;
            else if (id == "itemWeaponScale") _draft.itemWeaponScale = value;
            else if (id == "itemArmorScale") _draft.itemArmorScale = value;
            else if (id == "itemPotionScale") _draft.itemPotionScale = value;
            else if (id == "itemFoodScale") _draft.itemFoodScale = value;
            else if (id == "itemMiscScale") _draft.itemMiscScale = value;
            else if (id == "labelScale") _draft.labelScale = value;
            else if (id == "labelSpacing") _draft.labelSpacing = value;
            else if (id == "labelXOffset") _draft.labelXOffset = value;
            else if (id == "labelYOffset") _draft.labelYOffset = value;
            else if (id == "labelZOffset") _draft.labelZOffset = value;
            else changed = false;
        }
        if (!changed) return;
        if (movesHostedPanel) {
            _deferredRmlTransformApply = true;
        } else {
            _applyPending.store(true);
        }
    }

    void RmlPanelHost::SyncRmlDeveloperCommands()
    {
        if (!_rmlUi || !_rmlUi->IsDeveloperReady()) return;
        std::vector<dragonboard::ui::rml::DragonBoardRmlUi::DeveloperCommand> commands;
        {
            std::scoped_lock lock(_devMutex);
            commands.reserve(_devCommands.size());
            for (const auto& command : _devCommands) {
                commands.push_back({
                    command.label, command.command, command.description, command.dangerous });
            }
        }
        _rmlUi->SetDeveloperCommands(std::move(commands));
    }

    void RmlPanelHost::SyncRmlDeveloperInfo()
    {
        if (!_rmlUi || !_rmlUi->IsDeveloperReady()) return;
        DevGameInfoSnapshot snapshot;
        {
            std::scoped_lock lock(_devMutex);
            snapshot = _devGameInfo;
        }

        dragonboard::ui::rml::DragonBoardRmlUi::DeveloperInfo info;
        info.fps = _presentFps;
        info.frameTimeMs = _presentFrameMs;
        info.panelDrawCalls = _panelDrawCalls;
        info.pluginVersion = Plugin::VERSION.string();
        info.d3dFeatureLevel = _device ? static_cast<std::uint32_t>(_device->GetFeatureLevel()) : 0;
        info.playerX = snapshot.playerX;
        info.playerY = snapshot.playerY;
        info.playerZ = snapshot.playerZ;
        info.cellName = std::move(snapshot.cellName);
        info.cellFormId = snapshot.cellFormId;
        info.worldspaceName = std::move(snapshot.worldspaceName);
        info.worldspaceFormId = snapshot.worldspaceFormId;
        _rmlUi->SetDeveloperInfo(info);
    }

    void RmlPanelHost::SyncRmlItemEdit()
    {
        if (!_rmlUi || !_rmlUi->IsItemEditReady()) return;
        ItemEditDraft draft;
        {
            std::scoped_lock lock(_itemEditMutex);
            draft = _itemEditDraft;
        }
        dragonboard::ui::rml::DragonBoardRmlUi::ItemEditInfo info;
        info.category = std::move(draft.category);
        info.itemName = std::move(draft.itemName);
        info.modelPath = std::move(draft.modelPath);
        info.formID = draft.formID;
        info.posX = draft.posX;
        info.posY = draft.posY;
        info.posZ = draft.posZ;
        info.rotX = draft.rotX;
        info.rotY = draft.rotY;
        info.rotZ = draft.rotZ;
        info.scale = draft.scale;
        info.magicItem = draft.magicItem;
        info.boardPinnedToWorld = draft.boardPinnedToWorld;
        info.labelHidden = draft.labelHidden;
        info.canPinToWorld = draft.canPinToWorld;
        _rmlUi->SetItemEditInfo(info);
    }

    void RmlPanelHost::ApplyRmlItemEditSliderChange(std::string_view id, float value)
    {
        bool changed = true;
        {
            std::scoped_lock lock(_itemEditMutex);
            if (id == "editPosX") _itemEditDraft.posX = value;
            else if (id == "editPosY") _itemEditDraft.posY = value;
            else if (id == "editPosZ") _itemEditDraft.posZ = value;
            else if (id == "editRotX") _itemEditDraft.rotX = value;
            else if (id == "editRotY") _itemEditDraft.rotY = value;
            else if (id == "editRotZ") _itemEditDraft.rotZ = value;
            else if (id == "editScale") _itemEditDraft.scale = value;
            else changed = false;
        }
        if (changed) _itemEditApplyPending.store(true);
    }

    void RmlPanelHost::ApplyItemEditDraftGameThread()
    {
        ItemEditDraft draft;
        vrui::VRUIItemEditPanel* backend = nullptr;
        {
            std::scoped_lock lock(_itemEditMutex);
            draft = _itemEditDraft;
            backend = _itemEditBackend;
        }
        if (!backend) return;
        backend->setWorkingTransform(
            draft.posX, draft.posY, draft.posZ,
            draft.rotX, draft.rotY, draft.rotZ, draft.scale);
    }

    void RmlPanelHost::ExecuteItemEditActionGameThread(ItemEditAction action)
    {
        vrui::VRUIItemEditPanel* backend = nullptr;
        {
            std::scoped_lock lock(_itemEditMutex);
            backend = _itemEditBackend;
        }
        if (!backend) {
            Close();
            return;
        }

        bool succeeded = true;
        switch (action) {
        case ItemEditAction::kApplyItem:
            ApplyItemEditDraftGameThread();
            backend->applyItemOffsets();
            break;
        case ItemEditAction::kApplyCategory:
            ApplyItemEditDraftGameThread();
            backend->applyCategoryOffsets();
            break;
        case ItemEditAction::kReset:
            {
                std::string sourcePanel;
                {
                    std::scoped_lock lock(_itemEditMutex);
                    sourcePanel = _itemEditDraft.sourcePanel;
                }
                backend->resetItemOffsets();
                Close();
                vrui::VRMenuManager::get().switchToPanel(sourcePanel.empty() ? "MainPanel" : sourcePanel);
            }
            return;
        case ItemEditAction::kBack:
            {
                std::string sourcePanel;
                {
                    std::scoped_lock lock(_itemEditMutex);
                    sourcePanel = _itemEditDraft.sourcePanel;
                }
                Close();
                vrui::VRMenuManager::get().switchToPanel(sourcePanel.empty() ? "MainPanel" : sourcePanel);
            }
            return;
        case ItemEditAction::kPinDashboard:
            ApplyItemEditDraftGameThread();
            succeeded = backend->pinToDashboard();
            break;
        case ItemEditAction::kPinLeftHand:
            ApplyItemEditDraftGameThread();
            succeeded = backend->pinToLeftHand();
            break;
        case ItemEditAction::kPinWorld:
            {
                bool canPinToWorld = false;
                {
                    std::scoped_lock lock(_itemEditMutex);
                    canPinToWorld = _itemEditDraft.canPinToWorld;
                }
                if (canPinToWorld) {
                    ApplyItemEditDraftGameThread();
                    succeeded = backend->pinToWorld();
                } else {
                    succeeded = false;
                }
            }
            break;
        case ItemEditAction::kToggleLabel:
            {
                const bool hidden = backend->togglePinnedLabel();
                std::scoped_lock lock(_itemEditMutex);
                _itemEditDraft.labelHidden = hidden;
            }
            break;
        case ItemEditAction::kNone:
            return;
        }

        if (!succeeded) {
            _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError));
        }
        const auto state = backend->getEditState();
        {
            std::scoped_lock lock(_itemEditMutex);
            _itemEditDraft.boardPinnedToWorld = state.boardPinnedToWorld;
            _itemEditDraft.labelHidden = state.labelHidden;
            _itemEditDraft.canPinToWorld = state.canPinToWorld;
        }
        _rmlItemEditSyncPending.store(true);
    }

    // RmlUi is the only local panel renderer. All interaction is handled above
    // by DragonBoardRmlUi and converted into game-thread requests.









}
