#include "ui/rml/RmlPanelHost.h"
#include "ui/rml/DragonBoardRmlUi.h"

#include "vrui/VRMenuManager.h"
#include "vrui/VRUIPanel.h"
#include "vrui/VRUIItemEditPanel.h"
#include "vrui/VRUIInventoryContainer.h"
#include "vrui/VRUIMagicContainer.h"
#include "vrui/VRUIItemUtils.h"
#include "vrui/VRUISettings.h"
#include "vrui/VRUIMapMarker.h"
#include "vrui/ModActionManager.h"
#include "game/actions/ActionExecutor.h"
#include "ui/pointer/PointerVisualController.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <Windows.h>

#include <d3d11.h>
#include <RE/B/BSOpenVR.h>
#include <RE/R/Renderer.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        constexpr float kSceneScreenSizeScale = 0.85f;
        constexpr float kScenePlaneExtent = 170.666656f;
        // Keep CSS layout and pointer coordinates stable while allowing the
        // backing texture to render at a lower physical resolution.
        constexpr int kPanelLogicalWidth = 1920;
        constexpr int kPanelLogicalHeight = 1080;
        constexpr const char* kInventoryKeyboardOverlayKey =
            "dragonboardvr.inventory.search.keyboard";
        constexpr const char* kInventoryKeyboardOverlayName =
            "DragonBoardVR Inventory Search";
        constexpr std::uint64_t kInventoryKeyboardUserValue =
            0x4456425253454152ULL;
        constexpr std::size_t kInventorySearchMaximumLength = 96;
        constexpr const char* kMagicKeyboardOverlayKey =
            "dragonboardvr.magic.search.keyboard";
        constexpr const char* kMagicKeyboardOverlayName =
            "DragonBoardVR Magic Search";
        constexpr std::uint64_t kMagicKeyboardUserValue =
            0x445642524D414749ULL;
        constexpr std::size_t kMagicSearchMaximumLength = 96;
        constexpr const char* kDeveloperKeyboardOverlayKey =
            "dragonboardvr.developer.command.keyboard";
        constexpr const char* kDeveloperKeyboardOverlayName =
            "DragonBoardVR Add Developer Command";
        constexpr std::uint64_t kDeveloperKeyboardUserValue =
            0x4456425244455643ULL;
        constexpr std::size_t kDeveloperCommandMaximumLength = 256;

        vr::IVROverlay* GetSteamVrOverlay()
        {
#ifdef ENABLE_SKYRIM_VR
            static vr::IVROverlay* cachedOverlay = nullptr;
            if (cachedOverlay) return cachedOverlay;

            auto* openVR = RE::BSOpenVR::GetSingleton();
            if (openVR) {
                if (openVR->ivrOverlay) {
                    cachedOverlay = openVR->ivrOverlay;
                    return cachedOverlay;
                }
                if (openVR->unk190) {
                    cachedOverlay = RE::BSOpenVR::GetIVROverlay(openVR->unk190);
                    if (cachedOverlay) return cachedOverlay;
                }
            }

            const auto openVrModule = GetModuleHandleW(L"openvr_api.dll");
            if (!openVrModule) {
                logger::error(
                    "DragonBoardVR: openvr_api.dll is not loaded; SteamVR keyboard is unavailable.");
                return nullptr;
            }

            using GetGenericInterfaceFn =
                void*(__cdecl*)(const char*, vr::EVRInitError*);
            const auto getGenericInterface = reinterpret_cast<GetGenericInterfaceFn>(
                GetProcAddress(openVrModule, "VR_GetGenericInterface"));
            if (!getGenericInterface) {
                logger::error(
                    "DragonBoardVR: openvr_api.dll does not export VR_GetGenericInterface.");
                return nullptr;
            }

            vr::EVRInitError error = vr::VRInitError_None;
            cachedOverlay = static_cast<vr::IVROverlay*>(
                getGenericInterface(vr::IVROverlay_Version, &error));
            if (!cachedOverlay || error != vr::VRInitError_None) {
                logger::error(
                    "DragonBoardVR: direct IVROverlay request '{}' failed with OpenVR error {}.",
                    vr::IVROverlay_Version,
                    static_cast<int>(error));
                cachedOverlay = nullptr;
                return nullptr;
            }
            logger::info(
                "DragonBoardVR: IVROverlay '{}' obtained directly from openvr_api.dll.",
                vr::IVROverlay_Version);
            return cachedOverlay;
#endif
            return nullptr;
        }

        const char* InventoryFilterId(vrui::InventoryFilterMode filter)
        {
            using Filter = vrui::InventoryFilterMode;
            switch (filter) {
            case Filter::WeaponsAll: return "weapons";
            case Filter::ArmorAll: return "armor";
            case Filter::ConsumablesAll: return "consumables";
            case Filter::QuestItems: return "quest";
            case Filter::BooksAll: return "books";
            case Filter::MiscAll: return "misc";
            default: return "";
            }
        }

        const char* MagicFilterId(vrui::MagicFilterMode filter)
        {
            using Filter = vrui::MagicFilterMode;
            switch (filter) {
            case Filter::Destruction: return "destruction";
            case Filter::Conjuration: return "conjuration";
            case Filter::Restoration: return "restoration";
            case Filter::Illusion: return "illusion";
            case Filter::Alteration: return "alteration";
            case Filter::Powers: return "powers";
            case Filter::Passive: return "passive";
            default: return "";
            }
        }

        const char* QuestTypeLabel(RE::QUEST_DATA::Type type)
        {
            using Type = RE::QUEST_DATA::Type;
            switch (type) {
            case Type::kMainQuest: return "MAIN QUEST";
            case Type::kMagesGuild: return "COLLEGE OF WINTERHOLD";
            case Type::kThievesGuild: return "THIEVES GUILD";
            case Type::kDarkBrotherhood: return "DARK BROTHERHOOD";
            case Type::kCompanionsQuest: return "COMPANIONS";
            case Type::kMiscellaneous: return "MISCELLANEOUS";
            case Type::kDaedric: return "DAEDRIC";
            case Type::kSideQuest: return "SIDE QUEST";
            case Type::kCivilWar: return "CIVIL WAR";
            case Type::kDLC01_Vampire: return "DAWNGUARD";
            case Type::kDLC02_Dragonborn: return "DRAGONBORN";
            default: return "QUEST";
            }
        }

        const char* ObjectiveStateLabel(RE::QUEST_OBJECTIVE_STATE state)
        {
            using State = RE::QUEST_OBJECTIVE_STATE;
            switch (state) {
            case State::kCompleted:
            case State::kCompletedDisplayed:
                return "DONE";
            case State::kFailed:
            case State::kFailedDisplayed:
                return "FAILED";
            case State::kDisplayed:
                return "ACTIVE";
            default:
                return "";
            }
        }

        bool ObjectiveCompleted(RE::QUEST_OBJECTIVE_STATE state)
        {
            return state == RE::QUEST_OBJECTIVE_STATE::kCompleted ||
                   state == RE::QUEST_OBJECTIVE_STATE::kCompletedDisplayed;
        }

        bool ObjectiveFailed(RE::QUEST_OBJECTIVE_STATE state)
        {
            return state == RE::QUEST_OBJECTIVE_STATE::kFailed ||
                   state == RE::QUEST_OBJECTIVE_STATE::kFailedDisplayed;
        }

        std::string ResolveJournalText(RE::TESQuest* quest, std::uint32_t instanceID)
        {
#ifdef ENABLE_SKYRIM_VR
            if (!quest) return {};
            using func_t = void(RE::TESQuest*, RE::BSString&, std::uint32_t);
            static REL::Relocation<func_t> getJournalText{
                REL::VariantID(0, 0, 0x0388850)
            };
            RE::BSString text;
            getJournalText(quest, text, instanceID);
            return text.empty() ? std::string{} : std::string(text.c_str());
#else
            (void)quest;
            (void)instanceID;
            return {};
#endif
        }

        RE::BGSQuestInstanceText* FindQuestInstanceText(
            RE::TESQuest* quest,
            std::uint32_t instanceID)
        {
            if (!quest) return nullptr;
            const auto found = std::find_if(
                quest->instanceData.begin(),
                quest->instanceData.end(),
                [instanceID](const RE::BGSQuestInstanceText* instance) {
                    return instance && instance->id == instanceID;
                });
            return found != quest->instanceData.end() ? *found : nullptr;
        }

        std::string ResolveAliasName(
            RE::TESQuest* quest,
            std::uint32_t instanceID,
            std::string_view aliasName)
        {
            if (!quest || aliasName.empty()) return {};
            const auto equalsIgnoreCase = [](std::string_view left, std::string_view right) {
                return left.size() == right.size() &&
                       std::equal(
                           left.begin(),
                           left.end(),
                           right.begin(),
                           [](unsigned char a, unsigned char b) {
                               return std::tolower(a) == std::tolower(b);
                           });
            };
            if (equalsIgnoreCase(aliasName, "Player")) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    const char* name = player->GetName();
                    if (name && *name) return name;
                }
            }

            RE::BGSBaseAlias* matchingAlias = nullptr;
            for (auto* alias : quest->aliases) {
                if (alias &&
                    equalsIgnoreCase(alias->aliasName.c_str(), aliasName)) {
                    matchingAlias = alias;
                    break;
                }
            }
            if (!matchingAlias) return {};

            if (auto* instance = FindQuestInstanceText(quest, instanceID)) {
                const auto stored = std::find_if(
                    instance->stringData.begin(),
                    instance->stringData.end(),
                    [matchingAlias](const RE::BGSQuestInstanceText::StringData& data) {
                        return data.aliasID == matchingAlias->aliasID;
                    });
                if (stored != instance->stringData.end() && stored->fullNameFormID != 0) {
                    if (auto* form = RE::TESForm::LookupByID(stored->fullNameFormID)) {
                        const char* name = form->GetName();
                        if (name && *name) return name;
                    }
                }
            }

            if (const auto* refAlias =
                    skyrim_cast<RE::BGSRefAlias*>(matchingAlias)) {
                if (auto* reference = refAlias->GetReference()) {
                    const char* name = reference->GetDisplayFullName();
                    if (name && *name) return name;
                }
            }
            return {};
        }

        std::string ResolveQuestObjectiveText(
            RE::TESQuest* quest,
            std::uint32_t instanceID,
            std::string text)
        {
            constexpr std::string_view prefix = "<Alias";
            std::size_t cursor = 0;
            while ((cursor = text.find(prefix, cursor)) != std::string::npos) {
                const auto equals = text.find('=', cursor + prefix.size());
                const auto end = text.find('>', cursor + prefix.size());
                if (equals == std::string::npos || end == std::string::npos ||
                    equals > end) {
                    cursor += prefix.size();
                    continue;
                }
                const auto aliasStart = equals + 1;
                const std::string_view aliasName(
                    text.data() + aliasStart,
                    end - aliasStart);
                auto replacement =
                    ResolveAliasName(quest, instanceID, aliasName);
                if (replacement.empty()) replacement = "[...]";
                text.replace(cursor, end - cursor + 1, replacement);
                cursor += replacement.size();
            }
            return text;
        }

        std::uint64_t JournalQuestKey(
            std::uint32_t formID,
            std::uint32_t instanceID)
        {
            return (static_cast<std::uint64_t>(formID) << 32) | instanceID;
        }

#ifdef ENABLE_SKYRIM_VR
        // The vendored CommonLib VR_PLAYER_RUNTIME_DATA currently omits the
        // 0x10 bytes between 0x6E0 and 0x6F0. Consequently its questLog and
        // objectives C++ members resolve 0x10 bytes before the binary-verified
        // Skyrim VR offsets. Keep these two accesses explicit until the
        // upstream layout is corrected.
        RE::BSTArray<RE::BGSInstancedQuestObjective>& GetVrQuestObjectives(
            RE::PlayerCharacter* player)
        {
            constexpr std::ptrdiff_t kObjectivesOffset = 0xB70;
            return *reinterpret_cast<RE::BSTArray<RE::BGSInstancedQuestObjective>*>(
                reinterpret_cast<std::byte*>(player) + kObjectivesOffset);
        }

        RE::BSSimpleList<RE::TESQuestStageItem*>& GetVrQuestLog(
            RE::PlayerCharacter* player)
        {
            constexpr std::ptrdiff_t kQuestLogOffset = 0xB60;
            return *reinterpret_cast<RE::BSSimpleList<RE::TESQuestStageItem*>*>(
                reinterpret_cast<std::byte*>(player) + kQuestLogOffset);
        }

        struct QuestTargetTeleportPathView
        {
            struct ParentSpaceNode
            {
                bool isWorldspace = false;
                std::byte pad01[7]{};
                RE::TESWorldSpace* worldspace = nullptr;
                RE::TESObjectCELL* interiorCell = nullptr;
            };

            struct TeleportLink
            {
                RE::TESObjectREFR* reference = nullptr;
                RE::NiPoint3 teleportLocation{};
            };

            RE::BSTArray<ParentSpaceNode> spaces;
            RE::BSTArray<TeleportLink> teleportLinks;
            RE::NiPoint3 start{};
            RE::NiPoint3 end{};
        };
        static_assert(sizeof(QuestTargetTeleportPathView) == 0x48);
