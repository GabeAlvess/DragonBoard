#include "ui/imgui/DragonBoardSettingsMenu.h"
#include "ui/imgui/StandaloneImGuiStyle.h"
#include "ui/rml/DragonBoardRmlUi.h"

#include "ImGuiVRHelperAPI.h"
#include "ImGuiVRHelperTypes.h"
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
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <RE/R/Renderer.h>

namespace dragonboard::ui::imgui
{
    namespace
    {
        constexpr uint32_t kLocalSettingsContentId = 0xFFFFFFFFu;

        using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
        PresentFn g_originalPresent = nullptr;
        std::chrono::steady_clock::time_point g_lastPresent;
        bool g_lastPresentValid = false;

        HRESULT WINAPI StandaloneSettingsPresent(
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
                DragonBoardSettingsMenu::GetSingleton().RenderPresentThread(dt);
            } catch (const std::exception& e) {
                logger::error("DragonBoardVR: standalone Settings render exception: {}", e.what());
            }
            return g_originalPresent(swapChain, syncInterval, flags);
        }

    }

    DragonBoardSettingsMenu& DragonBoardSettingsMenu::GetSingleton()
    {
        static DragonBoardSettingsMenu singleton;
        return singleton;
    }

    DragonBoardSettingsMenu::~DragonBoardSettingsMenu()
    {
        // The NiSourceTexture must never outlive the tiny renderer bridge while
        // still pointing at it. Restore the engine-owned placeholder first,
        // then release the COM references acquired from API007 directly; the
        // helper DLL may already be shutting down at this point.
        if (_screenSourceTexture && _sceneTextureBridge) {
            _screenSourceTexture->rendererTexture = _originalRendererTexture;
        }
        _sceneTextureBridge.reset();
        if (_panelTexture.srv) {
            _panelTexture.srv->Release();
        }
        if (_panelTexture.texture) {
            _panelTexture.texture->Release();
        }
        _panelTexture = {};
        _boundTexture = nullptr;
        _boundSrv = nullptr;

        _rmlUi.reset();

        if (_imguiContext) {
            ImGui::SetCurrentContext(_imguiContext);
            ImGui_ImplDX11_Shutdown();
            ImGui::DestroyContext(_imguiContext);
            _imguiContext = nullptr;
        }
        if (_settingsSrv) _settingsSrv->Release();
        if (_settingsRtv) _settingsRtv->Release();
        if (_settingsTexture) _settingsTexture->Release();
        if (_context) _context->Release();
        if (_device) _device->Release();
    }

    bool DragonBoardSettingsMenu::Connect()
    {
        if (_connected.load()) {
            return true;
        }
        if (_helperConnectionAttempted.exchange(true)) {
            return false;
        }

		_helper010 = ImGuiVRHelperPluginAPI::GetImGuiVRHelperInterface010();
		_helper008 = _helper010 ? static_cast<ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface008*>(_helper010) :
		                         ImGuiVRHelperPluginAPI::GetImGuiVRHelperInterface008();
        _helper007 = _helper008 ? static_cast<ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface007*>(_helper008) :
                                 ImGuiVRHelperPluginAPI::GetImGuiVRHelperInterface007();
        _helper006 = _helper007 ? static_cast<ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface006*>(_helper007) :
                                 ImGuiVRHelperPluginAPI::GetImGuiVRHelperInterface006();
        _helper = _helper006 ? static_cast<ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface001*>(_helper006) :
                              ImGuiVRHelperPluginAPI::GetImGuiVRHelperInterface001();
        if (!_helper) {
            logger::info("DragonBoardVR: ImGui VR Helper not found; standalone Settings remains available.");
            return false;
        }

        constexpr uint32_t flags =
            ImGuiVRHelperPluginAPI::kClientFlag_LiveTool |
            ImGuiVRHelperPluginAPI::kClientFlag_PointerFocus;

        _clientId = _helper->RegisterClient(
            "DragonBoardVR Physical Host",
            Plugin::VERSION.string().c_str(),
            &DragonBoardSettingsMenu::OnHelperFrame,
            this,
            flags);

        if (_clientId == 0) {
            _helper = nullptr;
            logger::warn("DragonBoardVR: ImGui VR Helper rejected the settings client.");
            return false;
        }

        CaptureSettingsGameThread();
        _connected.store(true);
        logger::info(
            "DragonBoardVR: ImGui VR Helper connected (build {}, client {}).",
            _helper->GetBuildNumber(),
            _clientId);
        if (_helper006) {
            logger::info("DragonBoardVR: ImGui VR Helper physical-surface API 006 available.");
        } else {
            logger::warn(
                "DragonBoardVR: ImGui VR Helper API 006 unavailable; using the helper's own pose and raycast.");
        }
        if (_helper007) {
            logger::info("DragonBoardVR: ImGui VR Helper scene-host API 007 available.");
        }
        if (_helper008) {
            logger::info("DragonBoardVR: ImGui VR Helper universal physical-host API 008 available.");
        }
		if (_helper010) {
			logger::info("DragonBoardVR: external hosted panels will request 1920x1080 through API 010.");
		} else if (_helper008) {
			logger::warn("DragonBoardVR: ImGui VR Helper API 010 unavailable; hosted panels keep helper sizing.");
		}
        return true;
    }

    bool DragonBoardSettingsMenu::Open()
    {
        // The local Settings panel owns its ImGui context and texture.  The
        // helper connection is optional and is used only for external panels.
        Connect();
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: Standalone Settings render hook unavailable; using the 3D fallback.");
            return false;
        }

        CaptureSettingsGameThread();
        ResetStandaloneInput();
        _localPanelMode.store(LocalPanelMode::kSettings);
        _useRmlSettings.store(true);
        _rmlSettingsSyncPending.store(true);
        _closePending.store(false);
        _visible.store(true);
        if (_helper008 && _clientId != 0) {
            _helper008->ReleaseHostedPanel(_clientId);
        } else if (_helper && _clientId != 0) {
            _helper->ReleaseFocus(_clientId);
        }
        return true;
    }

    bool DragonBoardSettingsMenu::OpenDev()
    {
        Connect();
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: Standalone Dev render hook unavailable.");
            return false;
        }

        LoadDevCommandsGameThread();
        CaptureDevGameInfoGameThread();
        ResetStandaloneInput();
        _devInfoRefreshAccumulator = 0.0f;
        _localPanelMode.store(LocalPanelMode::kDeveloper);
        _useRmlDeveloper.store(true);
        _rmlDeveloperSyncPending.store(true);
        _closePending.store(false);
        _visible.store(true);
        if (_helper008 && _clientId != 0) {
            _helper008->ReleaseHostedPanel(_clientId);
        } else if (_helper && _clientId != 0) {
            _helper->ReleaseFocus(_clientId);
        }
        return true;
    }

    bool DragonBoardSettingsMenu::OpenItemEdit(vrui::VRUIItemEditPanel* editor)
    {
        if (!editor) return false;
        Connect();
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: standalone item editor render hook unavailable.");
            return false;
        }

        const auto state = editor->getEditState();
        ResetStandaloneInput();
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
        _useRmlItemEdit.store(true);
        _rmlItemEditSyncPending.store(true);
        _itemEditApplyPending.store(false);
        _itemEditActionPending.store(ItemEditAction::kNone);
        _closePending.store(false);
        _visible.store(true);
        if (_helper008 && _clientId != 0) {
            _helper008->ReleaseHostedPanel(_clientId);
        } else if (_helper && _clientId != 0) {
            _helper->ReleaseFocus(_clientId);
        }
        logger::info("DragonBoardVR: opened RmlUi item editor for {:08X} '{}'.", state.formID, state.itemName);
        return true;
    }

    void DragonBoardSettingsMenu::RequestRmlWarmup()
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

    bool DragonBoardSettingsMenu::IsDevOpen() const
    {
        return _visible.load() && _localPanelMode.load() == LocalPanelMode::kDeveloper;
    }

    void DragonBoardSettingsMenu::Close()
    {
        ResetStandaloneInput();
        _visible.store(false);
        _closePending.store(true);
    }

    void DragonBoardSettingsMenu::CloseHostedPanel()
    {
        ResetStandaloneInput();
        _visible.store(false);
        _closePending.store(true);
        if (_helper008 && _clientId != 0) {
            _helper008->ReleaseHostedPanel(_clientId);
        } else if (_helper && _clientId != 0) {
            _helper->ReleaseFocus(_clientId);
        }
    }

    void DragonBoardSettingsMenu::OnDominantVrButtonEvent(
        bool triggerButton,
        bool gripButton,
        bool pressed)
    {
        if (!_visible.load(std::memory_order_acquire)) return;

        // This state belongs only to the flat RmlUi/ImGui panel. The global
        // DragonBoard input state is deliberately left untouched so activation
        // chords such as Grip + Y continue to receive the original VR events.
        if (triggerButton) {
            const bool previous = _standaloneTriggerDown.exchange(
                pressed, std::memory_order_acq_rel);
            if (previous != pressed) {
                logger::info(
                    "DragonBoardVR: local panel dominant trigger {} (raw grip match={}).",
                    pressed ? "down" : "up",
                    gripButton);
            }
        } else if (gripButton) {
            const bool previous = _standaloneGripDown.exchange(
                pressed, std::memory_order_acq_rel);
            if (previous != pressed) {
                logger::info(
                    "DragonBoardVR: local panel dominant grip {}.",
                    pressed ? "down" : "up");
            }
        }
    }

    void DragonBoardSettingsMenu::ResetStandaloneInput()
    {
        _standaloneTriggerDown.store(false, std::memory_order_release);
        _standaloneGripDown.store(false, std::memory_order_release);
        _standaloneStickX.store(0.0f, std::memory_order_release);
        _standaloneStickY.store(0.0f, std::memory_order_release);
    }

    void DragonBoardSettingsMenu::UpdateGameThread(float deltaTime)
    {
        auto& manager = vrui::VRMenuManager::get();

        // API008 turns DragonBoard into a host even before its own Settings
        // client is opened, so another helper client can take focus and appear
        // on the board through its normal hotkey.
        if (manager.isMenuOpen() && !_connected.load()) {
            Connect();
        }

        UpdateClientSurfaceGameThread();
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
            _standaloneStickX.store(stickX);
            _standaloneStickY.store(stickY);

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

        _closePending.store(false);
    }

    void DragonBoardSettingsMenu::UpdateClientSurfaceGameThread()
    {
        auto& manager = vrui::VRMenuManager::get();
        const bool localSettingsActive = _visible.load();
        const bool universalHost = _helper008 != nullptr;
        const bool hostActive = manager.isMenuOpen() && (universalHost || localSettingsActive);
        if (!hostActive) {
			if (_helper010 && _hostSizeSubmitted) {
				_helper010->SetPhysicalHostPanelSize(_clientId, 0, 0);
				_hostSizeSubmitted = false;
			}
            if (!universalHost) {
                SetExternalHostGameThread(false);
            }
            if (_screenNode) {
                _screenNode->SetAppCulled(true);
            }
            _scenePanelVisible = false;
            _pointerInHostedPanel = false;
            _hostedClientId = 0;
            if (_surfaceSubmitted && _helper006 && _clientId != 0) {
                if (_helper008) {
                    _helper008->SubmitPhysicalHostSurface(_clientId, nullptr);
                } else {
                    _helper006->SubmitClientSurface(_clientId, nullptr);
                }
                _surfaceSubmitted = false;
            }
            if (_visible.load() && !manager.isMenuOpen()) {
                Close();
            }
            return;
        }

		if (localSettingsActive && _helper010 && _hostSizeSubmitted) {
			_helper010->SetPhysicalHostPanelSize(_clientId, 0, 0);
			_hostSizeSubmitted = false;
		} else if (!localSettingsActive && _helper010 && _clientId != 0 && !_hostSizeSubmitted) {
			_hostSizeSubmitted = _helper010->SetPhysicalHostPanelSize(_clientId, 1920, 1080);
			if (!_hostSizeSubmitted) {
				logger::warn("DragonBoardVR: helper rejected the 1920x1080 physical-host request.");
			}
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

        const float effectiveScreenScale = standalone::kSceneScreenSizeScale;
        const float width = std::max(
            1.0f,
            layoutPanel->getWidth() * worldScale * effectiveScreenScale);
        const float height = std::max(
            1.0f,
            layoutPanel->getHeight() * worldScale * effectiveScreenScale);

        ImGuiVRHelperPluginAPI::ClientSurfaceState state{};
        state.flags = ImGuiVRHelperPluginAPI::kClientSurfaceFlag_Visible;
        state.center[0] = worldPosition.x;
        state.center[1] = worldPosition.y;
        state.center[2] = worldPosition.z;

        // The board mesh's front-face correction is a local 180-degree turn.
        // Publish the opposite X basis so the Helper samples the texture from
        // that front face instead of showing it mirrored from the back.
        state.right[0] = -worldRotation.entry[0][0];
        state.right[1] = -worldRotation.entry[1][0];
        state.right[2] = -worldRotation.entry[2][0];
        state.up[0] = worldRotation.entry[0][2];
        state.up[1] = worldRotation.entry[1][2];
        state.up[2] = worldRotation.entry[2][2];
        state.width_units = width;
        state.height_units = height;

        const RE::NiPoint3 rayOrigin = manager.getLaserOrigin();
        const RE::NiPoint3 rayDirection = manager.getLaserDirection();
        const RE::NiPoint3 right(state.right[0], state.right[1], state.right[2]);
        const RE::NiPoint3 up(state.up[0], state.up[1], state.up[2]);
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
                    state.flags |= ImGuiVRHelperPluginAPI::kClientSurfaceFlag_PointerInPanel;
                    state.pointer_u = u;
                    state.pointer_v = v;
                    _pointerU = u;
                    _pointerV = v;
                }
            }
        }
        _pointerInHostedPanel =
            (state.flags & ImGuiVRHelperPluginAPI::kClientSurfaceFlag_PointerInPanel) != 0;

        const bool firstSurfaceSubmission = !_surfaceSubmitted;
        if (!localSettingsActive && _helper006 && _clientId != 0 && !_helper008) {
            _helper006->SubmitClientSurface(_clientId, &state);
            _surfaceSubmitted = true;
        }

        // Only suppress the helper's post-composited quad after the same panel
        // texture is successfully hosted by a dedicated child of Tablet.nif.
        // Until then API006 remains the visible fallback.
        const bool sceneHosted = UpdateScenePanelGameThread(surfacePanel->getBackgroundNode());
        _scenePanelVisible = sceneHosted;
        if (localSettingsActive) {
            if (_surfaceSubmitted && _helper006 && _clientId != 0) {
                if (_helper008) {
                    _helper008->SubmitPhysicalHostSurface(_clientId, nullptr);
                } else {
                    _helper006->SubmitClientSurface(_clientId, nullptr);
                }
                _surfaceSubmitted = false;
            }
            SetExternalHostGameThread(false);
        } else if (_helper008 && _clientId != 0) {
            if (sceneHosted) {
                _helper008->SubmitPhysicalHostSurface(_clientId, &state);
                _surfaceSubmitted = true;
            } else if (_surfaceSubmitted) {
                _helper008->SubmitPhysicalHostSurface(_clientId, nullptr);
                _surfaceSubmitted = false;
            }
        } else if (_helper007 && _clientId != 0) {
            SetExternalHostGameThread(sceneHosted);
        }

        if (firstSurfaceSubmission && _surfaceSubmitted) {
            logger::info(
                "DragonBoardVR: Publishing ImGui surface '{}' size {:.2f} x {:.2f} at ({:.2f}, {:.2f}, {:.2f}); source={} anchor='{}'.",
                surfacePanel->getName(),
                width,
                height,
                state.center[0],
                state.center[1],
                state.center[2],
                magicNodeAnchored ? "MagicNode.world x panel.local" : "panel.world",
                menuHandNode ? menuHandNode->name.c_str() : "<none>");
        }
    }

    bool DragonBoardSettingsMenu::UpdateScenePanelGameThread(RE::NiNode* backgroundNode)
    {
        if (!backgroundNode) {
            return false;
        }

        ImGuiVRHelperPluginAPI::PanelTextureHandle acquired{};
        uint32_t contentClientId = 0;
        ID3D11Texture2D* selectedTexture = nullptr;
        ID3D11ShaderResourceView* selectedSrv = nullptr;
        uint32_t selectedWidth = 0;
        uint32_t selectedHeight = 0;
        bool externalAcquired = false;

        if (_visible.load()) {
            // Texture creation and ImGui rendering happen from Present. Never
            // touch the immediate context from the game-update task.
            if (!_rendererReady.load()) {
                return false;
            }
            contentClientId = kLocalSettingsContentId;
            selectedTexture = _settingsTexture;
            selectedSrv = _settingsSrv;
            selectedWidth = standalone::kPanelWidth;
            selectedHeight = standalone::kPanelHeight;
        } else if (_helper007 && _clientId != 0) {
            contentClientId = _clientId;
            externalAcquired = _helper008 ?
                _helper008->AcquireFocusedPanelTexture(_clientId, &acquired, &contentClientId) :
                _helper007->AcquirePanelTexture(_clientId, &acquired);
            if (externalAcquired) {
                selectedTexture = acquired.texture;
                selectedSrv = acquired.srv;
                selectedWidth = acquired.width;
                selectedHeight = acquired.height;
            }
        }

        if (!selectedTexture || !selectedSrv) {
            if (_screenNode) {
                _screenNode->SetAppCulled(true);
            }
            _hostedClientId = 0;
            return false;
        }

        if (!_screenNode) {
            _screenNode = vrui::VRUIWidget::loadModelFromNif("DragonBoardVR\\ImGuiScreen.nif", false);
            if (!_screenNode) {
                if (externalAcquired) _helper007->ReleasePanelTexture(&acquired);
                return false;
            }

            _screenNode->name = "DragonBoardVR_ImGuiScreen";
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

                    // ImGuiScreen.nif is a dedicated SSE quad with a complete
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
                if (externalAcquired) _helper007->ReleasePanelTexture(&acquired);
                return false;
            }
        }

        constexpr float kPlaneExtent = 170.666656f;
        const float screenWidth = 18.0f * standalone::kSceneScreenSizeScale;
        const float screenHeight = screenWidth * 9.0f / 16.0f;
        _screenNode->local.rotate.entry[0][0] = screenWidth / kPlaneExtent;
        _screenNode->local.rotate.entry[1][1] = screenHeight / kPlaneExtent;

        if (_hostedClientId != contentClientId ||
            _boundTexture != selectedTexture || _boundSrv != selectedSrv) {
            if (_screenSourceTexture && _sceneTextureBridge) {
                _screenSourceTexture->rendererTexture = _originalRendererTexture;
            }
            _sceneTextureBridge.reset();
            if ((_panelTexture.texture || _panelTexture.srv) && _helper007) {
                _helper007->ReleasePanelTexture(&_panelTexture);
            }
            _panelTexture = {};

            _sceneTextureBridge = std::make_unique<RE::BSGraphics::Texture>();
            _sceneTextureBridge->texture = selectedTexture;
            _sceneTextureBridge->unk08 = 0;
            _sceneTextureBridge->resourceView = selectedSrv;
            _screenSourceTexture->rendererTexture = _sceneTextureBridge.get();
            if (externalAcquired) {
                _panelTexture = acquired;
                acquired = {};
            }
            _boundTexture = selectedTexture;
            _boundSrv = selectedSrv;
            _hostedClientId = contentClientId;

            logger::info(
                "DragonBoardVR: ImGui client {} texture bound to scene screen ({}x{}, texture={}, srv={}).",
                _hostedClientId,
                selectedWidth,
                selectedHeight,
                static_cast<void*>(selectedTexture),
                static_cast<void*>(selectedSrv));
        }
        if (externalAcquired) _helper007->ReleasePanelTexture(&acquired);

        if (_screenNode->parent != backgroundNode) {
            if (_screenNode->parent) {
                _screenNode->parent->DetachChild(_screenNode.get());
            }
            backgroundNode->AttachChild(_screenNode.get());
            logger::info(
                "DragonBoardVR: ImGui scene screen attached to tablet node '{}'.",
                backgroundNode->name.c_str());
        }

        _screenNode->SetAppCulled(false);
        RE::NiUpdateData updateData;
        _screenNode->Update(updateData);
        _screenNode->UpdateWorldBound();
        return _sceneTextureBridge != nullptr;
    }

    void DragonBoardSettingsMenu::SetExternalHostGameThread(bool enabled)
    {
        if (!_helper007 || _clientId == 0 || _externalHostEnabled == enabled) {
            return;
        }
        _helper007->SetExternalPanelHost(_clientId, enabled);
        _externalHostEnabled = enabled;
        logger::info(
            "DragonBoardVR: ImGui scene host {}.",
            enabled ? "enabled" : "disabled (API006 fallback active)");
    }

    void DragonBoardSettingsMenu::OnHelperFrame(
        [[maybe_unused]] const ImGuiVRHelperPluginAPI::Frame* frame,
        [[maybe_unused]] void* user)
    {
        // Host-only client. External mods render through their own callbacks;
        // DragonBoard Settings is rendered independently below.
    }

    bool DragonBoardSettingsMenu::EnsurePresentHookInstalled()
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
        vtable[8] = reinterpret_cast<void*>(&StandaloneSettingsPresent);
        DWORD ignored = 0;
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), &vtable[8], sizeof(void*));

        logger::info(
            "DragonBoardVR: standalone Settings Present hook installed (next={}).",
            reinterpret_cast<void*>(g_originalPresent));
        return true;
    }

    void DragonBoardSettingsMenu::RenderPresentThread(float deltaTime)
    {
        if (_rmlWarmupRequested.exchange(false, std::memory_order_acq_rel) &&
            !_rendererReady.load(std::memory_order_acquire)) {
            _rmlWarmupAttempted.store(true, std::memory_order_release);
            const auto started = std::chrono::steady_clock::now();
            const bool initialized = InitializeStandaloneRenderer();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (initialized) {
                logger::info(
                    "DragonBoardVR: RmlUi warm-up completed in {} ms before a local panel was opened.",
                    elapsedMs);
            } else {
                logger::warn(
                    "DragonBoardVR: RmlUi warm-up failed after {} ms; normal panel fallback remains available.",
                    elapsedMs);
            }
        }
        if (_visible.load()) {
            RenderStandalone(deltaTime);
        }
    }

    bool DragonBoardSettingsMenu::InitializeStandaloneRenderer()
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
        desc.Width = standalone::kPanelWidth;
        desc.Height = standalone::kPanelHeight;
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
            logger::error("DragonBoardVR: Failed to create the standalone 1920x1080 Settings texture.");
            if (srv) srv->Release();
            if (rtv) rtv->Release();
            if (texture) texture->Release();
            context->Release();
            device->Release();
            return false;
        }

        IMGUI_CHECKVERSION();
        auto* imguiContext = ImGui::CreateContext();
        if (!imguiContext) {
            srv->Release();
            rtv->Release();
            texture->Release();
            context->Release();
            device->Release();
            return false;
        }
        ImGui::SetCurrentContext(imguiContext);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // This context is fully standalone; the helper no longer composites a
        // cursor into its panel texture for us.
        io.MouseDrawCursor = true;
        standalone::ConfigureFonts(io);

        standalone::ApplyStyle();

        if (!ImGui_ImplDX11_Init(device, context)) {
            logger::error("DragonBoardVR: Failed to initialize the standalone ImGui DX11 backend.");
            ImGui::DestroyContext(imguiContext);
            srv->Release();
            rtv->Release();
            texture->Release();
            context->Release();
            device->Release();
            return false;
        }

        _imguiContext = imguiContext;
        _device = device;
        _context = context;
        _settingsTexture = texture;
        _settingsRtv = rtv;
        _settingsSrv = srv;

        _rmlUi = std::make_unique<dragonboard::ui::rml::DragonBoardRmlUi>();
        if (!_rmlUi->Initialize(device, context)) {
            logger::warn("DragonBoardVR: RmlUi Settings unavailable; the existing ImGui view remains active.");
            _rmlUi.reset();
        }
        _rendererReady.store(true);
        logger::info("DragonBoardVR: standalone panel renderer initialized at 1920x1080.");
        return true;
    }

    void DragonBoardSettingsMenu::RenderStandalone(float deltaTime)
    {
        if (!_visible.load() || !InitializeStandaloneRenderer()) return;

        const auto panelMode = _localPanelMode.load();
        const bool settingsRmlActive = panelMode == LocalPanelMode::kSettings &&
            _useRmlSettings.load() && _rmlUi && _rmlUi->IsSettingsReady();
        const bool developerRmlActive = panelMode == LocalPanelMode::kDeveloper &&
            _useRmlDeveloper.load() && _rmlUi && _rmlUi->IsDeveloperReady();
        const bool itemEditRmlActive = panelMode == LocalPanelMode::kItemEdit &&
            _useRmlItemEdit.load() && _rmlUi && _rmlUi->IsItemEditReady();
        if (settingsRmlActive || developerRmlActive || itemEditRmlActive) {
            if (settingsRmlActive) {
                _rmlUi->ShowSettings();
                if (_rmlSettingsSyncPending.exchange(false)) SyncRmlSettingsFromDraft();
            } else if (developerRmlActive) {
                _rmlUi->ShowDeveloper();
                if (_rmlDeveloperSyncPending.exchange(false)) SyncRmlDeveloperCommands();
                SyncRmlDeveloperInfo();
            } else {
                _rmlUi->ShowItemEdit();
                if (_rmlItemEditSyncPending.exchange(false)) SyncRmlItemEdit();
            }
            _rmlUi->ProcessInput(
                _pointerInHostedPanel.load(),
                _pointerU.load(),
                _pointerV.load(),
                _standaloneTriggerDown.load(),
                _standaloneGripDown.load(),
                _standaloneStickX.load(),
                _standaloneStickY.load(),
                static_cast<int>(standalone::kPanelWidth),
                static_cast<int>(standalone::kPanelHeight));

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

                const bool triggerDown = _standaloneTriggerDown.load();
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
            } else {
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
            }

            const bool rendered = _rmlUi->Render(
                _settingsRtv,
                static_cast<int>(standalone::kPanelWidth),
                static_cast<int>(standalone::kPanelHeight));
            if (_rmlUi->ConsumeCloseRequested()) {
                Close();
            }
            if (_rmlUi->ConsumeImGuiFallbackRequested()) {
                if (settingsRmlActive) {
                    _useRmlSettings.store(false);
                } else if (developerRmlActive) {
                    _useRmlDeveloper.store(false);
                } else {
                    _useRmlItemEdit.store(false);
                }
                _previousTriggerDown = _standaloneTriggerDown.load();
                logger::info(
                    "DragonBoardVR: switched {} from RmlUi to the ImGui fallback.",
                    settingsRmlActive ? "Settings" : (developerRmlActive ? "Developer" : "Item editor"));
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

            logger::error("DragonBoardVR: RmlUi frame failed; switching to the ImGui fallback.");
            if (settingsRmlActive) {
                _useRmlSettings.store(false);
            } else if (developerRmlActive) {
                _useRmlDeveloper.store(false);
            } else {
                _useRmlItemEdit.store(false);
            }
        }

        ImGui::SetCurrentContext(_imguiContext);
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(
            static_cast<float>(standalone::kPanelWidth),
            static_cast<float>(standalone::kPanelHeight));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::clamp(deltaTime, 1.0f / 240.0f, 0.1f);

        ImGui_ImplDX11_NewFrame();
        PumpStandaloneInput(io.DisplaySize.x, io.DisplaySize.y);
        ImGui::NewFrame();
        DrawLocalPanel();
        ImGui::Render();

        _presentFrameMs = io.DeltaTime * 1000.0f;
        _presentFps = io.DeltaTime > 0.0f ? 1.0f / io.DeltaTime : 0.0f;
        _panelDrawCalls = 0;
        if (const auto* drawData = ImGui::GetDrawData()) {
            for (int i = 0; i < drawData->CmdListsCount; ++i) {
                _panelDrawCalls += drawData->CmdLists[i]->CmdBuffer.Size;
            }
        }

        ID3D11RenderTargetView* oldRtv = nullptr;
        ID3D11DepthStencilView* oldDsv = nullptr;
        _context->OMGetRenderTargets(1, &oldRtv, &oldDsv);

        D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        _context->RSGetViewports(&viewportCount, oldViewports);

        const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        _context->OMSetRenderTargets(1, &_settingsRtv, nullptr);
        _context->ClearRenderTargetView(_settingsRtv, clear);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(standalone::kPanelWidth);
        viewport.Height = static_cast<float>(standalone::kPanelHeight);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        _context->RSSetViewports(1, &viewport);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        _context->OMSetRenderTargets(1, &oldRtv, oldDsv);
        if (viewportCount > 0) {
            _context->RSSetViewports(viewportCount, oldViewports);
        }
        if (oldRtv) oldRtv->Release();
        if (oldDsv) oldDsv->Release();
    }

    void DragonBoardSettingsMenu::SyncRmlSettingsFromDraft()
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

    void DragonBoardSettingsMenu::ApplyRmlSliderChange(std::string_view id, float value)
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

    void DragonBoardSettingsMenu::SyncRmlDeveloperCommands()
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

    void DragonBoardSettingsMenu::SyncRmlDeveloperInfo()
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
        info.helperConnected = _connected.load();
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

    void DragonBoardSettingsMenu::SyncRmlItemEdit()
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

    void DragonBoardSettingsMenu::ApplyRmlItemEditSliderChange(std::string_view id, float value)
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

    void DragonBoardSettingsMenu::ApplyItemEditDraftGameThread()
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

    void DragonBoardSettingsMenu::ExecuteItemEditActionGameThread(ItemEditAction action)
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

    void DragonBoardSettingsMenu::PumpStandaloneInput(float width, float height)
    {
        auto& io = ImGui::GetIO();
        const bool pointerOnPanel = _pointerInHostedPanel.load();
        if (pointerOnPanel) {
            io.AddMousePosEvent(
                std::clamp(_pointerU.load(), 0.0f, 1.0f) * width,
                std::clamp(_pointerV.load(), 0.0f, 1.0f) * height);
            _pointerWasOnPanel = true;
        } else if (_pointerWasOnPanel && !ImGui::IsAnyItemActive()) {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            _pointerWasOnPanel = false;
        }

        const bool triggerDown = _standaloneTriggerDown.load();
        if (triggerDown != _previousTriggerDown) {
            if (!triggerDown || pointerOnPanel) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, triggerDown);
            }
            _previousTriggerDown = triggerDown;
        }

        // Trigger is click-only. Grip explicitly arms scrolling so a stick
        // movement can never steal a click while the user edits a control.
        const bool gripDown = _standaloneGripDown.load();
        const bool scrollArmed = gripDown && !triggerDown;
        const float stickX = _standaloneStickX.load();
        const float stickY = _standaloneStickY.load();
        constexpr float deadzone = 0.15f;
        if (scrollArmed && (std::abs(stickX) > deadzone || std::abs(stickY) > deadzone)) {
            _scrollAccumulatorX += stickX * 0.1f;
            _scrollAccumulatorY += stickY * 0.1f;
            float wheelX = 0.0f;
            float wheelY = 0.0f;
            if (std::abs(_scrollAccumulatorX) > 0.3f) {
                wheelX = _scrollAccumulatorX > 0.0f ? 1.0f : -1.0f;
                _scrollAccumulatorX = 0.0f;
            }
            if (std::abs(_scrollAccumulatorY) > 0.3f) {
                wheelY = _scrollAccumulatorY > 0.0f ? 1.0f : -1.0f;
                _scrollAccumulatorY = 0.0f;
            }
            if (wheelX != 0.0f || wheelY != 0.0f) {
                io.AddMouseWheelEvent(-wheelX, -wheelY);
            }
        } else if (!scrollArmed) {
            _scrollAccumulatorX = 0.0f;
            _scrollAccumulatorY = 0.0f;
        }
    }

    void DragonBoardSettingsMenu::DrawLocalPanel()
    {
        if (_localPanelMode.load() == LocalPanelMode::kDeveloper) {
            DrawDeveloperPanel();
        } else if (_localPanelMode.load() == LocalPanelMode::kItemEdit) {
            DrawItemEditPanel();
        } else {
            DrawSettings();
        }
    }

    void DragonBoardSettingsMenu::DrawItemEditPanel()
    {
        std::scoped_lock lock(_itemEditMutex);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(1920.0f, 1080.0f));
        ImGui::Begin("DragonBoard item editor fallback", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::Text("Editing: %s", _itemEditDraft.itemName.c_str());
        ImGui::Text("%s  |  %08X", _itemEditDraft.category.c_str(), _itemEditDraft.formID);
        ImGui::Separator();
        ImGui::SliderFloat("Position X", &_itemEditDraft.posX, -20.0f, 20.0f);
        ImGui::SliderFloat("Position Y", &_itemEditDraft.posY, -20.0f, 20.0f);
        ImGui::SliderFloat("Position Z", &_itemEditDraft.posZ, -20.0f, 20.0f);
        ImGui::SliderFloat("Rotation X", &_itemEditDraft.rotX, -180.0f, 180.0f);
        ImGui::SliderFloat("Rotation Y", &_itemEditDraft.rotY, -180.0f, 180.0f);
        ImGui::SliderFloat("Rotation Z", &_itemEditDraft.rotZ, -180.0f, 180.0f);
        ImGui::SliderFloat("Scale", &_itemEditDraft.scale, 0.01f, 5.0f);
        if (ImGui::IsAnyItemActive()) _itemEditApplyPending.store(true);
        if (ImGui::Button("Apply Item")) _itemEditActionPending.store(ItemEditAction::kApplyItem);
        ImGui::SameLine();
        if (ImGui::Button("Apply Category")) _itemEditActionPending.store(ItemEditAction::kApplyCategory);
        ImGui::SameLine();
        if (ImGui::Button("Reset")) _itemEditActionPending.store(ItemEditAction::kReset);
        ImGui::SameLine();
        if (ImGui::Button("Back")) _itemEditActionPending.store(ItemEditAction::kBack);
        ImGui::End();
    }









}