#endif

        template <class QuestContainer>
        std::uint64_t HashJournalState(const QuestContainer& quests)
        {
            std::uint64_t hash = 1469598103934665603ULL;
            const auto append = [&hash](std::uint64_t value) {
                hash ^= value;
                hash *= 1099511628211ULL;
            };
            append(quests.size());
            for (const auto& quest : quests) {
                append(quest.formID);
                append(quest.instanceID);
                append(quest.active);
                append(quest.completed);
                append(quest.failed);
                append(quest.objectives.size());
                for (const auto& objective : quest.objectives) {
                    append(objective.objectiveID);
                    append(objective.instanceID);
                    append(objective.completed);
                    append(objective.failed);
                    append(objective.hasTargets);
                }
            }
            return hash;
        }

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
            (_rendererReady.load() &&
             (!_rmlUi || (_rmlUi->AreBuiltinDocumentsLoaded() && !_rmlUi->IsSettingsReady())))) {
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
            (_rendererReady.load() &&
             (!_rmlUi || (_rmlUi->AreBuiltinDocumentsLoaded() && !_rmlUi->IsDeveloperReady())))) {
            logger::error("DragonBoardVR: RmlUi Developer document is unavailable.");
            return false;
        }

        LoadDevCommandsGameThread();
        CaptureDevGameInfoGameThread();
        ResetPanelInput();
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        _devInfoRefreshAccumulator = 0.0f;
        _presentFrameTimeHistory.fill(0.0f);
        _presentFrameTimeHistoryIndex = 0;
        _presentFrameTimeHistoryCount = 0;
        _presentFrameTimeHistorySum = 0.0f;
        _presentFps = 0.0f;
        _presentFrameMs = 0.0f;
        _localPanelMode.store(LocalPanelMode::kDeveloper);
        _rmlDeveloperSyncPending.store(true);
        _rmlDeveloperInfoSyncPending.store(true);
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
            (_rendererReady.load() &&
             (!_rmlUi || (_rmlUi->AreBuiltinDocumentsLoaded() && !_rmlUi->IsItemEditReady())))) {
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
        editor->setWorkingTransformChangedHandler(
            [this, editor](const vrui::VRUIItemEditPanel::EditState& changed) {
                {
                    std::scoped_lock lock(_itemEditMutex);
                    if (_itemEditBackend != editor) return;
                    _itemEditDraft.posX = changed.posX;
                    _itemEditDraft.posY = changed.posY;
                    _itemEditDraft.posZ = changed.posZ;
                    _itemEditDraft.rotX = changed.rotX;
                    _itemEditDraft.rotY = changed.rotY;
                    _itemEditDraft.rotZ = changed.rotZ;
                    _itemEditDraft.scale = changed.scale;
                }
                _rmlItemEditSyncPending.store(true, std::memory_order_release);
                logger::info(
                    "DragonBoardVR: synchronized item editor draft after preview release "
                    "pos=({:.2f}, {:.2f}, {:.2f}) rot=({:.2f}, {:.2f}, {:.2f}) scale={:.3f}.",
                    changed.posX, changed.posY, changed.posZ,
                    changed.rotX, changed.rotY, changed.rotZ, changed.scale);
            });
        _localPanelMode.store(LocalPanelMode::kItemEdit);
        _rmlItemEditSyncPending.store(true);
        _itemEditApplyPending.store(false);
        _itemEditActionPending.store(ItemEditAction::kNone);
        _visible.store(true);
        logger::info("DragonBoardVR: opened RmlUi item editor for {:08X} '{}'.", state.formID, state.itemName);
        return true;
    }

    bool RmlPanelHost::OpenMods()
    {
        if (!EnsurePresentHookInstalled()) return false;
        if ((_rmlWarmupAttempted.load() && !_rendererReady.load()) ||
            (_rendererReady.load() &&
             (!_rmlUi || (_rmlUi->AreBuiltinDocumentsLoaded() && !_rmlUi->IsModsReady())))) return false;
        const auto actions = vrui::ModActionManager::get().getActions();
        {
            std::scoped_lock lock(_modsMutex);
            _mods.clear();
            _mods.reserve(actions.size());
            for (const auto& action : actions) {
                _mods.push_back({ action.label, action.iconPath, action.command });
            }
        }
        ResetPanelInput();
        _modsHoveredIndex.store(static_cast<std::size_t>(-1));
        _modsOptionsPending.store(static_cast<std::size_t>(-1));
        _modsRemovePending.store(static_cast<std::size_t>(-1));
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        _localPanelMode.store(LocalPanelMode::kMods);
        _rmlModsSyncPending.store(true);
        _visible.store(true);
        return true;
    }

    bool RmlPanelHost::OpenInventory(
        vrui::VRUIInventoryContainer* inventory,
        vrui::VRUIItemEditPanel* preview)
    {
        if (!inventory || !preview) return false;
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: RmlUi Inventory render hook unavailable.");
            return false;
        }
        if ((_rmlWarmupAttempted.load() && !_rendererReady.load()) ||
            (_rendererReady.load() &&
             (!_rmlUi || (_rmlUi->AreBuiltinDocumentsLoaded() && !_rmlUi->IsInventoryReady())))) {
            logger::error("DragonBoardVR: RmlUi Inventory document is unavailable.");
            return false;
        }

        {
            std::scoped_lock lock(_inventoryMutex);
            _inventoryBackend = inventory;
            _inventoryPreviewBackend = preview;
            _inventorySearchQuery.clear();
            _inventoryVisibleIndices.clear();
        }
        preview->setWorkingTransformChangedHandler({});
        preview->setInventoryPreviewInteractionHandler(
            [this](std::uint32_t formID, vrui::EquipHand hand) {
                vrui::VRUIInventoryContainer* backend = nullptr;
                {
                    std::scoped_lock lock(_inventoryMutex);
                    backend = _inventoryBackend;
                }
                if (backend && backend->interactWithItem(formID, hand)) {
                    _inventoryRefreshDelay = 0.25f;
                    logger::trace(
                        "DragonBoardVR: inventory preview grip interaction requested for {:08X}.",
                        formID);
                }
            });
        logger::info("DragonBoardVR: capturing RmlUi inventory snapshot.");
        CaptureInventoryGameThread(false);
        ResetPanelInput();
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        _localPanelMode.store(LocalPanelMode::kInventory);
        _rmlInventorySyncPending.store(true);
        _inventoryActionPending.store(InventoryAction::kNone);
        _inventoryActionIndex.store(0);
        _inventoryActionLeftHand.store(false);
        _inventoryRefreshDelay = -1.0f;
        _inventoryPollAccumulator = 0.0f;
        _visible.store(true);
        std::size_t entryCount = 0;
        {
            std::scoped_lock lock(_inventoryMutex);
            entryCount = _inventoryItems.size();
        }
        logger::info(
            "DragonBoardVR: opened RmlUi inventory with {} entries.",
            entryCount);
        return true;
    }

    bool RmlPanelHost::OpenMagic(
        vrui::VRUIMagicContainer* magic,
        vrui::VRUIItemEditPanel* preview)
    {
        if (!magic || !preview) return false;
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: RmlUi Magic render hook unavailable.");
            return false;
        }
        if ((_rmlWarmupAttempted.load() && !_rendererReady.load()) ||
            (_rendererReady.load() &&
             (!_rmlUi || (_rmlUi->AreBuiltinDocumentsLoaded() && !_rmlUi->IsMagicReady())))) {
            logger::error("DragonBoardVR: RmlUi Magic document is unavailable.");
            return false;
        }

        {
            std::scoped_lock lock(_magicMutex);
            _magicBackend = magic;
            _magicPreviewBackend = preview;
            _magicSearchQuery.clear();
            _magicVisibleIndices.clear();
        }
        preview->setWorkingTransformChangedHandler({});
        preview->setInventoryPreviewInteractionHandler(
            [this](std::uint32_t formID, vrui::EquipHand hand) {
                vrui::VRUIMagicContainer* backend = nullptr;
                {
                    std::scoped_lock lock(_magicMutex);
                    backend = _magicBackend;
                }
                if (backend && backend->activateSpell(formID, hand)) {
                    _magicRefreshDelay = 0.20f;
                    logger::trace(
                        "DragonBoardVR: magic preview grip interaction requested for {:08X}.",
                        formID);
                }
            });
        CaptureMagicGameThread(false);
        ResetPanelInput();
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        _localPanelMode.store(LocalPanelMode::kMagic);
        _rmlMagicSyncPending.store(true);
        _magicActionPending.store(MagicAction::kNone);
        _magicActionIndex.store(0);
        _magicActionLeftHand.store(false);
        _magicRefreshDelay = -1.0f;
        _magicPollAccumulator = 0.0f;
        _visible.store(true);
        std::size_t entryCount = 0;
        {
            std::scoped_lock lock(_magicMutex);
            entryCount = _magicItems.size();
        }
        logger::info(
            "DragonBoardVR: opened RmlUi magic panel with {} entries.",
            entryCount);
        return true;
    }

    bool RmlPanelHost::OpenJournal()
    {
        if (!EnsurePresentHookInstalled()) {
            logger::error("DragonBoardVR: RmlUi Journal render hook unavailable.");
            return false;
        }
        if ((_rmlWarmupAttempted.load() && !_rendererReady.load()) ||
            (_rendererReady.load() &&
             (!_rmlUi || (_rmlUi->AreBuiltinDocumentsLoaded() && !_rmlUi->IsJournalReady())))) {
            logger::error("DragonBoardVR: RmlUi Journal document is unavailable.");
            return false;
        }

        CaptureJournalGameThread(false);
        ResetPanelInput();
        _activeExternalPanel.store(DragonBoardVR_API::InvalidPanel);
        _localPanelMode.store(LocalPanelMode::kJournal);
        _rmlJournalSyncPending.store(true);
        _journalActionPending.store(JournalAction::kNone);
        _journalActionFormID.store(0);
        _journalActionInstanceID.store(0);
        _journalActionObjectiveInstanceID.store(0);
        _journalActionObjectiveID.store(0);
        _journalPollAccumulator = 0.0f;
        _visible.store(true);
        logger::info("DragonBoardVR: opened RmlUi journal.");
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

    void RmlPanelHost::RequestQuestMarkerRestore()
    {
        const auto& settings = vrui::VRUISettings::get();
        if (settings.questMarkerLastFormID == 0) return;
        _questMarkerRestoreDelay = 2.0f;
        _questMarkerRestoreAttempts = 8;
    }

    bool RmlPanelHost::IsDeveloperOpen() const
    {
        return _visible.load() && _localPanelMode.load() == LocalPanelMode::kDeveloper;
    }

    bool RmlPanelHost::IsSettingsOpen() const
    {
        return _visible.load() && _localPanelMode.load() == LocalPanelMode::kSettings;
    }

    bool RmlPanelHost::IsJournalOpen() const
    {
        return _visible.load() && _localPanelMode.load() == LocalPanelMode::kJournal;
    }

    bool RmlPanelHost::IsModsOpen() const
    {
        return _visible.load(std::memory_order_acquire) &&
            _localPanelMode.load(std::memory_order_acquire) == LocalPanelMode::kMods;
    }

    bool RmlPanelHost::RequestHoveredModOptions()
    {
        if (!IsModsOpen()) return false;
        const auto index = _modsHoveredIndex.load(std::memory_order_acquire);
        if (index == static_cast<std::size_t>(-1)) return false;
        {
            std::scoped_lock lock(_modsMutex);
            if (index >= _mods.size()) return false;
        }
        _modsOptionsPending.store(index, std::memory_order_release);
        return true;
    }

    bool RmlPanelHost::RequestHoveredModRemoval()
    {
        if (!IsModsOpen()) return false;
        const auto index = _modsHoveredIndex.load(std::memory_order_acquire);
        if (index == static_cast<std::size_t>(-1)) return false;
        {
            std::scoped_lock lock(_modsMutex);
            if (index >= _mods.size()) return false;
        }
        _modsRemovePending.store(index, std::memory_order_release);
        return true;
    }

    std::shared_ptr<vrui::VRUIWidget> RmlPanelHost::GetPreviewInteractionTarget()
    {
        if (!_visible.load(std::memory_order_acquire)) {
            return nullptr;
        }
        if (!_previewInteractionZoneHovered.load(std::memory_order_acquire)) {
            return nullptr;
        }

        const auto mode = _localPanelMode.load(std::memory_order_acquire);
        if (mode == LocalPanelMode::kInventory) {
            std::scoped_lock lock(_inventoryMutex);
            return _inventoryPreviewBackend ?
                _inventoryPreviewBackend->getPreviewWidget() : nullptr;
        } else if (mode == LocalPanelMode::kMagic) {
            std::scoped_lock lock(_magicMutex);
            return _magicPreviewBackend ?
                _magicPreviewBackend->getPreviewWidget() : nullptr;
        }

        return nullptr;
    }

    void RmlPanelHost::Close()
    {
        // Menu recreation destroys the native inventory/magic panels. Clear
        // their non-owning backends while holding the same locks used by the
        // per-frame preview lookup, so the pointer cannot outlive its panel.
        {
            std::scoped_lock lock(_inventoryMutex, _magicMutex);
            _inventoryBackend = nullptr;
            _inventoryPreviewBackend = nullptr;
            _magicBackend = nullptr;
            _magicPreviewBackend = nullptr;
        }
        ResetPanelInput();
        _previewInteractionZoneHovered.store(false, std::memory_order_release);
        _developerKeyboardCloseRequested.store(true, std::memory_order_release);
        _inventoryKeyboardCloseRequested.store(true, std::memory_order_release);
        _magicKeyboardCloseRequested.store(true, std::memory_order_release);
        _modsHoveredIndex.store(static_cast<std::size_t>(-1), std::memory_order_release);
        _visible.store(false);
    }

    void RmlPanelHost::OnVrButtonEvent(
        bool leftHand,
        bool triggerButton,
        bool gripButton,
        bool pressed)
    {
        if (!_visible.load(std::memory_order_acquire)) return;

        // This state belongs only to the flat RmlUi panel. The global
        // DragonBoard input state is deliberately left untouched so activation
        // chords such as Grip + Y continue to receive the original VR events.
        if (triggerButton) {
            _lastTriggerWasLeft.store(leftHand, std::memory_order_release);
            if (leftHand) {
                _leftTriggerDown.store(pressed, std::memory_order_release);
            } else {
                _rightTriggerDown.store(pressed, std::memory_order_release);
            }
            const bool anyTriggerDown =
                _leftTriggerDown.load(std::memory_order_acquire) ||
                _rightTriggerDown.load(std::memory_order_acquire);
            const bool previous = _triggerDown.exchange(
                anyTriggerDown, std::memory_order_acq_rel);
            if (previous != anyTriggerDown) {
                logger::info(
                    "DragonBoardVR: local panel {} trigger {}.",
                    leftHand ? "left" : "right",
                    anyTriggerDown ? "down" : "up");
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
        _leftTriggerDown.store(false, std::memory_order_release);
        _rightTriggerDown.store(false, std::memory_order_release);
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
            } else if (command.type == RenderCommandType::kRegister ||
                       command.type == RenderCommandType::kUnregister ||
                       command.type == RenderCommandType::kShow) {
                _renderScheduler.MarkDirty(RmlDirtyReason::kDocument);
            } else {
                _renderScheduler.MarkDirty(RmlDirtyReason::kData);
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
                const bool triggerWasLeft =
                    _lastTriggerWasLeft.load(std::memory_order_acquire);
                const bool triggerWasDominant =
                    vrui::VRUISettings::get().useLeftHandAsMenu ?
                    !triggerWasLeft : triggerWasLeft;
                manager.triggerHaptic(triggerWasDominant, intensity, duration);
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
            } else if (_localPanelMode.load() == LocalPanelMode::kInventory) {
                _inventoryPollAccumulator += std::clamp(deltaTime, 0.0f, 0.5f);
                if (_inventoryPollAccumulator >= 0.4f) {
                    _inventoryPollAccumulator = 0.0f;
                    vrui::VRUIInventoryContainer* backend = nullptr;
                    std::uint64_t previousSignature = 0;
                    {
                        std::scoped_lock lock(_inventoryMutex);
                        backend = _inventoryBackend;
                        previousSignature = _inventoryStateSignature;
                    }
                    if (backend) {
                        const auto currentSignature =
                            backend->buildRmlInventorySignature();
                        if (currentSignature != previousSignature) {
                            logger::trace(
                                "DragonBoardVR: inventory contents changed while RmlUi inventory was open.");
                            CaptureInventoryGameThread(true);
                        }
                    }
                }
            } else if (_localPanelMode.load() == LocalPanelMode::kMagic) {
                _magicPollAccumulator += std::clamp(deltaTime, 0.0f, 0.5f);
                if (_magicPollAccumulator >= 0.4f) {
                    _magicPollAccumulator = 0.0f;
                    vrui::VRUIMagicContainer* backend = nullptr;
                    std::uint64_t previousSignature = 0;
                    {
                        std::scoped_lock lock(_magicMutex);
                        backend = _magicBackend;
                        previousSignature = _magicStateSignature;
                    }
                    if (backend) {
                        const auto currentSignature =
                            backend->buildRmlMagicSignature();
                        if (currentSignature != previousSignature) {
                            logger::trace(
                                "DragonBoardVR: magic state changed while RmlUi magic was open.");
                            CaptureMagicGameThread(true);
                        }
                    }
                }
            } else if (_localPanelMode.load() == LocalPanelMode::kJournal) {
                _journalPollAccumulator += std::clamp(deltaTime, 0.0f, 0.5f);
                if (_journalPollAccumulator >= 0.5f) {
                    _journalPollAccumulator = 0.0f;
                    CaptureJournalGameThread(true);
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
        if (_inventoryRefreshDelay >= 0.0f) {
            _inventoryRefreshDelay -= std::clamp(deltaTime, 0.0f, 0.5f);
            if (_inventoryRefreshDelay <= 0.0f) {
                _inventoryRefreshDelay = -1.0f;
                CaptureInventoryGameThread(true);
            }
        }
        if (_inventoryPreviewRefreshPending.exchange(false, std::memory_order_acq_rel)) {
            UpdateInventoryPreviewGameThread();
        }
        if (_magicRefreshDelay >= 0.0f) {
            _magicRefreshDelay -= std::clamp(deltaTime, 0.0f, 0.5f);
            if (_magicRefreshDelay <= 0.0f) {
                _magicRefreshDelay = -1.0f;
                CaptureMagicGameThread(true);
            }
        }
        if (_magicPreviewRefreshPending.exchange(false, std::memory_order_acq_rel)) {
            UpdateMagicPreviewGameThread();
        }
        if (_questTargetResolveDelay >= 0.0f) {
            _questTargetResolveDelay -= std::clamp(deltaTime, 0.0f, 0.5f);
            if (_questTargetResolveDelay <= 0.0f) {
                const bool resolved = CacheQuestObjectiveTargetGameThread(
                    _questTargetResolveFormID,
                    _questTargetResolveInstanceID,
                    _questTargetResolveObjectiveID);
                if (resolved || _questTargetResolveAttempts <= 1) {
                    _questTargetResolveDelay = -1.0f;
                    _questTargetResolveAttempts = 0;
                } else {
                    --_questTargetResolveAttempts;
                    _questTargetResolveDelay = 0.75f;
                }
            }
        }
        if (_questMarkerRestoreDelay >= 0.0f) {
            _questMarkerRestoreDelay -= std::clamp(deltaTime, 0.0f, 0.5f);
            if (_questMarkerRestoreDelay <= 0.0f) {
                if (RestoreQuestMarkerGameThread() || _questMarkerRestoreAttempts <= 1) {
                    _questMarkerRestoreDelay = -1.0f;
                    _questMarkerRestoreAttempts = 0;
                } else {
                    --_questMarkerRestoreAttempts;
                    _questMarkerRestoreDelay = 1.0f;
                }
            }
        }
        if (_questMarkerWatchFormID != 0) {
            _questMarkerPollAccumulator += std::clamp(deltaTime, 0.0f, 0.5f);
            if (_questMarkerPollAccumulator >= 0.5f) {
                _questMarkerPollAccumulator = 0.0f;
                RefreshTrackedQuestObjectiveGameThread();
            }
        }
        if (_questMovingTargetHandle) {
            _questMovingTargetPollAccumulator += std::clamp(deltaTime, 0.0f, 0.5f);
            if (_questMovingTargetPollAccumulator >= 1.0f) {
                _questMovingTargetPollAccumulator = 0.0f;
                RefreshMovingQuestTargetGameThread();
            }
        }
        if (const auto action = _itemEditActionPending.exchange(ItemEditAction::kNone);
            action != ItemEditAction::kNone) {
            ExecuteItemEditActionGameThread(action);
        }
        if (const auto action = _inventoryActionPending.exchange(InventoryAction::kNone);
            action != InventoryAction::kNone) {
            ExecuteInventoryActionGameThread(
                action,
                _inventoryActionIndex.exchange(0),
                _inventoryActionLeftHand.load(std::memory_order_acquire));
        }
        if (const auto action = _magicActionPending.exchange(MagicAction::kNone);
            action != MagicAction::kNone) {
            ExecuteMagicActionGameThread(
                action,
                _magicActionIndex.exchange(0),
                _magicActionLeftHand.load(std::memory_order_acquire));
        }
        if (const auto action = _journalActionPending.exchange(JournalAction::kNone);
            action != JournalAction::kNone) {
            ExecuteJournalActionGameThread(
                action,
                _journalActionFormID.exchange(0),
                _journalActionInstanceID.exchange(0),
                _journalActionObjectiveInstanceID.exchange(0),
                _journalActionObjectiveID.exchange(0));
        }
        if (_modsAddPending.exchange(false)) {
            Close();
            vrui::VRMenuManager::get().toggleMenu();
            vrui::ModActionManager::get().startListening();
        }
        if (_modsClosePending.exchange(false)) {
            Close();
            vrui::VRMenuManager::get().switchToPanel("MainPanel");
        }
        if (const auto index = _modsActivatePending.exchange(static_cast<std::size_t>(-1));
            index != static_cast<std::size_t>(-1)) {
            std::string command;
            {
                std::scoped_lock lock(_modsMutex);
                if (index < _mods.size()) command = _mods[index].command;
            }
            if (!command.empty()) {
                const auto parsed = dragonboard::game::actions::Parse(command);
                if (parsed.kind != dragonboard::game::actions::ActionKind::kUnknown) {
                    const auto side = vrui::VRUISettings::get().useLeftHandAsMenu ?
                        dragonboard::game::actions::EquipSide::kRight :
                        dragonboard::game::actions::EquipSide::kLeft;
                    vrui::VRMenuManager::get().toggleMenu();
                    (void)dragonboard::game::actions::Execute(
                        parsed, side, dragonboard::game::actions::ExecutionContext::kModsPanel);
                }
            }
        }
        if (const auto index = _modsOptionsPending.exchange(static_cast<std::size_t>(-1));
            index != static_cast<std::size_t>(-1)) {
            std::optional<ModEntry> mod;
            {
                std::scoped_lock lock(_modsMutex);
                if (index < _mods.size()) mod = _mods[index];
            }
            if (mod) {
                const auto parsed = dragonboard::game::actions::Parse(mod->command);
                std::string category = "Mods";
                if (parsed.kind == dragonboard::game::actions::ActionKind::kCastPower) {
                    category = "Magic";
                } else if (parsed.kind == dragonboard::game::actions::ActionKind::kEquipItem) {
                    category = "Misc";
                }
                auto editPanel = std::dynamic_pointer_cast<vrui::VRUIItemEditPanel>(
                    manager.findPanelByName("ItemEditPanel"));
                if (editPanel) {
                    editPanel->setTargetItem(
                        category, mod->label, mod->iconPath, parsed.formID,
                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                        "ModsPanel", mod->command);
                    manager.switchToPanel("ItemEditPanel");
                }
            }
        }
        if (const auto index = _modsRemovePending.exchange(static_cast<std::size_t>(-1));
            index != static_cast<std::size_t>(-1)) {
            vrui::ModActionManager::get().removeAction(index);
            const auto actions = vrui::ModActionManager::get().getActions();
            {
                std::scoped_lock lock(_modsMutex);
                _mods.clear();
                _mods.reserve(actions.size());
                for (const auto& action : actions) {
                    _mods.push_back({ action.label, action.iconPath, action.command });
                }
            }
            _modsHoveredIndex.store(static_cast<std::size_t>(-1), std::memory_order_release);
            _rmlModsSyncPending.store(true, std::memory_order_release);
            logger::info("DragonBoardVR: removed Mods action {} through Y/B long press.", index);
        }

        if (_devCommandAdditionPending.exchange(false, std::memory_order_acq_rel)) {
            std::string command;
            {
                std::scoped_lock lock(_devMutex);
                command = std::move(_pendingDevCommandAddition);
            }
            AddDevCommandGameThread(std::move(command));
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

        if (_mapCalibrationResetPending.exchange(false, std::memory_order_acq_rel)) {
            auto& settings = vrui::VRUISettings::get();
            settings.mapCalibrationPoints = {};
            vrui::VRMenuManager::get().saveSettingsNow();
            CaptureDevGameInfoGameThread();
            _rmlDeveloperInfoSyncPending.store(true, std::memory_order_release);
            logger::info("DragonBoardVR: map calibration reset; legacy moving marker restored.");
        }

        if (const auto city = _mapCalibrationCityPending.exchange(
                static_cast<std::size_t>(-1), std::memory_order_acq_rel);
            city != static_cast<std::size_t>(-1)) {
            CaptureMapCalibrationGameThread(
                city,
                _mapCalibrationPointerU.load(std::memory_order_acquire),
                _mapCalibrationPointerV.load(std::memory_order_acquire));
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

        // Update the actual textured quad first.  The previous pointer mapping
        // intersected an approximation based on Persistent_Panel dimensions,
        // while the visible RmlUi texture inherits the tablet NIF transform
        // and has its own 16:9 scale.  That made the two pointers disagree
        // even before any input smoothing was applied.
        _scenePanelVisible =
            UpdateScenePanelGameThread(surfacePanel->getBackgroundNode());
        if (!_scenePanelVisible || !_screenNode) {
            _pointerInHostedPanel = false;
            return;
        }

        const auto& screenTransform = _screenNode->world;
        RE::NiPoint3 screenXAxis(
            screenTransform.rotate.entry[0][0],
            screenTransform.rotate.entry[1][0],
            screenTransform.rotate.entry[2][0]);
        RE::NiPoint3 screenYAxis(
            screenTransform.rotate.entry[0][1],
            screenTransform.rotate.entry[1][1],
            screenTransform.rotate.entry[2][1]);
        const float xAxisLength = screenXAxis.Length();
        const float yAxisLength = screenYAxis.Length();
        const float worldScale = std::abs(screenTransform.scale);
        if (xAxisLength <= 1e-5f || yAxisLength <= 1e-5f || worldScale <= 1e-5f) {
            _pointerInHostedPanel = false;
            return;
        }

        // Derive the logical axes and extents directly from the quad.  Its UV
        // orientation follows the positive local X axis.
        const RE::NiPoint3 right(
            screenXAxis.x / xAxisLength,
            screenXAxis.y / xAxisLength,
            screenXAxis.z / xAxisLength);
        const RE::NiPoint3 up(
            screenYAxis.x / yAxisLength,
            screenYAxis.y / yAxisLength,
            screenYAxis.z / yAxisLength);
        const float width = kScenePlaneExtent * xAxisLength * worldScale;
        const float height = kScenePlaneExtent * yAxisLength * worldScale;
        const RE::NiPoint3 worldPosition = screenTransform.translate;

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
    }

    bool RmlPanelHost::PanelUvToMapMarkerLocal(
        float pointerU, float pointerV, float& panelX, float& panelY) const
    {
        if (!_screenNode) return false;
        auto surfacePanel = vrui::VRMenuManager::get().findPanelByName("Background_Panel");
        if (!surfacePanel || !surfacePanel->getNode()) return false;

        const auto& screen = _screenNode->world;
        RE::NiPoint3 screenX(
            screen.rotate.entry[0][0], screen.rotate.entry[1][0], screen.rotate.entry[2][0]);
        RE::NiPoint3 screenY(
            screen.rotate.entry[0][1], screen.rotate.entry[1][1], screen.rotate.entry[2][1]);
        const float screenXLength = screenX.Length();
        const float screenYLength = screenY.Length();
        const float screenScale = std::abs(screen.scale);
        if (screenXLength <= 1.0e-5f || screenYLength <= 1.0e-5f || screenScale <= 1.0e-5f) {
            return false;
        }
        screenX = screenX / screenXLength;
        screenY = screenY / screenYLength;
        const float width = kScenePlaneExtent * screenXLength * screenScale;
        const float height = kScenePlaneExtent * screenYLength * screenScale;
        const RE::NiPoint3 worldPoint = screen.translate +
            screenX * ((std::clamp(pointerU, 0.0f, 1.0f) - 0.5f) * width) +
            screenY * ((0.5f - std::clamp(pointerV, 0.0f, 1.0f)) * height);

        const auto& parent = surfacePanel->getNode()->world;
        if (std::abs(parent.scale) <= 1.0e-5f) return false;
        const RE::NiPoint3 delta = worldPoint - parent.translate;
        const RE::NiPoint3 local(
            (delta.x * parent.rotate.entry[0][0] + delta.y * parent.rotate.entry[1][0] +
                delta.z * parent.rotate.entry[2][0]) / parent.scale,
            (delta.x * parent.rotate.entry[0][1] + delta.y * parent.rotate.entry[1][1] +
                delta.z * parent.rotate.entry[2][1]) / parent.scale,
            (delta.x * parent.rotate.entry[0][2] + delta.y * parent.rotate.entry[1][2] +
                delta.z * parent.rotate.entry[2][2]) / parent.scale);
        panelX = local.x;
        panelY = local.z;
        return std::isfinite(panelX) && std::isfinite(panelY);
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

        const float screenWidth = 18.0f * kSceneScreenSizeScale;
        const float screenHeight = screenWidth * 9.0f / 16.0f;
        _screenNode->local.rotate.entry[0][0] = screenWidth / kScenePlaneExtent;
        _screenNode->local.rotate.entry[1][1] = screenHeight / kScenePlaneExtent;

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
                _panelWidth,
                _panelHeight);
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
        UpdateDeveloperCommandKeyboardPresentThread();
        UpdateInventorySearchKeyboardPresentThread();
        UpdateMagicSearchKeyboardPresentThread();
        bool initializedThisFrame = false;
        if (_rmlWarmupRequested.exchange(false, std::memory_order_acq_rel) &&
            !_rendererReady.load(std::memory_order_acquire)) {
            _rmlWarmupAttempted.store(true, std::memory_order_release);
            const auto started = std::chrono::steady_clock::now();
            const bool initialized = InitializeRenderer();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (initialized) {
                initializedThisFrame = true;
                logger::info(
                    "DragonBoardVR: RmlUi core warm-up completed in {} ms; "
                    "documents will load on later Present frames.",
                    elapsedMs);
            } else {
                _rmlWarmupAttempted.store(false, std::memory_order_release);
                logger::warn(
                    "DragonBoardVR: RmlUi warm-up failed after {} ms.",
                    elapsedMs);
            }
        }
        if (_rendererReady.load(std::memory_order_acquire)) {
            ApplyRenderCommandsPresentThread();
            if (!initializedThisFrame) {
                AdvanceRmlPrewarmPresentThread();
            }
        }
        const auto& settings = vrui::VRUISettings::get();
        _renderScheduler.Configure(settings.rmlRenderOnDirty, settings.rmlMaxActiveFPS);
        const bool visible = _visible.load(std::memory_order_acquire);
        if (visible != _rmlWasVisiblePresentThread) {
            _rmlWasVisiblePresentThread = visible;
            _renderScheduler.SetVisible(visible);
            _rmlInputStateInitialized = false;
            _lastRmlPanelModePresentThread.reset();
            _lastRmlExternalPanelPresentThread = DragonBoardVR_API::InvalidPanel;
            _rmlRenderRateAccumulator = 0.0f;
            _rmlRendersInRateWindow = 0;
            _rmlRendersPerSecond = 0.0f;
            if (visible) {
                _rmlCachedFrames = 0;
                _rmlDirtyReason = "Open";
            }
        }
        if (visible) {
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

        const auto& settings = vrui::VRUISettings::get();
        const auto requestedWidth = static_cast<std::uint32_t>(settings.rmlRenderWidth);
        const auto requestedHeight = static_cast<std::uint32_t>(settings.rmlRenderHeight);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = requestedWidth;
        desc.Height = requestedHeight;
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
            logger::error(
                "DragonBoardVR: failed to create the {}x{} RmlUi render texture.",
                requestedWidth,
                requestedHeight);
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
        _panelWidth = requestedWidth;
        _panelHeight = requestedHeight;
        _rmlPrewarmStep = 0;
        _rmlPrewarmFrameCount = 0;
        _rmlPrewarmTotalMs = 0;
        _rmlPrewarmComplete = false;
        _rendererReady.store(true);
        logger::info(
            "DragonBoardVR: RmlUi panel host initialized at {}x{}.",
            _panelWidth,
            _panelHeight);
        return true;
    }

    bool RmlPanelHost::EnsureRenderTargetSizePresentThread(
        std::uint32_t width,
        std::uint32_t height)
    {
        if (!_device || width == 0 || height == 0 || width * 9 != height * 16) {
            return false;
        }
        if (_panelRenderTexture && _panelRenderTarget && _panelShaderResource &&
            _panelWidth == width && _panelHeight == height) {
            return true;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* renderTarget = nullptr;
        ID3D11ShaderResourceView* shaderResource = nullptr;
        if (FAILED(_device->CreateTexture2D(&desc, nullptr, &texture)) ||
            FAILED(_device->CreateRenderTargetView(texture, nullptr, &renderTarget)) ||
            FAILED(_device->CreateShaderResourceView(texture, nullptr, &shaderResource))) {
            if (shaderResource) shaderResource->Release();
            if (renderTarget) renderTarget->Release();
            if (texture) texture->Release();
            logger::error(
                "DragonBoardVR: failed to resize the RmlUi render texture to {}x{}; "
                "keeping {}x{}.",
                width,
                height,
                _panelWidth,
                _panelHeight);
            return false;
        }

        auto* oldTexture = _panelRenderTexture;
        auto* oldRenderTarget = _panelRenderTarget;
        auto* oldShaderResource = _panelShaderResource;
        _panelRenderTexture = texture;
        _panelRenderTarget = renderTarget;
        _panelShaderResource = shaderResource;
        _panelWidth = width;
        _panelHeight = height;
        if (_sceneTextureBridge) {
            _sceneTextureBridge->texture = _panelRenderTexture;
            _sceneTextureBridge->resourceView = _panelShaderResource;
        }
        if (oldShaderResource) oldShaderResource->Release();
        if (oldRenderTarget) oldRenderTarget->Release();
        if (oldTexture) oldTexture->Release();

        _renderScheduler.MarkDirty(RmlDirtyReason::kResolution);
        logger::info(
            "DragonBoardVR: resized the RmlUi render texture to {}x{}.",
            _panelWidth,
            _panelHeight);
        return true;
    }

    void RmlPanelHost::AdvanceRmlPrewarmPresentThread()
    {
        if (_rmlPrewarmComplete || !_rmlUi || !_panelRenderTarget) {
            return;
        }

        if (!_rmlUi->AreBuiltinDocumentsLoaded()) {
            const auto started = std::chrono::steady_clock::now();
            const bool loaded = _rmlUi->LoadNextBuiltinDocument();
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            _rmlPrewarmTotalMs += elapsedMs;
            ++_rmlPrewarmFrameCount;
            logger::info(
                "DragonBoardVR: staged one RmlUi document in {} ms (loaded={}).",
                elapsedMs,
                loaded);
            return;
        }

        // Document rendering is only speculative prewarm. Do not switch away
        // from a panel that the player has already opened.
        if (_visible.load()) return;

        using RmlUi = dragonboard::ui::rml::DragonBoardRmlUi;
        using ShowDocument = bool (RmlUi::*)();
        struct PrewarmDocument
        {
            const char* name;
            ShowDocument show;
        };
        static constexpr std::array<PrewarmDocument, 7> kDocuments{ {
            { "Settings", &RmlUi::ShowSettings },
            { "Developer", &RmlUi::ShowDeveloper },
            { "Mods", &RmlUi::ShowMods },
            { "Inventory", &RmlUi::ShowInventory },
            { "Magic", &RmlUi::ShowMagic },
            { "Journal", &RmlUi::ShowJournal },
            { "ItemEdit", &RmlUi::ShowItemEdit }
        } };

        if (_rmlPrewarmStep >= kDocuments.size()) {
            _rmlPrewarmComplete = true;
            return;
        }

        const auto& document = kDocuments[_rmlPrewarmStep];
        const auto started = std::chrono::steady_clock::now();
        bool shown = false;
        bool rendered = false;
        try {
            shown = (_rmlUi.get()->*document.show)();
            if (shown) {
                rendered = _rmlUi->Render(
                    _panelRenderTarget,
                    static_cast<int>(_panelWidth),
                    static_cast<int>(_panelHeight),
                    kPanelLogicalWidth,
                    kPanelLogicalHeight);
            }
        } catch (const std::exception& error) {
            logger::warn(
                "DragonBoardVR: RmlUi prewarm for {} raised an exception: {}.",
                document.name,
                error.what());
        } catch (...) {
            logger::warn(
                "DragonBoardVR: RmlUi prewarm for {} raised an unknown exception.",
                document.name);
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        _rmlPrewarmTotalMs += elapsedMs;
        ++_rmlPrewarmFrameCount;
        logger::info(
            "DragonBoardVR: RmlUi prewarmed {} in {} ms (shown={}, rendered={}).",
            document.name,
            elapsedMs,
            shown,
            rendered);

        ++_rmlPrewarmStep;
        if (_rmlPrewarmStep >= kDocuments.size()) {
            _rmlPrewarmComplete = true;
            logger::info(
                "DragonBoardVR: RmlUi full prewarm completed across {} staged Present frames "
                "({} ms total work).",
                _rmlPrewarmFrameCount,
                _rmlPrewarmTotalMs);
        }
    }

    void RmlPanelHost::RenderPanel(float deltaTime)
    {
        if (!_visible.load() || !InitializeRenderer()) return;
        const auto& settings = vrui::VRUISettings::get();
        const auto requestedWidth = static_cast<std::uint32_t>(settings.rmlRenderWidth);
        const auto requestedHeight = static_cast<std::uint32_t>(settings.rmlRenderHeight);
        if ((_panelWidth != requestedWidth || _panelHeight != requestedHeight) &&
            !EnsureRenderTargetSizePresentThread(requestedWidth, requestedHeight)) {
            logger::warn(
                "DragonBoardVR: continuing with the existing RmlUi render size {}x{}.",
                _panelWidth,
                _panelHeight);
        }
        const float presentSeconds = std::clamp(deltaTime, 1.0f / 240.0f, 0.1f);
        _rmlRenderRateAccumulator += presentSeconds;
        if (_rmlRenderRateAccumulator >= 1.0f) {
            _rmlRendersPerSecond = static_cast<float>(_rmlRendersInRateWindow) /
                _rmlRenderRateAccumulator;
            _rmlRenderRateAccumulator = 0.0f;
            _rmlRendersInRateWindow = 0;
        }
        ApplyRenderCommandsPresentThread();
        _previewInteractionZoneHovered.store(false, std::memory_order_release);

        const auto panelMode = _localPanelMode.load();
        const bool settingsRmlActive = panelMode == LocalPanelMode::kSettings &&
            _rmlUi && _rmlUi->IsSettingsReady();
        const bool developerRmlActive = panelMode == LocalPanelMode::kDeveloper &&
            _rmlUi && _rmlUi->IsDeveloperReady();
        const bool itemEditRmlActive = panelMode == LocalPanelMode::kItemEdit &&
            _rmlUi && _rmlUi->IsItemEditReady();
        const bool modsRmlActive = panelMode == LocalPanelMode::kMods &&
            _rmlUi && _rmlUi->IsModsReady();
        const bool inventoryRmlActive = panelMode == LocalPanelMode::kInventory &&
            _rmlUi && _rmlUi->IsInventoryReady();
        const bool magicRmlActive = panelMode == LocalPanelMode::kMagic &&
            _rmlUi && _rmlUi->IsMagicReady();
        const bool journalRmlActive = panelMode == LocalPanelMode::kJournal &&
            _rmlUi && _rmlUi->IsJournalReady();
        const auto externalPanel = _activeExternalPanel.load();
        if (!_lastRmlPanelModePresentThread ||
            *_lastRmlPanelModePresentThread != panelMode ||
            (panelMode == LocalPanelMode::kExternal &&
             _lastRmlExternalPanelPresentThread != externalPanel)) {
            _renderScheduler.MarkDirty(RmlDirtyReason::kDocument);
            _lastRmlPanelModePresentThread = panelMode;
            _lastRmlExternalPanelPresentThread = externalPanel;
        }
        const bool externalRmlActive = panelMode == LocalPanelMode::kExternal &&
            _rmlUi && _rmlUi->IsPanelReady(externalPanel);
        if (settingsRmlActive || developerRmlActive || itemEditRmlActive ||
            modsRmlActive || inventoryRmlActive || magicRmlActive ||
            journalRmlActive ||
            externalRmlActive) {
            if (settingsRmlActive) {
                _rmlUi->ShowSettings();
                if (_rmlSettingsSyncPending.exchange(false)) {
                    SyncRmlSettingsFromDraft();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
            } else if (developerRmlActive) {
                _rmlUi->ShowDeveloper();
                if (_rmlDeveloperSyncPending.exchange(false)) {
                    SyncRmlDeveloperCommands();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
                _developerInfoPresentAccumulator += std::clamp(deltaTime, 0.0f, 0.1f);
                if (_rmlDeveloperInfoSyncPending.exchange(false) ||
                    _developerInfoPresentAccumulator >= 0.25f) {
                    _developerInfoPresentAccumulator = 0.0f;
                    SyncRmlDeveloperInfo();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
            } else if (itemEditRmlActive) {
                _rmlUi->ShowItemEdit();
                if (_rmlItemEditSyncPending.exchange(false)) {
                    SyncRmlItemEdit();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
            } else if (modsRmlActive) {
                _rmlUi->ShowMods();
                if (_rmlModsSyncPending.exchange(false)) {
                    SyncRmlMods();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
            } else if (inventoryRmlActive) {
                _rmlUi->ShowInventory();
                if (_rmlInventorySyncPending.exchange(false)) {
                    SyncRmlInventory();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
            } else if (magicRmlActive) {
                _rmlUi->ShowMagic();
                if (_rmlMagicSyncPending.exchange(false)) {
                    SyncRmlMagic();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
            } else if (journalRmlActive) {
                _rmlUi->ShowJournal();
                if (_rmlJournalSyncPending.exchange(false)) {
                    SyncRmlJournal();
                    _renderScheduler.MarkDirty(RmlDirtyReason::kData);
                }
            } else {
                _rmlUi->ShowPanel(externalPanel);
            }

            const bool pointerOnPanel = _pointerInHostedPanel.load();
            const float pointerU = _pointerU.load();
            const float pointerV = _pointerV.load();
            const bool triggerDown = _triggerDown.load();
            const bool gripDown = _gripDown.load();
            const float stickX = _stickX.load();
            const float stickY = _stickY.load();
            const bool pointerChanged = !_rmlInputStateInitialized ||
                pointerOnPanel != _lastRmlPointerOnPanel ||
                std::abs(pointerU - _lastRmlPointerU) >= (0.5f / static_cast<float>(kPanelLogicalWidth)) ||
                std::abs(pointerV - _lastRmlPointerV) >= (0.5f / static_cast<float>(kPanelLogicalHeight)) ||
                triggerDown != _lastRmlTriggerDown;
            const bool scrollChanged = !_rmlInputStateInitialized ||
                gripDown != _lastRmlGripDown ||
                std::abs(stickX - _lastRmlStickX) >= 0.01f ||
                std::abs(stickY - _lastRmlStickY) >= 0.01f;
            if (pointerChanged) _renderScheduler.MarkDirty(RmlDirtyReason::kPointer);
            if (scrollChanged) _renderScheduler.MarkDirty(RmlDirtyReason::kScroll);
            _rmlInputStateInitialized = true;
            _lastRmlPointerOnPanel = pointerOnPanel;
            _lastRmlPointerU = pointerU;
            _lastRmlPointerV = pointerV;
            _lastRmlTriggerDown = triggerDown;
            _lastRmlGripDown = gripDown;
            _lastRmlStickX = stickX;
            _lastRmlStickY = stickY;
            _rmlUi->ProcessInput(
                pointerOnPanel,
                pointerU,
                pointerV,
                triggerDown,
                gripDown,
                stickX,
                stickY,
                kPanelLogicalWidth,
                kPanelLogicalHeight,
                deltaTime);
            if (inventoryRmlActive || magicRmlActive) {
                _previewInteractionZoneHovered.store(
                    _rmlUi->IsPreviewInteractionZoneHovered(),
                    std::memory_order_release);
            }

            if (modsRmlActive) {
                const auto hovered = _rmlUi->GetHoveredModsIndex();
                _modsHoveredIndex.store(
                    hovered.value_or(static_cast<std::size_t>(-1)),
                    std::memory_order_release);
            } else {
                _modsHoveredIndex.store(static_cast<std::size_t>(-1), std::memory_order_release);
            }

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
                if (auto request = _rmlUi->ConsumeMapCalibrationRequest()) {
                    _mapCalibrationPointerU.store(request->pointerU, std::memory_order_release);
                    _mapCalibrationPointerV.store(request->pointerV, std::memory_order_release);
                    _mapCalibrationCityPending.store(request->cityIndex, std::memory_order_release);
                }
                if (_rmlUi->ConsumeMapCalibrationResetRequested()) {
                    _mapCalibrationResetPending.store(true, std::memory_order_release);
                }
                if (_rmlUi->ConsumeDeveloperAddCommandRequested()) {
                    if (!BeginDeveloperCommandKeyboardPresentThread()) {
                        _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                            dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError));
                    }
                }
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
            } else if (modsRmlActive) {
                const auto [action, index] = _rmlUi->ConsumeModsAction();
                using ModsAction = dragonboard::ui::rml::DragonBoardRmlUi::ModsAction;
                if (action == ModsAction::kAdd) _modsAddPending.store(true);
                else if (action == ModsAction::kClose) _modsClosePending.store(true);
                else if (action == ModsAction::kActivate) _modsActivatePending.store(index);
            } else if (inventoryRmlActive) {
                const auto [action, index] = _rmlUi->ConsumeInventoryAction();
                using RmlInventoryAction =
                    dragonboard::ui::rml::DragonBoardRmlUi::InventoryAction;
                switch (action) {
                case RmlInventoryAction::kSelect:
                    if (std::size_t inventoryIndex = 0;
                        TryMapInventoryVisibleIndex(index, inventoryIndex)) {
                        _inventoryActionIndex.store(inventoryIndex);
                        _inventoryActionPending.store(InventoryAction::kSelect);
                    }
                    break;
                case RmlInventoryAction::kEquip:
                    _inventoryActionLeftHand.store(
                        _lastTriggerWasLeft.load(std::memory_order_acquire),
                        std::memory_order_release);
                    _inventoryActionPending.store(InventoryAction::kEquip);
                    break;
                case RmlInventoryAction::kDrop:
                    _inventoryActionPending.store(InventoryAction::kDrop);
                    break;
                case RmlInventoryAction::kPin:
                    _inventoryActionPending.store(InventoryAction::kPin);
                    break;
                case RmlInventoryAction::kFavorite:
                    if (std::size_t inventoryIndex = 0;
                        TryMapInventoryVisibleIndex(index, inventoryIndex)) {
                        _inventoryActionIndex.store(inventoryIndex);
                        _inventoryActionPending.store(InventoryAction::kFavorite);
                    }
                    break;
                case RmlInventoryAction::kClose:
                    _inventoryActionPending.store(InventoryAction::kClose);
                    break;
                case RmlInventoryAction::kSearch:
                    if (!BeginInventorySearchKeyboardPresentThread()) {
                        _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                            dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError));
                    }
                    break;
                case RmlInventoryAction::kClearSearch:
                    ApplyInventorySearchQueryPresentThread({});
                    break;
                case RmlInventoryAction::kFilterWeapons:
                    _inventoryActionPending.store(InventoryAction::kFilterWeapons);
                    break;
                case RmlInventoryAction::kFilterArmor:
                    _inventoryActionPending.store(InventoryAction::kFilterArmor);
                    break;
                case RmlInventoryAction::kFilterConsumables:
                    _inventoryActionPending.store(InventoryAction::kFilterConsumables);
                    break;
                case RmlInventoryAction::kFilterQuest:
                    _inventoryActionPending.store(InventoryAction::kFilterQuest);
                    break;
                case RmlInventoryAction::kFilterBooks:
                    _inventoryActionPending.store(InventoryAction::kFilterBooks);
                    break;
                case RmlInventoryAction::kFilterMisc:
                    _inventoryActionPending.store(InventoryAction::kFilterMisc);
                    break;
                case RmlInventoryAction::kNone:
                    break;
                }
            } else if (magicRmlActive) {
                const auto [action, index] = _rmlUi->ConsumeMagicAction();
                using RmlMagicAction =
                    dragonboard::ui::rml::DragonBoardRmlUi::MagicAction;
                switch (action) {
                case RmlMagicAction::kSelect:
                    if (std::size_t magicIndex = 0;
                        TryMapMagicVisibleIndex(index, magicIndex)) {
                        _magicActionIndex.store(magicIndex);
                        _magicActionPending.store(MagicAction::kSelect);
                    }
                    break;
                case RmlMagicAction::kEquip:
                    _magicActionLeftHand.store(
                        _lastTriggerWasLeft.load(std::memory_order_acquire),
                        std::memory_order_release);
                    _magicActionPending.store(MagicAction::kEquip);
                    break;
                case RmlMagicAction::kEdit:
                    _magicActionPending.store(MagicAction::kEdit);
                    break;
                case RmlMagicAction::kPinDashboard:
                    _magicActionPending.store(MagicAction::kPinDashboard);
                    break;
                case RmlMagicAction::kPinLeftHand:
                    _magicActionPending.store(MagicAction::kPinLeftHand);
                    break;
                case RmlMagicAction::kPinWorld:
                    _magicActionPending.store(MagicAction::kPinWorld);
                    break;
                case RmlMagicAction::kToggleLabel:
                    _magicActionPending.store(MagicAction::kToggleLabel);
                    break;
                case RmlMagicAction::kFavorite:
                    if (std::size_t magicIndex = 0;
                        TryMapMagicVisibleIndex(index, magicIndex)) {
                        _magicActionIndex.store(magicIndex);
                        _magicActionPending.store(MagicAction::kFavorite);
                    }
                    break;
                case RmlMagicAction::kClose:
                    _magicActionPending.store(MagicAction::kClose);
                    break;
                case RmlMagicAction::kSearch:
                    if (!BeginMagicSearchKeyboardPresentThread()) {
                        _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                            dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError));
                    }
                    break;
                case RmlMagicAction::kClearSearch:
                    ApplyMagicSearchQueryPresentThread({});
                    break;
                case RmlMagicAction::kFilterDestruction:
                    _magicActionPending.store(MagicAction::kFilterDestruction);
                    break;
                case RmlMagicAction::kFilterConjuration:
                    _magicActionPending.store(MagicAction::kFilterConjuration);
                    break;
                case RmlMagicAction::kFilterRestoration:
                    _magicActionPending.store(MagicAction::kFilterRestoration);
                    break;
                case RmlMagicAction::kFilterIllusion:
                    _magicActionPending.store(MagicAction::kFilterIllusion);
                    break;
                case RmlMagicAction::kFilterAlteration:
                    _magicActionPending.store(MagicAction::kFilterAlteration);
                    break;
                case RmlMagicAction::kFilterPowers:
                    _magicActionPending.store(MagicAction::kFilterPowers);
                    break;
                case RmlMagicAction::kFilterPassive:
                    _magicActionPending.store(MagicAction::kFilterPassive);
                    break;
                case RmlMagicAction::kNone:
                    break;
                }
            } else if (journalRmlActive) {
                const auto request = _rmlUi->ConsumeJournalAction();
                const auto action = request.action;
                using RmlJournalAction =
                    dragonboard::ui::rml::DragonBoardRmlUi::JournalAction;
                switch (action) {
                case RmlJournalAction::kSelectQuest:
                    _journalActionFormID.store(request.formID);
                    _journalActionInstanceID.store(request.instanceID);
                    _journalActionPending.store(JournalAction::kSelectQuest);
                    break;
                case RmlJournalAction::kToggleTracking:
                    _journalActionFormID.store(request.formID);
                    _journalActionInstanceID.store(request.instanceID);
                    _journalActionPending.store(JournalAction::kToggleTracking);
                    break;
                case RmlJournalAction::kTrackObjective:
                    _journalActionFormID.store(request.formID);
                    _journalActionInstanceID.store(request.instanceID);
                    _journalActionObjectiveInstanceID.store(request.objectiveInstanceID);
                    _journalActionObjectiveID.store(request.objectiveID);
                    _journalActionPending.store(JournalAction::kTrackObjective);
                    break;
                case RmlJournalAction::kSettings:
                    _journalActionPending.store(JournalAction::kSettings);
                    break;
                case RmlJournalAction::kClose:
                    _journalActionPending.store(JournalAction::kClose);
                    break;
                case RmlJournalAction::kNone:
                    break;
                }
            } else {
                CollectExternalEventsPresentThread();
            }

            if (_rmlUi->ConsumeCloseRequested()) {
                Close();
                return;
            }
            if (settingsRmlActive && _rmlUi->ConsumeSaveRequested()) {
                _savePending.store(true);
            }

            const bool shouldRender = _renderScheduler.ShouldRender(
                deltaTime,
                _rmlUi->RequiresContinuousRendering());
            if (!shouldRender) {
                ++_rmlCachedFrames;
                return;
            }

            const bool rendered = _rmlUi->Render(
                _panelRenderTarget,
                static_cast<int>(_panelWidth),
                static_cast<int>(_panelHeight),
                kPanelLogicalWidth,
                kPanelLogicalHeight);
            if (rendered) {
                _renderScheduler.OnRendered();
                _rmlDirtyReason = _renderScheduler.DescribeLastRenderedReasons();
                ++_rmlRendersInRateWindow;
                const float frameSeconds =
                    std::clamp(deltaTime, 1.0f / 240.0f, 0.1f);
                if (_presentFrameTimeHistoryCount < _presentFrameTimeHistory.size()) {
                    _presentFrameTimeHistory[_presentFrameTimeHistoryIndex] = frameSeconds;
                    _presentFrameTimeHistorySum += frameSeconds;
                    ++_presentFrameTimeHistoryCount;
                } else {
                    _presentFrameTimeHistorySum -=
                        _presentFrameTimeHistory[_presentFrameTimeHistoryIndex];
                    _presentFrameTimeHistory[_presentFrameTimeHistoryIndex] = frameSeconds;
                    _presentFrameTimeHistorySum += frameSeconds;
                }
                _presentFrameTimeHistoryIndex =
                    (_presentFrameTimeHistoryIndex + 1) % _presentFrameTimeHistory.size();

                const float averageFrameSeconds =
                    _presentFrameTimeHistoryCount > 0 ?
                    _presentFrameTimeHistorySum /
                        static_cast<float>(_presentFrameTimeHistoryCount) :
                    0.0f;
                _presentFrameMs = averageFrameSeconds * 1000.0f;
                _presentFps = averageFrameSeconds > 0.0f ?
                    1.0f / averageFrameSeconds : 0.0f;
                _panelDrawCalls = _rmlUi->GetLastDrawCallCount();
                const auto& timing = _rmlUi->GetLastRenderTiming();
                auto& sample = _rmlPerformanceHistory[_rmlPerformanceHistoryIndex];
                sample.presentMs = frameSeconds * 1000.0f;
                sample.updateMs = timing.updateMs;
                sample.beginFrameMs = timing.beginFrameMs;
                sample.renderMs = timing.renderMs;
                sample.endFrameMs = timing.endFrameMs;
                sample.dx11StateMs = timing.beginFrameMs + timing.endFrameMs;
                sample.totalMs = timing.totalMs;
                _rmlPerformanceHistoryIndex =
                    (_rmlPerformanceHistoryIndex + 1) % _rmlPerformanceHistory.size();
                _rmlPerformanceHistoryCount = std::min(
                    _rmlPerformanceHistoryCount + 1,
                    _rmlPerformanceHistory.size());

                _rmlDomElements = timing.domElements;
                _rmlRenderWidth = timing.width;
                _rmlRenderHeight = timing.height;
                _rmlActiveDocument = timing.activeDocument;
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
        std::size_t selectedIndex = 0;
        {
            std::scoped_lock lock(_devMutex);
            commands.reserve(_devCommands.size());
            for (const auto& command : _devCommands) {
                commands.push_back({
                    command.label, command.command, command.description, command.dangerous });
            }
            selectedIndex = _selectedDevCommand >= 0 ?
                static_cast<std::size_t>(_selectedDevCommand) : 0;
        }
        _rmlUi->SetDeveloperCommands(std::move(commands), selectedIndex);
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
        const auto summarize = [this](auto member) {
            DragonBoardRmlUi::DeveloperInfo::TimingStats result;
            if (_rmlPerformanceHistoryCount == 0) return result;

            std::vector<float> values;
            values.reserve(_rmlPerformanceHistoryCount);
            float sum = 0.0f;
            for (std::size_t index = 0; index < _rmlPerformanceHistoryCount; ++index) {
                const float value = _rmlPerformanceHistory[index].*member;
                values.push_back(value);
                sum += value;
            }
            std::sort(values.begin(), values.end());
            const auto percentile = [&values](float percentileValue) {
                const auto rank = static_cast<std::size_t>(std::ceil(
                    percentileValue * static_cast<float>(values.size()))) - 1;
                return values[std::min(rank, values.size() - 1)];
            };
            const std::size_t lastIndex =
                (_rmlPerformanceHistoryIndex + _rmlPerformanceHistory.size() - 1) %
                _rmlPerformanceHistory.size();
            result.lastMs = _rmlPerformanceHistory[lastIndex].*member;
            result.averageMs = sum / static_cast<float>(_rmlPerformanceHistoryCount);
            result.p95Ms = percentile(0.95f);
            result.p99Ms = percentile(0.99f);
            return result;
        };
        info.present = summarize(&RmlPerformanceSample::presentMs);
        info.update = summarize(&RmlPerformanceSample::updateMs);
        info.beginFrame = summarize(&RmlPerformanceSample::beginFrameMs);
        info.render = summarize(&RmlPerformanceSample::renderMs);
        info.endFrame = summarize(&RmlPerformanceSample::endFrameMs);
        info.dx11State = summarize(&RmlPerformanceSample::dx11StateMs);
        info.total = summarize(&RmlPerformanceSample::totalMs);
        info.panelDrawCalls = _panelDrawCalls;
        info.domElements = _rmlDomElements;
        info.rendersPerSecond = _rmlRendersPerSecond;
        info.cachedFrames = _rmlCachedFrames;
        info.renderWidth = _rmlRenderWidth;
        info.renderHeight = _rmlRenderHeight;
        info.activeDocument = _rmlActiveDocument;
        info.dirtyReason = _rmlDirtyReason;
        info.pluginVersion = Plugin::VERSION.string();
        info.d3dFeatureLevel = _device ? static_cast<std::uint32_t>(_device->GetFeatureLevel()) : 0;
        info.playerX = snapshot.playerX;
        info.playerY = snapshot.playerY;
        info.playerZ = snapshot.playerZ;
        info.cellName = std::move(snapshot.cellName);
        info.cellFormId = snapshot.cellFormId;
        info.worldspaceName = std::move(snapshot.worldspaceName);
        info.worldspaceFormId = snapshot.worldspaceFormId;
        std::size_t validPoints = 0;
        for (std::size_t city = 0; city < snapshot.mapCalibrationPoints.size(); ++city) {
            const auto& point = snapshot.mapCalibrationPoints[city];
            if (point.valid) {
                ++validPoints;
                info.mapCalibrationStatus[city] = std::format(
                    "Saved ({:.0f}, {:.0f})", point.worldX, point.worldY);
            } else {
                info.mapCalibrationStatus[city] = "Not captured";
            }
        }
        vrui::MapCalibrationTransform calibration;
        if (vrui::FitMapCalibration(snapshot.mapCalibrationPoints, calibration) &&
            vrui::IsMapCalibrationUsable(calibration)) {
            info.mapCalibrationSummary = std::format(
                "{}/5 points captured - texture UV calibration active - RMS error {:.4f}",
                validPoints, calibration.rmsError);
        } else if (validPoints == vrui::kMapCalibrationPointCount) {
            info.mapCalibrationSummary = std::format(
                "5/5 points captured but inconsistent (UV RMS {:.4f}) - marker remains hidden",
                calibration.rmsError);
        } else {
            info.mapCalibrationSummary = std::format(
                "{}/5 points captured - all five are required before activation",
                validPoints);
        }
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

    void RmlPanelHost::SyncRmlMods()
    {
        if (!_rmlUi || !_rmlUi->IsModsReady()) return;
        std::vector<std::string> labels;
        {
            std::scoped_lock lock(_modsMutex);
            labels.reserve(_mods.size());
            for (const auto& mod : _mods) labels.push_back(mod.label);
        }
        _rmlUi->SetMods(labels);
    }

    void RmlPanelHost::SyncRmlInventory()
    {
        if (!_rmlUi || !_rmlUi->IsInventoryReady()) return;

        dragonboard::ui::rml::DragonBoardRmlUi::InventoryInfo info;
        {
            std::scoped_lock lock(_inventoryMutex);
            info.playerName = _inventoryPlayerName;
            info.playerLevel = _inventoryPlayerLevel;
            info.gold = _inventoryGold;
            info.currentWeight = _inventoryCurrentWeight;
            info.carryWeight = _inventoryCarryWeight;
            info.activeFilter = _inventoryActiveFilter;
            info.searchQuery = _inventorySearchQuery;
            _inventoryVisibleIndices.clear();
            info.items.reserve(_inventoryItems.size());
            bool selectedVisible = false;
            for (std::size_t inventoryIndex = 0;
                 inventoryIndex < _inventoryItems.size();
                 ++inventoryIndex) {
                const auto& entry = _inventoryItems[inventoryIndex];
                if (!InventoryEntryMatchesSearch(entry, _inventorySearchQuery)) continue;
                const auto visibleIndex = info.items.size();
                _inventoryVisibleIndices.push_back(inventoryIndex);
                if (inventoryIndex == _inventorySelectedIndex) {
                    info.selectedIndex = visibleIndex;
                    selectedVisible = true;
                }
                dragonboard::ui::rml::DragonBoardRmlUi::InventoryItemInfo item;
                item.name = entry.name;
                item.category = entry.category;
                item.description = entry.description;
                item.equipmentMarker = entry.equipmentMarker;
                item.equipmentState = entry.equipmentState;
                item.formID = entry.formID;
                item.count = entry.count;
                item.attack = entry.attack;
                item.defense = entry.defense;
                item.weight = entry.weight;
                item.value = entry.value;
                item.hasAttack = entry.hasAttack;
                item.hasDefense = entry.hasDefense;
                item.equipped = entry.equipped;
                item.equippedLeft = entry.equippedLeft;
                item.equippedRight = entry.equippedRight;
                item.favorited = entry.favorited;
                item.canEquip = entry.canEquip;
                info.items.push_back(std::move(item));
            }
            if (!selectedVisible) info.selectedIndex = 0;
        }
        _rmlUi->SetInventory(std::move(info));
    }

    void RmlPanelHost::SyncRmlMagic()
    {
        if (!_rmlUi || !_rmlUi->IsMagicReady()) return;

        dragonboard::ui::rml::DragonBoardRmlUi::MagicInfo info;
        {
            std::scoped_lock lock(_magicMutex);
            info.playerName = _magicPlayerName;
            info.playerLevel = _magicPlayerLevel;
            info.currentMagicka = _magicCurrentMagicka;
            info.maximumMagicka = _magicMaximumMagicka;
            info.activeFilter = _magicActiveFilter;
            info.searchQuery = _magicSearchQuery;
            info.editModeEnabled = vrui::VRUISettings::get().editModeEnabled;
            _magicVisibleIndices.clear();
            info.items.reserve(_magicItems.size());
            bool selectedVisible = false;
            for (std::size_t magicIndex = 0;
                 magicIndex < _magicItems.size();
                 ++magicIndex) {
                const auto& entry = _magicItems[magicIndex];
                if (!MagicEntryMatchesSearch(entry, _magicSearchQuery)) continue;
                const auto visibleIndex = info.items.size();
                _magicVisibleIndices.push_back(magicIndex);
                if (magicIndex == _magicSelectedIndex) {
                    info.selectedIndex = visibleIndex;
                    selectedVisible = true;
                }
                dragonboard::ui::rml::DragonBoardRmlUi::MagicItemInfo item;
                item.name = entry.name;
                item.category = entry.category;
                item.description = entry.description;
                item.iconPath = entry.iconPath;
                item.castingType = entry.castingType;
                item.delivery = entry.delivery;
                item.skillLevel = entry.skillLevel;
                item.duration = entry.duration;
                item.range = entry.range;
                item.formID = entry.formID;
                item.magickaCost = entry.magickaCost;
                item.equipped = entry.equipped;
                item.equippedLeft = entry.equippedLeft;
                item.equippedRight = entry.equippedRight;
                item.favorited = entry.favorited;
                item.canEquip = entry.canEquip;
                item.hasModelPreview = !entry.modelPath.empty();
                info.items.push_back(std::move(item));
            }
            if (!selectedVisible) info.selectedIndex = 0;
        }
        _rmlUi->SetMagic(std::move(info));
    }

    void RmlPanelHost::SyncRmlJournal()
    {
        if (!_rmlUi || !_rmlUi->IsJournalReady()) return;

        dragonboard::ui::rml::DragonBoardRmlUi::JournalInfo info;
        {
            std::scoped_lock lock(_journalMutex);
            info.playerName = _journalPlayerName;
            info.playerLevel = _journalPlayerLevel;
            info.selectedIndex = _journalSelectedIndex;
            info.quests.reserve(_journalQuests.size());
            for (const auto& entry : _journalQuests) {
                dragonboard::ui::rml::DragonBoardRmlUi::JournalQuestInfo quest;
                quest.formID = entry.formID;
                quest.instanceID = entry.instanceID;
                quest.title = entry.title;
                quest.summary = entry.summary;
                quest.type = entry.type;
                quest.active = entry.active;
                quest.completed = entry.completed;
                quest.failed = entry.failed;
                quest.objectives.reserve(entry.objectives.size());
                for (const auto& objectiveEntry : entry.objectives) {
                    dragonboard::ui::rml::DragonBoardRmlUi::JournalObjectiveInfo objective;
                    objective.objectiveID = objectiveEntry.objectiveID;
                    objective.instanceID = objectiveEntry.instanceID;
                    objective.text = objectiveEntry.text;
                    objective.state = objectiveEntry.state;
                    objective.completed = objectiveEntry.completed;
                    objective.failed = objectiveEntry.failed;
                    objective.hasTargets = objectiveEntry.hasTargets;
                    quest.objectives.push_back(std::move(objective));
                }
                info.quests.push_back(std::move(quest));
            }

            const auto copyStats = [](
                                       const std::vector<JournalStatEntry>& source,
                                       std::vector<dragonboard::ui::rml::DragonBoardRmlUi::JournalStatInfo>& target) {
                target.reserve(source.size());
                for (const auto& entry : source) {
                    target.push_back({ entry.label, entry.value });
                }
            };
            copyStats(_journalCharacterStats, info.characterStats);
            copyStats(_journalSkills, info.skills);
            copyStats(_journalGeneralStats, info.generalStats);
        }
        _rmlUi->SetJournal(info);
    }

    void RmlPanelHost::CaptureJournalGameThread(bool preserveSelection)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        std::uint32_t selectedFormID = 0;
        std::uint32_t selectedInstanceID = 0;
        if (preserveSelection) {
            std::scoped_lock lock(_journalMutex);
            selectedFormID = _journalSelectedFormID;
            selectedInstanceID = _journalSelectedInstanceID;
        }

        std::vector<JournalQuestEntry> quests;
        std::unordered_map<std::uint64_t, std::size_t> questIndices;
        const auto isCurrentJournalQuest = [](const RE::TESQuest* quest) {
            return quest && quest->IsEnabled() && !quest->IsStopped() &&
                   !quest->IsCompleted() &&
                   quest->data.flags.none(RE::QuestFlag::kFailed);
        };
        const auto ensureQuest =
            [&](RE::TESQuest* quest, std::uint32_t instanceID) -> JournalQuestEntry& {
            const auto key = JournalQuestKey(quest->GetFormID(), instanceID);
            if (const auto found = questIndices.find(key); found != questIndices.end()) {
                return quests[found->second];
            }

            JournalQuestEntry entry;
            entry.formID = quest->GetFormID();
            entry.instanceID = instanceID;
            entry.title = ResolveQuestObjectiveText(
                quest,
                instanceID,
                quest->GetName());
            if (entry.title.empty()) {
                entry.title = std::format("Quest {:08X}", entry.formID);
            }
            entry.summary = ResolveQuestObjectiveText(
                quest,
                instanceID,
                ResolveJournalText(quest, instanceID));
            entry.type = QuestTypeLabel(quest->GetType());
            entry.active = quest->IsActive();
            entry.completed = quest->IsCompleted();
            entry.failed = quest->data.flags.all(RE::QuestFlag::kFailed);
            questIndices.emplace(key, quests.size());
            quests.push_back(std::move(entry));
            return quests.back();
        };

#ifdef ENABLE_SKYRIM_VR
        auto& objectives = GetVrQuestObjectives(player);
#else
        auto& runtime = player->GetPlayerRuntimeData();
        auto& objectives = runtime.objectives;
#endif
        if (objectives.size() > 4096) {
            logger::error(
                "DragonBoardVR: refusing invalid VR journal objective array size {}.",
                objectives.size());
            return;
        }

        std::unordered_map<std::uint32_t, std::uint32_t> observedInstances;
        observedInstances.reserve(objectives.size());
        for (const auto& instanced : objectives) {
            if (instanced.Objective && instanced.Objective->ownerQuest) {
                observedInstances.insert_or_assign(
                    instanced.Objective->ownerQuest->GetFormID(),
                    instanced.instanceID);
            }
        }

#ifdef ENABLE_SKYRIM_VR
        auto& questLog = GetVrQuestLog(player);
#else
        auto& questLog = runtime.questLog;
#endif
        std::size_t questLogEntries = 0;
        for (auto* stageItem : questLog) {
            if (++questLogEntries > 4096) {
                logger::error(
                    "DragonBoardVR: refusing VR journal quest log with more than 4096 entries.");
                break;
            }
            // The native objective array remains the authoritative Journal
            // source. questLog is historical; use it only for the narrow case
            // of a currently tracked quest that has not published an objective
            // yet. This prevents enabled radiant/template quests from leaking
            // into the RmlUi list after their log updates.
            if (!stageItem || !stageItem->hasLogEntry ||
                !isCurrentJournalQuest(stageItem->owner) ||
                !stageItem->owner->IsActive()) {
                continue;
            }

            auto* quest = stageItem->owner;
            const auto observed = observedInstances.find(quest->GetFormID());
            const auto instanceID = observed != observedInstances.end() ?
                                        observed->second :
                                        quest->currentInstanceID;
            (void)ensureQuest(quest, instanceID);
        }

        for (const auto& instanced : objectives) {
            auto* objective = instanced.Objective;
            if (!objective || !objective->ownerQuest ||
                !isCurrentJournalQuest(objective->ownerQuest) ||
                instanced.InstanceState == RE::QUEST_OBJECTIVE_STATE::kDormant) {
                continue;
            }

            auto& quest = ensureQuest(objective->ownerQuest, instanced.instanceID);
            JournalObjectiveEntry entry;
            entry.objectiveID = objective->index;
            entry.instanceID = instanced.instanceID;
            entry.text = ResolveQuestObjectiveText(
                objective->ownerQuest,
                instanced.instanceID,
                objective->displayText.c_str());
            if (entry.text.empty()) {
                entry.text = std::format("Objective {}", objective->index);
            }
            entry.state = ObjectiveStateLabel(instanced.InstanceState);
            entry.completed = ObjectiveCompleted(instanced.InstanceState);
            entry.failed = ObjectiveFailed(instanced.InstanceState);
            entry.hasTargets = objective->numTargets > 0;
            quest.objectives.push_back(std::move(entry));
        }

        // Several radiant/template quests can publish separate objective
        // records with the same user-facing Journal title. The native Journal
        // consolidates that presentation; keep one stable representative here,
        // preferring the tracked entry and then the one with current objectives.
        std::vector<JournalQuestEntry> uniqueQuests;
        uniqueQuests.reserve(quests.size());
        std::unordered_map<std::string, std::size_t> titleIndices;
        for (auto& quest : quests) {
            auto titleKey = quest.title;
            std::ranges::transform(
                titleKey,
                titleKey.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            const auto score = [](const JournalQuestEntry& candidate) {
                return (candidate.active ? 4 : 0) +
                       (!candidate.objectives.empty() ? 2 : 0) +
                       (!candidate.completed && !candidate.failed ? 1 : 0);
            };
            if (const auto found = titleIndices.find(titleKey);
                found != titleIndices.end()) {
                auto& retained = uniqueQuests[found->second];
                if (score(quest) > score(retained)) retained = std::move(quest);
                continue;
            }
            titleIndices.emplace(std::move(titleKey), uniqueQuests.size());
            uniqueQuests.push_back(std::move(quest));
        }
        quests = std::move(uniqueQuests);

        for (auto& quest : quests) {
            std::stable_sort(
                quest.objectives.begin(),
                quest.objectives.end(),
                [](const JournalObjectiveEntry& left, const JournalObjectiveEntry& right) {
                    if (left.completed != right.completed) return !left.completed;
                    if (left.failed != right.failed) return !left.failed;
                    return left.objectiveID < right.objectiveID;
                });
        }
        std::stable_sort(
            quests.begin(),
            quests.end(),
            [](const JournalQuestEntry& left, const JournalQuestEntry& right) {
                if (left.active != right.active) return left.active;
                if (left.completed != right.completed) return !left.completed;
                if (left.failed != right.failed) return !left.failed;
                return left.title < right.title;
            });

        std::size_t selectedIndex = 0;
        if (preserveSelection && selectedFormID != 0) {
            const auto selected = std::find_if(
                quests.begin(),
                quests.end(),
                [&](const JournalQuestEntry& quest) {
                    return quest.formID == selectedFormID &&
                           quest.instanceID == selectedInstanceID;
                });
            if (selected != quests.end()) {
                selectedIndex = static_cast<std::size_t>(
                    std::distance(quests.begin(), selected));
            }
        }

        const auto formatValue = [](float value) {
            return std::format("{:.0f}", std::max(0.0f, value));
        };
        const auto maximumActorValue = [player](RE::ActorValue actorValue) {
            return std::max(
                player->GetActorValue(actorValue),
                player->GetPermanentActorValue(actorValue) +
                    player->GetActorValueModifier(
                        RE::ACTOR_VALUE_MODIFIER::kTemporary,
                        actorValue));
        };
        std::vector<JournalStatEntry> characterStats{
            { "Level", std::to_string(player->GetLevel()) },
            { "Health",
              std::format(
                  "{} / {}",
                  formatValue(player->GetActorValue(RE::ActorValue::kHealth)),
                  formatValue(maximumActorValue(RE::ActorValue::kHealth))) },
            { "Magicka",
              std::format(
                  "{} / {}",
                  formatValue(player->GetActorValue(RE::ActorValue::kMagicka)),
                  formatValue(maximumActorValue(RE::ActorValue::kMagicka))) },
            { "Stamina",
              std::format(
                  "{} / {}",
                  formatValue(player->GetActorValue(RE::ActorValue::kStamina)),
                  formatValue(maximumActorValue(RE::ActorValue::kStamina))) }
        };

        static constexpr std::array<const char*, 18> skillNames{
            "One-Handed", "Two-Handed", "Archery", "Block", "Smithing",
            "Heavy Armor", "Light Armor", "Pickpocket", "Lockpicking", "Sneak",
            "Alchemy", "Speech", "Alteration", "Conjuration", "Destruction",
            "Illusion", "Restoration", "Enchanting"
        };
        std::vector<JournalStatEntry> skills;
        if (auto* infoRuntime = player->GetVRInfoRuntimeData();
            infoRuntime && infoRuntime->skills && infoRuntime->skills->data) {
            const auto& skillData = *infoRuntime->skills->data;
            skills.reserve(skillNames.size());
            for (std::size_t index = 0; index < skillNames.size(); ++index) {
                const auto& skill = skillData.skills[index];
                auto value = std::format("{:.0f}", skill.level);
                if (skill.levelThreshold > 0.0f) {
                    value += std::format(
                        "  {:.0f}/{:.0f} XP",
                        skill.xp,
                        skill.levelThreshold);
                }
                if (skillData.legendaryLevels[index] > 0) {
                    value += std::format(
                        "  Legendary {}",
                        skillData.legendaryLevels[index]);
                }
                skills.push_back({ skillNames[index], std::move(value) });
            }
        }

        std::size_t trackedQuests = 0;
        std::size_t completedQuests = 0;
        std::size_t activeObjectives = 0;
        std::size_t completedObjectives = 0;
        for (const auto& quest : quests) {
            trackedQuests += quest.active ? 1 : 0;
            completedQuests += quest.completed ? 1 : 0;
            for (const auto& objective : quest.objectives) {
                completedObjectives += objective.completed ? 1 : 0;
                activeObjectives +=
                    !objective.completed && !objective.failed ? 1 : 0;
            }
        }
        std::vector<JournalStatEntry> generalStats{
            { "Journal Quests", std::to_string(quests.size()) },
            { "Tracked Quests", std::to_string(trackedQuests) },
            { "Completed Quests", std::to_string(completedQuests) },
            { "Active Objectives", std::to_string(activeObjectives) },
            { "Completed Objectives", std::to_string(completedObjectives) }
        };

        const auto signature = HashJournalState(quests);
        {
            std::scoped_lock lock(_journalMutex);
            _journalQuests = std::move(quests);
            _journalSelectedIndex =
                _journalQuests.empty() ? 0 :
                std::min(selectedIndex, _journalQuests.size() - 1);
            if (_journalQuests.empty()) {
                _journalSelectedFormID = 0;
                _journalSelectedInstanceID = 0;
            } else {
                _journalSelectedFormID = _journalQuests[_journalSelectedIndex].formID;
                _journalSelectedInstanceID =
                    _journalQuests[_journalSelectedIndex].instanceID;
            }
            _journalPlayerName = player->GetName();
            if (_journalPlayerName.empty()) _journalPlayerName = "Dragonborn";
            _journalPlayerLevel = player->GetLevel();
            _journalCharacterStats = std::move(characterStats);
            _journalSkills = std::move(skills);
            _journalGeneralStats = std::move(generalStats);
            _journalStateSignature = signature;
        }
        _rmlJournalSyncPending.store(true);
    }

    bool RmlPanelHost::SetQuestTrackedGameThread(
        std::uint32_t formID,
        bool tracked)
    {
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(formID);
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!quest || !vm) return false;

        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) return false;
        const auto handle = policy->GetHandleForObject(RE::FormType::Quest, quest);
        if (handle == policy->EmptyHandle()) return false;

        auto* args = RE::MakeFunctionArguments(static_cast<bool>(tracked));
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
        const bool dispatched =
            vm->DispatchMethodCall(handle, "Quest", "SetActive", args, callback);
        if (dispatched) {
            logger::info(
                "DragonBoardVR: quest {:08X} tracking set to {}.",
                formID,
                tracked);
        } else {
            logger::warn(
                "DragonBoardVR: failed to dispatch Quest.SetActive for {:08X}.",
                formID);
        }
        return dispatched;
    }

    bool RmlPanelHost::CacheQuestObjectiveTargetGameThread(
        std::uint32_t formID,
        std::uint32_t instanceID,
        std::uint16_t objectiveID)
    {
        _questMovingTargetHandle.reset();
        _questMovingTargetPollAccumulator = 0.0f;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(formID);
        if (!player || !quest) {
            vrui::VRUIMapMarker::ClearQuestObjectivePosition();
            return false;
        }

#ifdef ENABLE_SKYRIM_VR
        auto& objectives = GetVrQuestObjectives(player);
#else
        auto& objectives = player->GetPlayerRuntimeData().objectives;
#endif
        RE::BGSQuestObjective* selectedObjective = nullptr;
        for (const auto& instanced : objectives) {
            auto* objective = instanced.Objective;
            if (objective && objective->ownerQuest == quest &&
                objective->index == objectiveID &&
                instanced.instanceID == instanceID) {
                selectedObjective = objective;
                break;
            }
        }

        if (!selectedObjective || !selectedObjective->targets ||
            selectedObjective->numTargets == 0 || selectedObjective->numTargets > 128) {
            logger::warn(
                "DragonBoardVR: objective target cache rejected quest={:08X} instance={} objective={}; no valid target array.",
                formID,
                instanceID,
                objectiveID);
            vrui::VRUIMapMarker::ClearQuestObjectivePosition();
            return false;
        }

        // This action runs on Skyrim's game thread after a short delay. Read
        // the teleport path already stored in the target by the game's compass
        // update, then copy only an exterior door position into the render cache.
        // No relocation and no PlayerCharacter::questTargets lock are used.
        std::uint32_t unresolvedTargets = 0;
        std::uint32_t interiorTargets = 0;
        std::uint32_t teleportPathLinks = 0;
        for (std::uint32_t targetIndex = 0;
             targetIndex < selectedObjective->numTargets;
             ++targetIndex) {
            auto* target = selectedObjective->targets[targetIndex];
            if (!target) continue;

            const auto aliasID = static_cast<std::uint32_t>(target->alias);
            const auto cacheExteriorReference = [&](RE::TESObjectREFR* reference,
                                                    std::string_view source) {
                if (!reference || !reference->parentCell ||
                    reference->parentCell->cellFlags.any(
                        RE::TESObjectCELL::Flag::kIsInteriorCell)) {
                    return false;
                }
                const auto position = reference->GetPosition();
                vrui::VRUIMapMarker::SetQuestObjectivePosition(
                    formID,
                    reference->GetFormID(),
                    position);
                logger::info(
                    "DragonBoardVR: cached objective {} quest={:08X} instance={} objective={} alias={} target={:08X} world=({:.1f}, {:.1f}, {:.1f}).",
                    source,
                    formID,
                    instanceID,
                    objectiveID,
                    aliasID,
                    reference->GetFormID(),
                    position.x,
                    position.y,
                    position.z);
                return true;
            };

#ifdef ENABLE_SKYRIM_VR
            const auto* teleportPath =
                reinterpret_cast<const QuestTargetTeleportPathView*>(
                    reinterpret_cast<const std::byte*>(target) + 0x18);
            const auto linkCount = teleportPath->teleportLinks.size();
            if (linkCount <= 64) {
                teleportPathLinks += static_cast<std::uint32_t>(linkCount);
                for (const auto& link : teleportPath->teleportLinks) {
                    if (cacheExteriorReference(link.reference, "teleport-path entrance")) {
                        return true;
                    }
                    if (link.reference) {
                        const auto linkedDoor =
                            link.reference->extraList.GetTeleportLinkedDoor().get();
                        if (cacheExteriorReference(
                                linkedDoor.get(), "teleport-path linked entrance")) {
                            return true;
                        }
                    }
                }
            }
#endif

            RE::ObjectRefHandle targetHandle{};
            quest->CreateRefHandleByAliasID(targetHandle, aliasID);
            const auto targetReference = targetHandle.get();
            if (!targetReference || !targetReference->parentCell) {
                ++unresolvedTargets;
                continue;
            }
            if (targetReference->As<RE::Actor>() &&
                cacheExteriorReference(targetReference.get(), "moving actor target")) {
                _questMovingTargetHandle = targetHandle;
                _questMovingTargetPollAccumulator = 0.0f;
                logger::info(
                    "DragonBoardVR: enabled 1-second moving quest target updates for actor {:08X}.",
                    targetReference->GetFormID());
                return true;
            }
            if (cacheExteriorReference(targetReference.get(), "exterior target")) {
                return true;
            }

            ++interiorTargets;
            const auto linkedDoor =
                targetReference->extraList.GetTeleportLinkedDoor().get();
            if (cacheExteriorReference(linkedDoor.get(), "target linked entrance")) {
                return true;
            }
        }

        logger::warn(
            "DragonBoardVR: objective target cache found no exterior reference for quest={:08X} instance={} objective={} targets={} unresolved={} interior={} pathLinks={}.",
            formID,
            instanceID,
            objectiveID,
            selectedObjective->numTargets,
            unresolvedTargets,
            interiorTargets,
            teleportPathLinks);
        vrui::VRUIMapMarker::ClearQuestObjectivePosition();
        return false;
    }

    void RmlPanelHost::RefreshMovingQuestTargetGameThread()
    {
        if (!_questMovingTargetHandle || _questMarkerWatchFormID == 0) {
            _questMovingTargetHandle.reset();
            return;
        }

        const auto reference = _questMovingTargetHandle.get();
        auto* actor = reference ? reference->As<RE::Actor>() : nullptr;
        const bool interior = reference && reference->parentCell &&
            reference->parentCell->cellFlags.any(
                RE::TESObjectCELL::Flag::kIsInteriorCell);
        if (!actor || !reference->parentCell || interior) {
            const auto targetFormID = reference ? reference->GetFormID() : 0;
            _questMovingTargetHandle.reset();
            _questMovingTargetPollAccumulator = 0.0f;
            _questTargetResolveFormID = _questMarkerWatchFormID;
            _questTargetResolveInstanceID = _questMarkerWatchObjectiveInstanceID;
            _questTargetResolveObjectiveID = _questMarkerWatchObjectiveID;
            _questTargetResolveAttempts = 4;
            _questTargetResolveDelay = 0.25f;
            logger::info(
                "DragonBoardVR: moving quest target {:08X} became unavailable or interior; scheduled entrance resolution.",
                targetFormID);
            return;
        }

        vrui::VRUIMapMarker::SetQuestObjectivePosition(
            _questMarkerWatchFormID,
            reference->GetFormID(),
            reference->GetPosition());
    }

    void RmlPanelHost::PersistQuestMarkerSelectionGameThread()
    {
        auto& settings = vrui::VRUISettings::get();
        settings.questMarkerLastFormID = _questMarkerWatchFormID;
        settings.questMarkerLastQuestInstanceID = _questMarkerWatchQuestInstanceID;
        settings.questMarkerLastObjectiveInstanceID =
            _questMarkerWatchObjectiveInstanceID;
        settings.questMarkerLastObjectiveID = _questMarkerWatchObjectiveID;
        settings.save(vrui::VRUISettings::getDefaultIniPath());
    }

    bool RmlPanelHost::RestoreQuestMarkerGameThread()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto& settings = vrui::VRUISettings::get();
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(
            settings.questMarkerLastFormID);
        if (!player || !quest || !quest->IsActive()) return false;

#ifdef ENABLE_SKYRIM_VR
        auto& objectives = GetVrQuestObjectives(player);
#else
        auto& objectives = player->GetPlayerRuntimeData().objectives;
#endif
        if (objectives.size() > 4096) return false;

        const RE::BGSInstancedQuestObjective* exact = nullptr;
        const RE::BGSInstancedQuestObjective* newest = nullptr;
        for (const auto& instanced : objectives) {
            auto* objective = instanced.Objective;
            if (!objective || objective->ownerQuest != quest || objective->numTargets == 0 ||
                instanced.InstanceState == RE::QUEST_OBJECTIVE_STATE::kDormant ||
                ObjectiveCompleted(instanced.InstanceState) ||
                ObjectiveFailed(instanced.InstanceState)) {
                continue;
            }
            if (objective->index == settings.questMarkerLastObjectiveID &&
                instanced.instanceID == settings.questMarkerLastObjectiveInstanceID) {
                exact = std::addressof(instanced);
            }
            if (!newest || objective->index > newest->Objective->index) {
                newest = std::addressof(instanced);
            }
        }

        const auto* restored = exact ? exact : newest;
        if (!restored) return false;

        _questMarkerWatchFormID = settings.questMarkerLastFormID;
        _questMarkerWatchQuestInstanceID = settings.questMarkerLastQuestInstanceID;
        _questMarkerWatchObjectiveInstanceID = restored->instanceID;
        _questMarkerWatchObjectiveID = restored->Objective->index;
        _questMarkerPollAccumulator = 0.0f;
        _questMovingTargetHandle.reset();
        _questMovingTargetPollAccumulator = 0.0f;
        _questTargetResolveFormID = _questMarkerWatchFormID;
        _questTargetResolveInstanceID = restored->instanceID;
        _questTargetResolveObjectiveID = restored->Objective->index;
        _questTargetResolveAttempts = 4;
        _questTargetResolveDelay = 0.25f;
        PersistQuestMarkerSelectionGameThread();
        logger::info(
            "DragonBoardVR: restored persisted quest marker quest={:08X} questInstance={} objectiveInstance={} objective={}.",
            _questMarkerWatchFormID,
            _questMarkerWatchQuestInstanceID,
            _questMarkerWatchObjectiveInstanceID,
            _questMarkerWatchObjectiveID);
        return true;
    }

    void RmlPanelHost::RefreshTrackedQuestObjectiveGameThread()
    {
        if (_questMarkerWatchFormID == 0) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(_questMarkerWatchFormID);
        if (!player || !quest || !quest->IsActive()) {
            logger::info(
                "DragonBoardVR: stopped automatic objective marker updates for quest {:08X}; quest is no longer tracked.",
                _questMarkerWatchFormID);
            _questMarkerWatchFormID = 0;
            _questMarkerWatchQuestInstanceID = 0;
            _questMarkerWatchObjectiveInstanceID = 0;
            _questMarkerWatchObjectiveID = 0;
            _questMovingTargetHandle.reset();
            _questMovingTargetPollAccumulator = 0.0f;
            _questTargetResolveDelay = -1.0f;
            _questTargetResolveAttempts = 0;
            vrui::VRUIMapMarker::ClearQuestObjectivePosition();
            PersistQuestMarkerSelectionGameThread();
            return;
        }

#ifdef ENABLE_SKYRIM_VR
        auto& objectives = GetVrQuestObjectives(player);
#else
        auto& objectives = player->GetPlayerRuntimeData().objectives;
#endif
        if (objectives.size() > 4096) return;

        const RE::BGSInstancedQuestObjective* current = nullptr;
        const RE::BGSInstancedQuestObjective* replacement = nullptr;
        for (const auto& instanced : objectives) {
            auto* objective = instanced.Objective;
            if (!objective || objective->ownerQuest != quest || objective->numTargets == 0 ||
                instanced.InstanceState == RE::QUEST_OBJECTIVE_STATE::kDormant ||
                ObjectiveCompleted(instanced.InstanceState) ||
                ObjectiveFailed(instanced.InstanceState)) {
                continue;
            }

            if (objective->index == _questMarkerWatchObjectiveID &&
                instanced.instanceID == _questMarkerWatchObjectiveInstanceID) {
                current = std::addressof(instanced);
            }
            if (!replacement || objective->index > replacement->Objective->index) {
                replacement = std::addressof(instanced);
            }
        }

        // Skyrim can publish the next stage before retiring the previous one.
        // Advance when a newer objective exists; otherwise preserve the user's
        // current choice while it remains active.
        if (current && replacement &&
            replacement->Objective->index <= current->Objective->index) {
            return;
        }

        if (!replacement) {
            if (_questMarkerWatchObjectiveID != 0) {
                logger::info(
                    "DragonBoardVR: tracked quest {:08X} currently has no active map objective; marker hidden until the next update.",
                    _questMarkerWatchFormID);
                _questMarkerWatchObjectiveID = 0;
                _questMovingTargetHandle.reset();
                _questMovingTargetPollAccumulator = 0.0f;
                _questMarkerWatchObjectiveInstanceID = 0;
                _questTargetResolveDelay = -1.0f;
                _questTargetResolveAttempts = 0;
                vrui::VRUIMapMarker::ClearQuestObjectivePosition();
            }
            return;
        }

        _questMarkerWatchObjectiveID = replacement->Objective->index;
        _questMarkerWatchObjectiveInstanceID = replacement->instanceID;
        _questMovingTargetHandle.reset();
        _questMovingTargetPollAccumulator = 0.0f;
        _questTargetResolveFormID = _questMarkerWatchFormID;
        _questTargetResolveInstanceID = replacement->instanceID;
        _questTargetResolveObjectiveID = replacement->Objective->index;
        _questTargetResolveAttempts = 4;
        _questTargetResolveDelay = 0.25f;
        PersistQuestMarkerSelectionGameThread();
        vrui::VRUIMapMarker::ClearQuestObjectivePosition();
        logger::info(
            "DragonBoardVR: objective update detected; scheduled marker refresh quest={:08X} questInstance={} objectiveInstance={} objective={}.",
            _questMarkerWatchFormID,
            _questMarkerWatchQuestInstanceID,
            replacement->instanceID,
            replacement->Objective->index);
    }

    void RmlPanelHost::ExecuteJournalActionGameThread(
        JournalAction action,
        std::uint32_t formID,
        std::uint32_t instanceID,
        std::uint32_t objectiveInstanceID,
        std::uint16_t objectiveID)
    {
        if (action == JournalAction::kSettings) {
            (void)OpenSettings();
            return;
        }
        if (action == JournalAction::kClose) {
            Close();
            return;
        }

        bool tracked = false;
        {
            std::scoped_lock lock(_journalMutex);
            const auto selected = std::find_if(
                _journalQuests.begin(),
                _journalQuests.end(),
                [&](const JournalQuestEntry& quest) {
                    return quest.formID == formID && quest.instanceID == instanceID;
                });
            if (selected == _journalQuests.end()) {
                logger::warn(
                    "DragonBoardVR: ignored stale journal action {} for quest={:08X} instance={}.",
                    static_cast<unsigned>(action),
                    formID,
                    instanceID);
                return;
            }

            if (action == JournalAction::kSelectQuest) {
                _journalSelectedIndex = static_cast<std::size_t>(
                    std::distance(_journalQuests.begin(), selected));
                _journalSelectedFormID = selected->formID;
                _journalSelectedInstanceID = selected->instanceID;
                _rmlJournalSyncPending.store(true);
                return;
            }

            tracked = selected->active;
            if (action == JournalAction::kTrackObjective) {
                const auto objective = std::find_if(
                    selected->objectives.begin(),
                    selected->objectives.end(),
                    [&](const JournalObjectiveEntry& entry) {
                        return entry.objectiveID == objectiveID &&
                               entry.instanceID == objectiveInstanceID;
                    });
                if (objective == selected->objectives.end()) {
                    logger::warn(
                        "DragonBoardVR: ignored stale objective action quest={:08X} instance={} objective={}.",
                        formID,
                        objectiveInstanceID,
                        objectiveID);
                    return;
                }
                tracked = false;
            }
        }

        const bool desiredTracking =
            action == JournalAction::kToggleTracking ? !tracked : true;
        if (!SetQuestTrackedGameThread(formID, desiredTracking)) return;

        if (action == JournalAction::kTrackObjective) {
            vrui::VRUIMapMarker::ClearQuestObjectivePosition();
            _questMarkerWatchFormID = formID;
            _questMarkerWatchQuestInstanceID = instanceID;
            _questMarkerWatchObjectiveInstanceID = objectiveInstanceID;
            _questMarkerWatchObjectiveID = objectiveID;
            _questMarkerPollAccumulator = 0.0f;
            _questMovingTargetHandle.reset();
            _questMovingTargetPollAccumulator = 0.0f;
            PersistQuestMarkerSelectionGameThread();
            _questTargetResolveFormID = formID;
            _questTargetResolveInstanceID = objectiveInstanceID;
            _questTargetResolveObjectiveID = objectiveID;
            _questTargetResolveAttempts = 4;
            _questTargetResolveDelay = 0.75f;
            logger::info(
                "DragonBoardVR: scheduled bounded objective entrance resolution quest={:08X} instance={} objective={}.",
                formID,
                objectiveInstanceID,
                objectiveID);
        } else if (!desiredTracking) {
            if (_questMarkerWatchFormID == formID) {
                _questMarkerWatchFormID = 0;
                _questMarkerWatchQuestInstanceID = 0;
                _questMarkerWatchObjectiveInstanceID = 0;
                _questMarkerWatchObjectiveID = 0;
                _questMovingTargetHandle.reset();
                _questMovingTargetPollAccumulator = 0.0f;
                PersistQuestMarkerSelectionGameThread();
            }
            _questTargetResolveDelay = -1.0f;
            _questTargetResolveAttempts = 0;
            vrui::VRUIMapMarker::ClearQuestObjectivePosition();
            logger::info(
                "DragonBoardVR: objective map marker cache cleared with quest {:08X} tracking.",
                formID);
        }

        {
            std::scoped_lock lock(_journalMutex);
            const auto selected = std::find_if(
                _journalQuests.begin(),
                _journalQuests.end(),
                [&](const JournalQuestEntry& quest) {
                    return quest.formID == formID && quest.instanceID == instanceID;
                });
            if (selected != _journalQuests.end()) {
                selected->active = desiredTracking;
            }
        }
        _rmlJournalSyncPending.store(true);
        _journalPollAccumulator = 0.5f;
    }

    bool RmlPanelHost::BeginDeveloperCommandKeyboardPresentThread()
    {
#ifdef ENABLE_SKYRIM_VR
        if (_developerKeyboardOpen) return true;

        auto* overlay = GetSteamVrOverlay();
        if (!overlay) {
            logger::error("DragonBoardVR: SteamVR overlay interface is unavailable.");
            return false;
        }

        auto handle =
            static_cast<vr::VROverlayHandle_t>(_developerKeyboardOverlayHandle);
        if (handle == vr::k_ulOverlayHandleInvalid) {
            auto error = overlay->FindOverlay(kDeveloperKeyboardOverlayKey, &handle);
            if (error != vr::VROverlayError_None) {
                error = overlay->CreateOverlay(
                    kDeveloperKeyboardOverlayKey,
                    kDeveloperKeyboardOverlayName,
                    &handle);
            }
            if (error != vr::VROverlayError_None) {
                logger::error(
                    "DragonBoardVR: could not create the SteamVR developer keyboard overlay: {}.",
                    overlay->GetOverlayErrorNameFromEnum(error));
                return false;
            }
            _developerKeyboardOverlayHandle = handle;
        }

        vr::VREvent_t staleEvent{};
        while (overlay->PollNextOverlayEvent(
            handle, &staleEvent, sizeof(staleEvent))) {
        }

        const auto error = overlay->ShowKeyboardForOverlay(
            handle,
            vr::k_EGamepadTextInputModeNormal,
            vr::k_EGamepadTextInputLineModeSingleLine,
            "Add console command",
            static_cast<std::uint32_t>(kDeveloperCommandMaximumLength),
            "",
            false,
            kDeveloperKeyboardUserValue);
        if (error != vr::VROverlayError_None) {
            logger::error(
                "DragonBoardVR: SteamVR developer command keyboard failed to open: {}.",
                overlay->GetOverlayErrorNameFromEnum(error));
            return false;
        }

        _developerKeyboardCloseRequested.store(false, std::memory_order_release);
        _developerKeyboardOpen = true;
        logger::info("DragonBoardVR: SteamVR developer command keyboard opened.");
        return true;
#else
        return false;
#endif
    }

    bool RmlPanelHost::BeginInventorySearchKeyboardPresentThread()
    {
#ifdef ENABLE_SKYRIM_VR
        if (_inventoryKeyboardOpen) return true;

        auto* overlay = GetSteamVrOverlay();
        if (!overlay) {
            logger::error("DragonBoardVR: SteamVR overlay interface is unavailable.");
            return false;
        }

        auto handle =
            static_cast<vr::VROverlayHandle_t>(_inventoryKeyboardOverlayHandle);
        if (handle == vr::k_ulOverlayHandleInvalid) {
            auto error = overlay->FindOverlay(kInventoryKeyboardOverlayKey, &handle);
            if (error != vr::VROverlayError_None) {
                error = overlay->CreateOverlay(
                    kInventoryKeyboardOverlayKey,
                    kInventoryKeyboardOverlayName,
                    &handle);
            }
            if (error != vr::VROverlayError_None) {
                logger::error(
                    "DragonBoardVR: could not create the SteamVR inventory keyboard overlay: {}.",
                    overlay->GetOverlayErrorNameFromEnum(error));
                return false;
            }
            _inventoryKeyboardOverlayHandle = handle;
        }

        vr::VREvent_t staleEvent{};
        while (overlay->PollNextOverlayEvent(
            handle, &staleEvent, sizeof(staleEvent))) {
        }

        std::string existingText;
        {
            std::scoped_lock lock(_inventoryMutex);
            existingText = _inventorySearchQuery;
        }
        const auto error = overlay->ShowKeyboardForOverlay(
            handle,
            vr::k_EGamepadTextInputModeNormal,
            vr::k_EGamepadTextInputLineModeSingleLine,
            "Search inventory",
            static_cast<std::uint32_t>(kInventorySearchMaximumLength),
            existingText.c_str(),
            false,
            kInventoryKeyboardUserValue);
        if (error != vr::VROverlayError_None) {
            logger::error(
                "DragonBoardVR: SteamVR inventory keyboard failed to open: {}.",
                overlay->GetOverlayErrorNameFromEnum(error));
            return false;
        }

        _inventoryKeyboardCloseRequested.store(false, std::memory_order_release);
        _inventoryKeyboardOpen = true;
        logger::info("DragonBoardVR: SteamVR inventory search keyboard opened.");
        return true;
#else
        return false;
#endif
    }

    bool RmlPanelHost::BeginMagicSearchKeyboardPresentThread()
    {
#ifdef ENABLE_SKYRIM_VR
        if (_magicKeyboardOpen) return true;

        auto* overlay = GetSteamVrOverlay();
        if (!overlay) {
            logger::error("DragonBoardVR: SteamVR overlay interface is unavailable.");
            return false;
        }

        auto handle =
            static_cast<vr::VROverlayHandle_t>(_magicKeyboardOverlayHandle);
        if (handle == vr::k_ulOverlayHandleInvalid) {
            auto error = overlay->FindOverlay(kMagicKeyboardOverlayKey, &handle);
            if (error != vr::VROverlayError_None) {
                error = overlay->CreateOverlay(
                    kMagicKeyboardOverlayKey,
                    kMagicKeyboardOverlayName,
                    &handle);
            }
            if (error != vr::VROverlayError_None) {
                logger::error(
                    "DragonBoardVR: could not create the SteamVR magic keyboard overlay: {}.",
                    overlay->GetOverlayErrorNameFromEnum(error));
                return false;
            }
            _magicKeyboardOverlayHandle = handle;
        }

        vr::VREvent_t staleEvent{};
        while (overlay->PollNextOverlayEvent(
            handle, &staleEvent, sizeof(staleEvent))) {
        }

        std::string existingText;
        {
            std::scoped_lock lock(_magicMutex);
            existingText = _magicSearchQuery;
        }
        const auto error = overlay->ShowKeyboardForOverlay(
            handle,
            vr::k_EGamepadTextInputModeNormal,
            vr::k_EGamepadTextInputLineModeSingleLine,
            "Search magic",
            static_cast<std::uint32_t>(kMagicSearchMaximumLength),
            existingText.c_str(),
            false,
            kMagicKeyboardUserValue);
        if (error != vr::VROverlayError_None) {
            logger::error(
                "DragonBoardVR: SteamVR magic keyboard failed to open: {}.",
                overlay->GetOverlayErrorNameFromEnum(error));
            return false;
        }

        _magicKeyboardCloseRequested.store(false, std::memory_order_release);
        _magicKeyboardOpen = true;
        logger::info("DragonBoardVR: SteamVR magic search keyboard opened.");
        return true;
#else
        return false;
#endif
    }

    void RmlPanelHost::UpdateDeveloperCommandKeyboardPresentThread()
    {
#ifdef ENABLE_SKYRIM_VR
        const bool closeRequested =
            _developerKeyboardCloseRequested.exchange(false, std::memory_order_acq_rel);
        const auto handle =
            static_cast<vr::VROverlayHandle_t>(_developerKeyboardOverlayHandle);
        if (handle == vr::k_ulOverlayHandleInvalid && !_developerKeyboardOpen) return;

        auto* overlay = GetSteamVrOverlay();
        if (!overlay) {
            _developerKeyboardOpen = false;
            return;
        }

        if (closeRequested) {
            if (_developerKeyboardOpen) overlay->HideKeyboard();
            _developerKeyboardOpen = false;
        }
        if (handle == vr::k_ulOverlayHandleInvalid) return;

        vr::VREvent_t event{};
        while (overlay->PollNextOverlayEvent(handle, &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_KeyboardDone) {
                std::array<char, kDeveloperCommandMaximumLength + 1> text{};
                overlay->GetKeyboardText(
                    text.data(), static_cast<std::uint32_t>(text.size()));
                QueueDeveloperCommandAdditionPresentThread(text.data());
                _developerKeyboardOpen = false;
                logger::info("DragonBoardVR: SteamVR developer command accepted.");
            } else if (event.eventType == vr::VREvent_KeyboardClosed) {
                _developerKeyboardOpen = false;
                logger::info(
                    "DragonBoardVR: SteamVR developer command keyboard closed.");
            }
        }
#endif
    }

    void RmlPanelHost::QueueDeveloperCommandAdditionPresentThread(std::string command)
    {
        const auto first = std::find_if_not(
            command.begin(), command.end(),
            [](unsigned char character) { return std::isspace(character) != 0; });
        const auto last = std::find_if_not(
            command.rbegin(), command.rend(),
            [](unsigned char character) { return std::isspace(character) != 0; }).base();
        command = first < last ? std::string(first, last) : std::string{};
        if (command.empty()) {
            _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError));
            logger::warn("DragonBoardVR: ignored an empty Dev command.");
            return;
        }

        {
            std::scoped_lock lock(_devMutex);
            _pendingDevCommandAddition = std::move(command);
        }
        _devCommandAdditionPending.store(true, std::memory_order_release);
    }

    void RmlPanelHost::UpdateInventorySearchKeyboardPresentThread()
    {
#ifdef ENABLE_SKYRIM_VR
        const bool closeRequested =
            _inventoryKeyboardCloseRequested.exchange(false, std::memory_order_acq_rel);
        const auto handle =
            static_cast<vr::VROverlayHandle_t>(_inventoryKeyboardOverlayHandle);
        if (handle == vr::k_ulOverlayHandleInvalid && !_inventoryKeyboardOpen) return;

        auto* overlay = GetSteamVrOverlay();
        if (!overlay) {
            _inventoryKeyboardOpen = false;
            return;
        }

        if (closeRequested) {
            if (_inventoryKeyboardOpen) overlay->HideKeyboard();
            _inventoryKeyboardOpen = false;
        }

        if (handle == vr::k_ulOverlayHandleInvalid) return;

        vr::VREvent_t event{};
        while (overlay->PollNextOverlayEvent(handle, &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_KeyboardDone) {
                std::array<char, kInventorySearchMaximumLength + 1> text{};
                overlay->GetKeyboardText(
                    text.data(), static_cast<std::uint32_t>(text.size()));
                ApplyInventorySearchQueryPresentThread(text.data());
                _inventoryKeyboardOpen = false;
                logger::info(
                    "DragonBoardVR: SteamVR inventory search accepted.");
            } else if (event.eventType == vr::VREvent_KeyboardClosed) {
                _inventoryKeyboardOpen = false;
                logger::info(
                    "DragonBoardVR: SteamVR inventory search keyboard closed.");
            }
        }
#endif
    }

    void RmlPanelHost::UpdateMagicSearchKeyboardPresentThread()
    {
#ifdef ENABLE_SKYRIM_VR
        const bool closeRequested =
            _magicKeyboardCloseRequested.exchange(false, std::memory_order_acq_rel);
        const auto handle =
            static_cast<vr::VROverlayHandle_t>(_magicKeyboardOverlayHandle);
        if (handle == vr::k_ulOverlayHandleInvalid && !_magicKeyboardOpen) return;

        auto* overlay = GetSteamVrOverlay();
        if (!overlay) {
            _magicKeyboardOpen = false;
            return;
        }

        if (closeRequested) {
            if (_magicKeyboardOpen) overlay->HideKeyboard();
            _magicKeyboardOpen = false;
        }
        if (handle == vr::k_ulOverlayHandleInvalid) return;

        vr::VREvent_t event{};
        while (overlay->PollNextOverlayEvent(handle, &event, sizeof(event))) {
            if (event.eventType == vr::VREvent_KeyboardDone) {
                std::array<char, kMagicSearchMaximumLength + 1> text{};
                overlay->GetKeyboardText(
                    text.data(), static_cast<std::uint32_t>(text.size()));
                ApplyMagicSearchQueryPresentThread(text.data());
                _magicKeyboardOpen = false;
                logger::info("DragonBoardVR: SteamVR magic search accepted.");
            } else if (event.eventType == vr::VREvent_KeyboardClosed) {
                _magicKeyboardOpen = false;
                logger::info(
                    "DragonBoardVR: SteamVR magic search keyboard closed.");
            }
        }
#endif
    }

    void RmlPanelHost::ApplyInventorySearchQueryPresentThread(std::string query)
    {
        const auto first = std::find_if_not(
            query.begin(), query.end(),
            [](unsigned char character) { return std::isspace(character) != 0; });
        const auto last = std::find_if_not(
            query.rbegin(), query.rend(),
            [](unsigned char character) { return std::isspace(character) != 0; }).base();
        query = first < last ? std::string(first, last) : std::string{};

        {
            std::scoped_lock lock(_inventoryMutex);
            _inventorySearchQuery = std::move(query);
            _inventoryVisibleIndices.clear();
            ReconcileInventorySelectionForSearchLocked();
        }
        _inventoryPreviewRefreshPending.store(true, std::memory_order_release);
        _rmlInventorySyncPending.store(true, std::memory_order_release);
    }

    void RmlPanelHost::ApplyMagicSearchQueryPresentThread(std::string query)
    {
        const auto first = std::find_if_not(
            query.begin(), query.end(),
            [](unsigned char character) { return std::isspace(character) != 0; });
        const auto last = std::find_if_not(
            query.rbegin(), query.rend(),
            [](unsigned char character) { return std::isspace(character) != 0; }).base();
        query = first < last ? std::string(first, last) : std::string{};

        {
            std::scoped_lock lock(_magicMutex);
            _magicSearchQuery = std::move(query);
            _magicVisibleIndices.clear();
            ReconcileMagicSelectionForSearchLocked();
        }
        _magicPreviewRefreshPending.store(true, std::memory_order_release);
        _rmlMagicSyncPending.store(true, std::memory_order_release);
    }

    bool RmlPanelHost::TryMapInventoryVisibleIndex(
        std::size_t visibleIndex,
        std::size_t& inventoryIndex)
    {
        std::scoped_lock lock(_inventoryMutex);
        if (visibleIndex >= _inventoryVisibleIndices.size()) return false;
        inventoryIndex = _inventoryVisibleIndices[visibleIndex];
        return inventoryIndex < _inventoryItems.size();
    }

    bool RmlPanelHost::TryMapMagicVisibleIndex(
        std::size_t visibleIndex,
        std::size_t& magicIndex)
    {
        std::scoped_lock lock(_magicMutex);
        if (visibleIndex >= _magicVisibleIndices.size()) return false;
        magicIndex = _magicVisibleIndices[visibleIndex];
        return magicIndex < _magicItems.size();
    }

    bool RmlPanelHost::InventoryEntryMatchesSearch(
        const InventoryEntry& entry,
        std::string_view query)
    {
        if (query.empty()) return true;
        const auto equalIgnoreCase = [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                std::tolower(static_cast<unsigned char>(right));
        };
        return std::search(
            entry.name.begin(), entry.name.end(),
            query.begin(), query.end(),
            equalIgnoreCase) != entry.name.end();
    }

    bool RmlPanelHost::MagicEntryMatchesSearch(
        const MagicEntry& entry,
        std::string_view query)
    {
        if (query.empty()) return true;
        const auto equalIgnoreCase = [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                std::tolower(static_cast<unsigned char>(right));
        };
        return std::search(
            entry.name.begin(), entry.name.end(),
            query.begin(), query.end(),
            equalIgnoreCase) != entry.name.end();
    }

    bool RmlPanelHost::ReconcileInventorySelectionForSearchLocked()
    {
        const auto previousFormID = _inventorySelectedFormID;
        if (_inventorySelectedIndex < _inventoryItems.size() &&
            InventoryEntryMatchesSearch(
                _inventoryItems[_inventorySelectedIndex], _inventorySearchQuery)) {
            _inventorySelectedFormID =
                _inventoryItems[_inventorySelectedIndex].formID;
            return _inventorySelectedFormID != previousFormID;
        }

        const auto firstMatch = std::find_if(
            _inventoryItems.begin(), _inventoryItems.end(),
            [this](const InventoryEntry& entry) {
                return InventoryEntryMatchesSearch(entry, _inventorySearchQuery);
            });
        if (firstMatch == _inventoryItems.end()) {
            _inventorySelectedIndex = 0;
            _inventorySelectedFormID = 0;
        } else {
            _inventorySelectedIndex = static_cast<std::size_t>(
                std::distance(_inventoryItems.begin(), firstMatch));
            _inventorySelectedFormID = firstMatch->formID;
        }
        return _inventorySelectedFormID != previousFormID;
    }

    bool RmlPanelHost::ReconcileMagicSelectionForSearchLocked()
    {
        const auto previousFormID = _magicSelectedFormID;
        if (_magicSelectedIndex < _magicItems.size() &&
            MagicEntryMatchesSearch(
                _magicItems[_magicSelectedIndex], _magicSearchQuery)) {
            _magicSelectedFormID = _magicItems[_magicSelectedIndex].formID;
            return _magicSelectedFormID != previousFormID;
        }

        const auto firstMatch = std::find_if(
            _magicItems.begin(), _magicItems.end(),
            [this](const MagicEntry& entry) {
                return MagicEntryMatchesSearch(entry, _magicSearchQuery);
            });
        if (firstMatch == _magicItems.end()) {
            _magicSelectedIndex = 0;
            _magicSelectedFormID = 0;
        } else {
            _magicSelectedIndex = static_cast<std::size_t>(
                std::distance(_magicItems.begin(), firstMatch));
            _magicSelectedFormID = firstMatch->formID;
        }
        return _magicSelectedFormID != previousFormID;
    }

    void RmlPanelHost::CaptureInventoryGameThread(bool preserveSelection)
    {
        vrui::VRUIInventoryContainer* backend = nullptr;
        std::uint32_t previousFormID = 0;
        std::size_t previousIndex = 0;
        {
            std::scoped_lock lock(_inventoryMutex);
            backend = _inventoryBackend;
            previousFormID = preserveSelection ? _inventorySelectedFormID : 0;
            previousIndex = _inventorySelectedIndex;
        }
        if (!backend) return;

        const auto stateSignature = backend->buildRmlInventorySignature();
        const auto activeFilter = InventoryFilterId(backend->getFilter());
        bool reusedSnapshot = false;
        {
            std::scoped_lock lock(_inventoryMutex);
            reusedSnapshot =
                _inventorySnapshotValid &&
                _inventoryStateSignature == stateSignature &&
                _inventoryActiveFilter == activeFilter;
            if (reusedSnapshot && !preserveSelection) {
                _inventorySelectedIndex = 0;
                _inventorySelectedFormID = _inventoryItems.empty() ?
                    0 : _inventoryItems.front().formID;
                ReconcileInventorySelectionForSearchLocked();
            }
        }
        if (reusedSnapshot) {
            logger::trace(
                "DragonBoardVR: reused unchanged RmlUi inventory snapshot.");
            UpdateInventoryPreviewGameThread();
            _rmlInventorySyncPending.store(true, std::memory_order_release);
            return;
        }

        auto snapshot = backend->buildRmlInventorySnapshot();
        std::vector<InventoryEntry> entries;
        entries.reserve(snapshot.items.size());
        for (auto& source : snapshot.items) {
            InventoryEntry entry;
            entry.name = std::move(source.name);
            entry.category = std::move(source.category);
            entry.description = std::move(source.description);
            entry.equipmentMarker = std::move(source.equipmentMarker);
            entry.equipmentState = std::move(source.equipmentState);
            entry.editCategory = std::move(source.editCategory);
            entry.modelPath = std::move(source.modelPath);
            entry.formID = source.formID;
            entry.count = source.count;
            entry.attack = source.attack;
            entry.defense = source.defense;
            entry.weight = source.weight;
            entry.value = source.value;
            entry.rotX = source.rotX;
            entry.rotY = source.rotY;
            entry.rotZ = source.rotZ;
            entry.xOff = source.xOff;
            entry.yOff = source.yOff;
            entry.zOff = source.zOff;
            entry.scaleMult = source.scaleMult;
            entry.hasAttack = source.hasAttack;
            entry.hasDefense = source.hasDefense;
            entry.equipped = source.equipped;
            entry.equippedLeft = source.equippedLeft;
            entry.equippedRight = source.equippedRight;
            entry.favorited = source.favorited;
            entry.canEquip = source.canEquip;
            entries.push_back(std::move(entry));
        }

        std::size_t selectedIndex = 0;
        if (!entries.empty()) {
            const auto preserved = std::find_if(
                entries.begin(), entries.end(),
                [previousFormID](const InventoryEntry& entry) {
                    return previousFormID != 0 && entry.formID == previousFormID;
                });
            if (preserved != entries.end()) {
                selectedIndex = static_cast<std::size_t>(
                    std::distance(entries.begin(), preserved));
            } else if (preserveSelection) {
                selectedIndex = std::min(previousIndex, entries.size() - 1);
            }
        }

        {
            std::scoped_lock lock(_inventoryMutex);
            _inventoryItems = std::move(entries);
            _inventorySelectedIndex = selectedIndex;
            _inventorySelectedFormID = _inventoryItems.empty() ?
                0 : _inventoryItems[_inventorySelectedIndex].formID;
            _inventoryVisibleIndices.clear();
            _inventoryActiveFilter = activeFilter;
            _inventoryPlayerName = std::move(snapshot.playerName);
            _inventoryPlayerLevel = snapshot.playerLevel;
            _inventoryGold = snapshot.gold;
            _inventoryCurrentWeight = snapshot.currentWeight;
            _inventoryCarryWeight = snapshot.carryWeight;
            _inventoryStateSignature = stateSignature;
            _inventorySnapshotValid = true;
            ReconcileInventorySelectionForSearchLocked();
        }
        UpdateInventoryPreviewGameThread();
        _rmlInventorySyncPending.store(true, std::memory_order_release);
    }

    void RmlPanelHost::UpdateInventoryPreviewGameThread()
    {
        vrui::VRUIItemEditPanel* preview = nullptr;
        std::optional<InventoryEntry> selected;
        {
            std::scoped_lock lock(_inventoryMutex);
            preview = _inventoryPreviewBackend;
            if (_inventorySelectedIndex < _inventoryItems.size()) {
                selected = _inventoryItems[_inventorySelectedIndex];
            }
        }
        if (!preview) return;

        if (!selected) {
            preview->setTargetItem(
                "Misc", "", "", 0,
                0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
                "InventoryPanel");
        } else {
            float rotX = selected->rotX;
            float rotY = selected->rotY;
            float rotZ = selected->rotZ;
            float posX = selected->xOff;
            float posY = selected->yOff;
            float posZ = selected->zOff;
            float scale = selected->scaleMult;
            vrui::ItemUtils::getItemOverrides(
                RE::TESForm::LookupByID(selected->formID),
                rotX, rotY, rotZ,
                posX, posY, posZ,
                scale);
            preview->setTargetItem(
                selected->editCategory,
                selected->name,
                selected->modelPath,
                selected->formID,
                rotX, rotY, rotZ,
                posX, posY, posZ,
                scale,
                "InventoryPanel");
        }
        preview->setRmlPreviewLayout(vrui::VRUIItemEditPanel::RmlPreviewLayout::Inventory);
        preview->setRmlPreviewMode(true);
    }

    void RmlPanelHost::ExecuteInventoryActionGameThread(
        InventoryAction action,
        std::size_t index,
        bool leftHand)
    {
        vrui::VRUIInventoryContainer* backend = nullptr;
        vrui::VRUIItemEditPanel* preview = nullptr;
        InventoryEntry selected;
        bool hasSelection = false;
        {
            std::scoped_lock lock(_inventoryMutex);
            backend = _inventoryBackend;
            preview = _inventoryPreviewBackend;
            if (action == InventoryAction::kSelect && index < _inventoryItems.size()) {
                _inventorySelectedIndex = index;
                _inventorySelectedFormID = _inventoryItems[index].formID;
            }
            if (_inventorySelectedIndex < _inventoryItems.size()) {
                selected = _inventoryItems[_inventorySelectedIndex];
                hasSelection = true;
            }
        }

        switch (action) {
        case InventoryAction::kSelect:
            UpdateInventoryPreviewGameThread();
            _rmlInventorySyncPending.store(true, std::memory_order_release);
            return;
        case InventoryAction::kEquip:
            if (backend && hasSelection && selected.canEquip) {
                const auto hand =
                    leftHand ? vrui::EquipHand::kLeft : vrui::EquipHand::kRight;
                if (backend->activateItem(selected.formID, hand)) {
                    CaptureInventoryGameThread(true);
                    _inventoryRefreshDelay = 0.20f;
                }
            }
            return;
        case InventoryAction::kDrop:
            if (backend && hasSelection) {
                backend->dropItem(selected.formID);
                CaptureInventoryGameThread(true);
            }
            return;
        case InventoryAction::kPin:
            if (preview && hasSelection) {
                const bool succeeded = preview->pinToDashboard();
                _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                    succeeded ?
                        dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kStrong :
                        dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError));
                if (succeeded) {
                    Close();
                    vrui::VRMenuManager::get().switchToPanel("MainPanel");
                }
            }
            return;
        case InventoryAction::kFavorite:
            if (backend) {
                std::uint32_t formID = 0;
                {
                    std::scoped_lock lock(_inventoryMutex);
                    if (index < _inventoryItems.size()) {
                        formID = _inventoryItems[index].formID;
                    }
                }
                if (formID != 0 && backend->toggleFavorite(formID)) {
                    CaptureInventoryGameThread(true);
                    _inventoryRefreshDelay = 0.10f;
                }
            }
            return;
        case InventoryAction::kClose:
            Close();
            vrui::VRMenuManager::get().switchToPanel("MainPanel");
            return;
        case InventoryAction::kFilterWeapons:
        case InventoryAction::kFilterArmor:
        case InventoryAction::kFilterConsumables:
        case InventoryAction::kFilterQuest:
        case InventoryAction::kFilterBooks:
        case InventoryAction::kFilterMisc:
            if (backend) {
                using Filter = vrui::InventoryFilterMode;
                Filter requested = Filter::All;
                switch (action) {
                case InventoryAction::kFilterWeapons: requested = Filter::WeaponsAll; break;
                case InventoryAction::kFilterArmor: requested = Filter::ArmorAll; break;
                case InventoryAction::kFilterConsumables: requested = Filter::ConsumablesAll; break;
                case InventoryAction::kFilterQuest: requested = Filter::QuestItems; break;
                case InventoryAction::kFilterBooks: requested = Filter::BooksAll; break;
                case InventoryAction::kFilterMisc: requested = Filter::MiscAll; break;
                default: break;
                }
                backend->setFilter(
                    backend->getFilter() == requested ? Filter::All : requested);
                CaptureInventoryGameThread(false);
            }
            return;
        case InventoryAction::kNone:
            return;
        }
    }

    void RmlPanelHost::CaptureMagicGameThread(bool preserveSelection)
    {
        vrui::VRUIMagicContainer* backend = nullptr;
        std::uint32_t previousFormID = 0;
        std::size_t previousIndex = 0;
        {
            std::scoped_lock lock(_magicMutex);
            backend = _magicBackend;
            previousFormID = preserveSelection ? _magicSelectedFormID : 0;
            previousIndex = _magicSelectedIndex;
        }
        if (!backend) return;

        const auto stateSignature = backend->buildRmlMagicSignature();
        const auto activeFilter = MagicFilterId(backend->getFilter());
        bool reusedSnapshot = false;
        {
            std::scoped_lock lock(_magicMutex);
            reusedSnapshot =
                _magicSnapshotValid &&
                _magicStateSignature == stateSignature &&
                _magicActiveFilter == activeFilter;
            if (reusedSnapshot && !preserveSelection) {
                _magicSelectedIndex = 0;
                _magicSelectedFormID = _magicItems.empty() ?
                    0 : _magicItems.front().formID;
                ReconcileMagicSelectionForSearchLocked();
            }
        }
        if (reusedSnapshot) {
            logger::trace("DragonBoardVR: reused unchanged RmlUi magic snapshot.");
            UpdateMagicPreviewGameThread();
            _rmlMagicSyncPending.store(true, std::memory_order_release);
            return;
        }

        auto snapshot = backend->buildRmlMagicSnapshot();
        std::vector<MagicEntry> entries;
        entries.reserve(snapshot.items.size());
        for (auto& source : snapshot.items) {
            MagicEntry entry;
            entry.name = std::move(source.name);
            entry.category = std::move(source.category);
            entry.description = std::move(source.description);
            entry.modelPath = std::move(source.modelPath);
            entry.iconPath = std::move(source.iconPath);
            entry.castingType = std::move(source.castingType);
            entry.delivery = std::move(source.delivery);
            entry.skillLevel = std::move(source.skillLevel);
            entry.duration = std::move(source.duration);
            entry.range = std::move(source.range);
            entry.formID = source.formID;
            entry.magickaCost = source.magickaCost;
            entry.rotX = source.rotX;
            entry.rotY = source.rotY;
            entry.rotZ = source.rotZ;
            entry.xOff = source.xOff;
            entry.yOff = source.yOff;
            entry.zOff = source.zOff;
            entry.scaleMult = source.scaleMult;
            entry.equipped = source.equipped;
            entry.equippedLeft = source.equippedLeft;
            entry.equippedRight = source.equippedRight;
            entry.favorited = source.favorited;
            entry.canEquip = source.canEquip;
            entries.push_back(std::move(entry));
        }

        std::size_t selectedIndex = 0;
        if (!entries.empty()) {
            const auto preserved = std::find_if(
                entries.begin(), entries.end(),
                [previousFormID](const MagicEntry& entry) {
                    return previousFormID != 0 && entry.formID == previousFormID;
                });
            if (preserved != entries.end()) {
                selectedIndex = static_cast<std::size_t>(
                    std::distance(entries.begin(), preserved));
            } else if (preserveSelection) {
                selectedIndex = std::min(previousIndex, entries.size() - 1);
            }
        }

        {
            std::scoped_lock lock(_magicMutex);
            _magicItems = std::move(entries);
            _magicSelectedIndex = selectedIndex;
            _magicSelectedFormID = _magicItems.empty() ?
                0 : _magicItems[_magicSelectedIndex].formID;
            _magicVisibleIndices.clear();
            _magicActiveFilter = activeFilter;
            _magicPlayerName = std::move(snapshot.playerName);
            _magicPlayerLevel = snapshot.playerLevel;
            _magicCurrentMagicka = snapshot.currentMagicka;
            _magicMaximumMagicka = snapshot.maximumMagicka;
            _magicStateSignature = stateSignature;
            _magicSnapshotValid = true;
            ReconcileMagicSelectionForSearchLocked();
        }
        UpdateMagicPreviewGameThread();
        _rmlMagicSyncPending.store(true, std::memory_order_release);
    }

    void RmlPanelHost::UpdateMagicPreviewGameThread()
    {
        vrui::VRUIItemEditPanel* preview = nullptr;
        std::optional<MagicEntry> selected;
        {
            std::scoped_lock lock(_magicMutex);
            preview = _magicPreviewBackend;
            if (_magicSelectedIndex < _magicItems.size()) {
                selected = _magicItems[_magicSelectedIndex];
            }
        }
        if (!preview) return;

        if (!selected) {
            preview->setTargetItem(
                "Magic", "", "", 0,
                0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
                "MagicPanel");
        } else {
            float rotX = selected->rotX;
            float rotY = selected->rotY;
            float rotZ = selected->rotZ;
            float posX = selected->xOff;
            float posY = selected->yOff;
            float posZ = selected->zOff;
            float scale = selected->scaleMult;
            vrui::ItemUtils::getItemOverrides(
                RE::TESForm::LookupByID(selected->formID),
                rotX, rotY, rotZ,
                posX, posY, posZ,
                scale);
            preview->setTargetItem(
                "Magic",
                selected->name,
                selected->modelPath,
                selected->formID,
                rotX, rotY, rotZ,
                posX, posY, posZ,
                scale,
                "MagicPanel");
        }
        preview->setRmlPreviewLayout(vrui::VRUIItemEditPanel::RmlPreviewLayout::Magic);
        preview->setRmlPreviewMode(true);
    }

    void RmlPanelHost::ExecuteMagicActionGameThread(
        MagicAction action,
        std::size_t index,
        bool leftHand)
    {
        vrui::VRUIMagicContainer* backend = nullptr;
        vrui::VRUIItemEditPanel* preview = nullptr;
        MagicEntry selected;
        bool hasSelection = false;
        {
            std::scoped_lock lock(_magicMutex);
            backend = _magicBackend;
            preview = _magicPreviewBackend;
            if (action == MagicAction::kSelect && index < _magicItems.size()) {
                _magicSelectedIndex = index;
                _magicSelectedFormID = _magicItems[index].formID;
            }
            if (_magicSelectedIndex < _magicItems.size()) {
                selected = _magicItems[_magicSelectedIndex];
                hasSelection = true;
            }
        }

        const auto queuePinHaptic = [this](bool succeeded) {
            _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                succeeded ?
                    dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kStrong :
                    dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kError));
        };

        switch (action) {
        case MagicAction::kSelect:
            UpdateMagicPreviewGameThread();
            _rmlMagicSyncPending.store(true, std::memory_order_release);
            return;
        case MagicAction::kEquip:
            if (backend && hasSelection && selected.canEquip) {
                const auto hand =
                    leftHand ? vrui::EquipHand::kLeft : vrui::EquipHand::kRight;
                if (backend->activateSpell(selected.formID, hand)) {
                    CaptureMagicGameThread(true);
                    _magicRefreshDelay = 0.20f;
                }
            }
            return;
        case MagicAction::kEdit:
            if (preview && hasSelection &&
                vrui::VRUISettings::get().editModeEnabled) {
                preview->setTargetItem(
                    "Magic",
                    selected.name,
                    selected.modelPath,
                    selected.formID,
                    selected.rotX,
                    selected.rotY,
                    selected.rotZ,
                    selected.xOff,
                    selected.yOff,
                    selected.zOff,
                    selected.scaleMult,
                    "MagicPanel");
                vrui::VRMenuManager::get().switchToPanel("ItemEditPanel");
            }
            return;
        case MagicAction::kPinDashboard:
            {
                const bool succeeded = preview && hasSelection && preview->pinToDashboard();
                queuePinHaptic(succeeded);
                if (succeeded) {
                    Close();
                    vrui::VRMenuManager::get().switchToPanel("MainPanel");
                }
            }
            return;
        case MagicAction::kPinLeftHand:
            {
                const bool succeeded = preview && hasSelection && preview->pinToLeftHand();
                queuePinHaptic(succeeded);
                if (succeeded) {
                    Close();
                    vrui::VRMenuManager::get().switchToPanel("MainPanel");
                }
            }
            return;
        case MagicAction::kPinWorld:
            {
                const bool succeeded = preview && hasSelection && preview->pinToWorld();
                queuePinHaptic(succeeded);
                if (succeeded) {
                    Close();
                    vrui::VRMenuManager::get().switchToPanel("MainPanel");
                }
            }
            return;
        case MagicAction::kToggleLabel:
            queuePinHaptic(preview && hasSelection && preview->togglePinnedLabel());
            return;
        case MagicAction::kFavorite:
            if (backend) {
                std::uint32_t formID = 0;
                {
                    std::scoped_lock lock(_magicMutex);
                    if (index < _magicItems.size()) {
                        formID = _magicItems[index].formID;
                    }
                }
                if (formID != 0 && backend->toggleFavorite(formID)) {
                    CaptureMagicGameThread(true);
                    _magicRefreshDelay = 0.10f;
                }
            }
            return;
        case MagicAction::kClose:
            Close();
            vrui::VRMenuManager::get().switchToPanel("MainPanel");
            return;
        case MagicAction::kFilterDestruction:
        case MagicAction::kFilterConjuration:
        case MagicAction::kFilterRestoration:
        case MagicAction::kFilterIllusion:
        case MagicAction::kFilterAlteration:
        case MagicAction::kFilterPowers:
        case MagicAction::kFilterPassive:
            if (backend) {
                using Filter = vrui::MagicFilterMode;
                Filter requested = Filter::All;
                switch (action) {
                case MagicAction::kFilterDestruction: requested = Filter::Destruction; break;
                case MagicAction::kFilterConjuration: requested = Filter::Conjuration; break;
                case MagicAction::kFilterRestoration: requested = Filter::Restoration; break;
                case MagicAction::kFilterIllusion: requested = Filter::Illusion; break;
                case MagicAction::kFilterAlteration: requested = Filter::Alteration; break;
                case MagicAction::kFilterPowers: requested = Filter::Powers; break;
                case MagicAction::kFilterPassive: requested = Filter::Passive; break;
                default: break;
                }
                backend->setFilter(
                    backend->getFilter() == requested ? Filter::All : requested);
                CaptureMagicGameThread(false);
            }
            return;
        case MagicAction::kNone:
            return;
        }
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

        const bool pinAction =
            action == ItemEditAction::kPinDashboard ||
            action == ItemEditAction::kPinLeftHand ||
            action == ItemEditAction::kPinWorld;
        if (pinAction && succeeded) {
            _pendingRmlHapticCue.store(static_cast<std::uint8_t>(
                dragonboard::ui::rml::DragonBoardRmlUi::HapticCue::kStrong));
            Close();
            vrui::VRMenuManager::get().switchToPanel("MainPanel");
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
