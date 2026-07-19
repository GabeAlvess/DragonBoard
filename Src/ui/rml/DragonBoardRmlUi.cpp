#include "ui/rml/DragonBoardRmlUi.h"
#include "ui/rml/DragonBoardRmlRenderer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/SystemInterface.h>

namespace dragonboard::ui::rml
{
    namespace
    {
        bool ParseDecimal(std::string_view value, std::uint32_t& result)
        {
            if (value.empty()) return false;
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), result);
            return error == std::errc{} && end == value.data() + value.size();
        }

        bool ParseJournalQuestIdentity(
            std::string_view value,
            std::uint32_t& formID,
            std::uint32_t& instanceID)
        {
            const auto separator = value.find('-');
            return separator != std::string_view::npos &&
                   ParseDecimal(value.substr(0, separator), formID) &&
                   ParseDecimal(value.substr(separator + 1), instanceID);
        }

        bool ParseJournalObjectiveIdentity(
            std::string_view value,
            std::uint32_t& formID,
            std::uint32_t& questInstanceID,
            std::uint32_t& objectiveInstanceID,
            std::uint16_t& objectiveID)
        {
            const auto first = value.find('-');
            const auto second = first == std::string_view::npos ?
                                    std::string_view::npos :
                                    value.find('-', first + 1);
            const auto third = second == std::string_view::npos ?
                                   std::string_view::npos :
                                   value.find('-', second + 1);
            std::uint32_t parsedObjectiveID = 0;
            if (first == std::string_view::npos || second == std::string_view::npos ||
                third == std::string_view::npos ||
                !ParseDecimal(value.substr(0, first), formID) ||
                !ParseDecimal(
                    value.substr(first + 1, second - first - 1), questInstanceID) ||
                !ParseDecimal(
                    value.substr(second + 1, third - second - 1), objectiveInstanceID) ||
                !ParseDecimal(value.substr(third + 1), parsedObjectiveID) ||
                parsedObjectiveID > std::numeric_limits<std::uint16_t>::max()) {
                return false;
            }
            objectiveID = static_cast<std::uint16_t>(parsedObjectiveID);
            return true;
        }

        bool IsActionCard(const Rml::Element* element)
        {
            if (!element) return false;
            const std::string_view id(element->GetId());
            return id == "edit-pin-dashboard" || id == "edit-pin-left" ||
                   id == "edit-pin-world" || id == "edit-toggle-label" ||
                   id.starts_with("mods-card-");
        }

        constexpr const char* kContextName = "dragonboard_local_panels_rml";

        constexpr std::array<const char*, 3> kDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/settings.rml",
            "SKSE/Plugins/DragonBoardVR/ui/settings.rml",
            "Assets/ui/rml/settings.rml"
        };

        constexpr std::array<const char*, 3> kDeveloperDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/dev.rml",
            "SKSE/Plugins/DragonBoardVR/ui/dev.rml",
            "Assets/ui/rml/dev.rml"
        };

        constexpr std::array<const char*, 3> kItemEditDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/edit.rml",
            "SKSE/Plugins/DragonBoardVR/ui/edit.rml",
            "Assets/ui/rml/edit.rml"
        };

        constexpr std::array<const char*, 3> kModsDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/mods.rml",
            "SKSE/Plugins/DragonBoardVR/ui/mods.rml",
            "Assets/ui/rml/mods.rml"
        };

        constexpr std::array<const char*, 3> kInventoryDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/inventory.rml",
            "SKSE/Plugins/DragonBoardVR/ui/inventory.rml",
            "Assets/ui/rml/inventory.rml"
        };

        constexpr std::array<const char*, 3> kMagicDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/magic.rml",
            "SKSE/Plugins/DragonBoardVR/ui/magic.rml",
            "Assets/ui/rml/magic.rml"
        };

        constexpr std::array<const char*, 3> kJournalDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/journal.rml",
            "SKSE/Plugins/DragonBoardVR/ui/journal.rml",
            "Assets/ui/rml/journal.rml"
        };

        constexpr std::array<const char*, 3> kFontCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/assets/DragonBoardVR_Font.ttf",
            "SKSE/Plugins/DragonBoardVR/ui/assets/DragonBoardVR_Font.ttf",
            "Assets/ui/rml/assets/DragonBoardVR_Font.ttf"
        };

        constexpr std::array<const char*, 5> kPages{
            "general", "position", "visuals", "items", "labels"
        };

        constexpr std::array<const char*, 23> kSliders{
            "menuScale", "buttonSpacingX", "buttonSpacingY",
            "menuOffsetX", "menuOffsetY", "menuOffsetZ",
            "menuRotX", "menuRotY", "menuRotZ",
            "buttonMeshScale", "itemMeshScale", "containerGridOffsetZ", "reticleScale",
            "itemWeaponScale", "itemArmorScale", "itemPotionScale", "itemFoodScale", "itemMiscScale",
            "labelScale", "labelSpacing", "labelXOffset", "labelYOffset", "labelZOffset"
        };

        constexpr std::array<const char*, 3> kDeveloperPages{
            "commands", "info", "calibration"
        };

        constexpr std::array<const char*, 5> kMapCalibrationCities{
            "Whiterun", "Riften", "Solitude", "Falkreath", "Windhelm"
        };

        constexpr std::array<const char*, 4> kItemEditPages{
            "position", "rotation", "scale", "pin"
        };

        constexpr std::array<const char*, 7> kItemEditSliders{
            "editPosX", "editPosY", "editPosZ",
            "editRotX", "editRotY", "editRotZ", "editScale"
        };

        std::string EscapeRml(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value) {
                switch (character) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                default: escaped += character; break;
                }
            }
            return escaped;
        }

        float Milliseconds(
            std::chrono::steady_clock::time_point start,
            std::chrono::steady_clock::time_point end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }

        std::size_t CountDomElements(const Rml::Element* element)
        {
            if (!element) return 0;
            std::size_t count = 1;
            const int childCount = element->GetNumChildren(false);
            for (int index = 0; index < childCount; ++index) {
                count += CountDomElements(element->GetChild(index));
            }
            return count;
        }

    }

    class DragonBoardRmlUi::UiEventListener final : public Rml::EventListener
    {
    public:
        explicit UiEventListener(DragonBoardRmlUi& owner) : _owner(owner) {}

        void ProcessEvent(Rml::Event& event) override
        {
            if (event.GetType() == "click") {
                // Events can target a block-level span or text node inside a
                // button. Resolve the actual button so its full visual card,
                // including title and description, has one reliable action.
                auto* element = event.GetTargetElement();
                while (element && element->GetTagName() != "button" &&
                       element->GetTagName() != "input" && !IsActionCard(element)) {
                    element = element->GetParentNode();
                }
                if (element && !element->GetId().empty()) {
                    _owner.HandleClick(element->GetId().c_str());
                }
            } else if (event.GetType() == "change") {
                auto* element = event.GetTargetElement();
                while (element && element->GetTagName() != "input") {
                    element = element->GetParentNode();
                }
                if (element && !element->GetId().empty()) {
                    _owner.HandleSliderChange(
                        element->GetId().c_str(),
                        event.GetParameter<float>("value", 0.0f));
                }
            }
        }

    private:
        DragonBoardRmlUi& _owner;
    };

    class DragonBoardRmlUi::SystemLogger final : public Rml::SystemInterface
    {
    public:
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
        {
            switch (type) {
            case Rml::Log::LT_ERROR:
            case Rml::Log::LT_ASSERT:
                logger::error("DragonBoardVR RmlUi: {}", message);
                break;
            case Rml::Log::LT_WARNING:
                logger::warn("DragonBoardVR RmlUi: {}", message);
                break;
            case Rml::Log::LT_INFO:
                logger::info("DragonBoardVR RmlUi: {}", message);
                break;
            default:
                logger::debug("DragonBoardVR RmlUi: {}", message);
                break;
            }
            return true;
        }
    };

    DragonBoardRmlUi::DragonBoardRmlUi() = default;

    DragonBoardRmlUi::~DragonBoardRmlUi()
    {
        Shutdown();
    }

    bool DragonBoardRmlUi::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        if (_renderer && _renderer->IsReady() && _context) return true;
        Shutdown();

        _renderer = std::make_unique<DragonBoardRmlRenderer>();
        if (!_renderer->Initialize(device, context)) {
            logger::error("DragonBoardVR: failed to initialize the minimal RmlUi DX11 renderer.");
            Shutdown();
            return false;
        }

        _systemLogger = std::make_unique<SystemLogger>();
        Rml::SetSystemInterface(_systemLogger.get());
        Rml::SetRenderInterface(_renderer.get());
        if (!Rml::Initialise()) {
            logger::error("DragonBoardVR: RmlUi core initialization failed.");
            Shutdown();
            return false;
        }
        _rmlInitialized = true;

        _context = Rml::CreateContext(kContextName, Rml::Vector2i(1920, 1080), _renderer.get());
        if (!_context) {
            logger::error("DragonBoardVR: RmlUi context creation failed.");
            Shutdown();
            return false;
        }
        // Grip motion is already sampled continuously by the VR bridge. An
        // additional smooth-scroll queue makes the page keep moving after grip
        // release and shifts click targets under the trigger.
        _context->SetDefaultScrollBehavior(Rml::ScrollBehavior::Instant, 1.0f);

        bool fontLoaded = false;
        for (const auto* path : kFontCandidates) {
            if (!std::filesystem::exists(path)) continue;
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) continue;
            const auto size = stream.tellg();
            if (size <= 0) continue;
            _fontData.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            if (!stream.read(reinterpret_cast<char*>(_fontData.data()), size)) {
                _fontData.clear();
                continue;
            }
            const Rml::Span<const Rml::byte> fontBytes(
                _fontData.data(), _fontData.size());
            if (Rml::LoadFontFace(
                    fontBytes,
                    "DragonBoard",
                    Rml::Style::FontStyle::Normal,
                    Rml::Style::FontWeight::Normal)) {
                logger::info("DragonBoardVR: RmlUi font '{}' registered as DragonBoard.", path);
                fontLoaded = true;
                break;
            }
            _fontData.clear();
        }
        if (!fontLoaded) {
            logger::error("DragonBoardVR: RmlUi could not find a usable font.");
            Shutdown();
            return false;
        }

        _eventListener = std::make_unique<UiEventListener>(*this);
        _builtinDocumentLoadStep = 0;
        return true;
    }

    bool DragonBoardRmlUi::LoadNextBuiltinDocument()
    {
        constexpr std::size_t kBuiltinDocumentCount = 7;
        if (!_context || !_eventListener || _builtinDocumentLoadStep >= kBuiltinDocumentCount) {
            return false;
        }

        const auto loadDocument =
            [this]<std::size_t N>(
                const std::array<const char*, N>& candidates,
                Rml::ElementDocument*& destination,
                const char* name) -> const char* {
                for (const auto* path : candidates) {
                    if (!std::filesystem::exists(path)) continue;
                    const auto started = std::chrono::steady_clock::now();
                    destination = _context->LoadDocument(path);
                    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
                    logger::info(
                        "DragonBoardVR: staged RmlUi {} document load took {} ms "
                        "(path='{}', success={}).",
                        name,
                        elapsedMs,
                        path,
                        destination != nullptr);
                    if (destination) return path;
                }
                logger::error("DragonBoardVR: RmlUi {} document could not be loaded.", name);
                return nullptr;
            };

        const auto step = _builtinDocumentLoadStep++;
        Rml::ElementDocument* loadedDocument = nullptr;
        switch (step) {
        case 0: {
            const auto* path = loadDocument(kDocumentCandidates, _settingsDocument, "Settings");
            loadedDocument = _settingsDocument;
            if (_settingsDocument) {
                for (const auto* page : kPages) {
                    const std::string tabId = std::string("tab-") + page;
                    BindClick(_settingsDocument, tabId.c_str());
                }
                for (const auto* slider : kSliders) BindSlider(_settingsDocument, slider);
                BindClick(_settingsDocument, "save");
                BindClick(_settingsDocument, "close");
                BindClick(_settingsDocument, "toggle-edit-mode");
                BindClick(_settingsDocument, "toggle-dev-panel");
                SelectSettingsPage("general");
                _settingsDocument->Hide();
                logger::info("DragonBoardVR: external RmlUi settings loaded from '{}'.", path);
            }
            break;
        }
        case 1: {
            const auto* path = loadDocument(
                kDeveloperDocumentCandidates, _developerDocument, "Developer");
            loadedDocument = _developerDocument;
            if (_developerDocument) {
                for (const auto* page : kDeveloperPages) {
                    const std::string tabId = std::string("dev-tab-") + page;
                    BindClick(_developerDocument, tabId.c_str());
                }
                BindClick(_developerDocument, "dev-add-command");
                BindClick(_developerDocument, "dev-execute");
                BindClick(_developerDocument, "dev-close");
                BindClick(_developerDocument, "dev-calibration-surface");
                BindClick(_developerDocument, "dev-calibration-reset");
                for (std::size_t city = 0; city < kMapCalibrationCities.size(); ++city) {
                    const auto id = "dev-cal-city-" + std::to_string(city);
                    BindClick(_developerDocument, id.c_str());
                }
                SelectDeveloperPage("commands");
                _developerDocument->Hide();
                logger::info("DragonBoardVR: external RmlUi developer panel loaded from '{}'.", path);
            }
            break;
        }
        case 2: {
            const auto* path = loadDocument(
                kItemEditDocumentCandidates, _itemEditDocument, "ItemEdit");
            loadedDocument = _itemEditDocument;
            if (_itemEditDocument) {
                for (const auto* page : kItemEditPages) {
                    const std::string tabId = std::string("edit-tab-") + page;
                    BindClick(_itemEditDocument, tabId.c_str());
                }
                for (const auto* slider : kItemEditSliders) BindSlider(_itemEditDocument, slider);
                for (const auto* id : {
                         "edit-apply-item", "edit-apply-category", "edit-reset", "edit-back", "edit-close",
                         "edit-pin-dashboard", "edit-pin-left", "edit-pin-world", "edit-toggle-label" }) {
                    BindClick(_itemEditDocument, id);
                }
                SelectItemEditPage("position");
                _itemEditDocument->Hide();
                logger::info("DragonBoardVR: external RmlUi item editor loaded from '{}'.", path);
            }
            break;
        }
        case 3: {
            const auto* path = loadDocument(kModsDocumentCandidates, _modsDocument, "Mods");
            loadedDocument = _modsDocument;
            if (_modsDocument) {
                BindClick(_modsDocument, "mods-add");
                BindClick(_modsDocument, "mods-close");
                _modsDocument->Hide();
                logger::info("DragonBoardVR: external RmlUi mods panel loaded from '{}'.", path);
            }
            break;
        }
        case 4: {
            const auto* path = loadDocument(
                kInventoryDocumentCandidates, _inventoryDocument, "Inventory");
            loadedDocument = _inventoryDocument;
            if (_inventoryDocument) {
                for (const auto* id : {
                         "inventory-equip", "inventory-drop", "inventory-pin", "inventory-close",
                         "inventory-search", "inventory-search-clear",
                         "inventory-filter-weapons", "inventory-filter-armor",
                         "inventory-filter-consumables", "inventory-filter-quest",
                         "inventory-filter-books", "inventory-filter-misc" }) {
                    BindClick(_inventoryDocument, id);
                }
                _inventoryDocument->Hide();
                logger::info("DragonBoardVR: external RmlUi inventory panel loaded from '{}'.", path);
            }
            break;
        }
        case 5: {
            const auto* path = loadDocument(kMagicDocumentCandidates, _magicDocument, "Magic");
            loadedDocument = _magicDocument;
            if (_magicDocument) {
                for (const auto* id : {
                         "magic-equip", "magic-edit", "magic-pin", "magic-close",
                         "magic-search", "magic-search-clear",
                         "magic-filter-destruction", "magic-filter-conjuration",
                         "magic-filter-restoration", "magic-filter-illusion",
                         "magic-filter-alteration", "magic-filter-powers",
                         "magic-filter-passive", "magic-pin-dashboard",
                         "magic-pin-left", "magic-pin-world", "magic-pin-label" }) {
                    BindClick(_magicDocument, id);
                }
                _magicDocument->Hide();
                logger::info("DragonBoardVR: external RmlUi magic panel loaded from '{}'.", path);
            }
            break;
        }
        case 6: {
            const auto* path = loadDocument(kJournalDocumentCandidates, _journalDocument, "Journal");
            loadedDocument = _journalDocument;
            if (_journalDocument) {
                for (const auto* id : {
                         "journal-tab-quests", "journal-tab-stats",
                         "journal-settings", "journal-close",
                         "journal-toggle-tracking" }) {
                    BindClick(_journalDocument, id);
                }
                SelectJournalPage("quests");
                _journalDocument->Hide();
                logger::info("DragonBoardVR: external RmlUi journal loaded from '{}'.", path);
            }
            break;
        }
        default:
            break;
        }

        if (loadedDocument) {
            loadedDocument->AddEventListener("change", _eventListener.get());
        }
        return loadedDocument != nullptr;
    }

    bool DragonBoardRmlUi::AreBuiltinDocumentsLoaded() const
    {
        return _builtinDocumentLoadStep >= 7;
    }

    void DragonBoardRmlUi::Shutdown()
    {
        _triggerScrollLockDocument = nullptr;
        _triggerScrollLockActive = false;
        _triggerScrollReleasePending = false;
        _triggerCaptureMode = TriggerCaptureMode::kNone;
        _triggerCapturedSliderId.clear();
        _sliderPointerInitialized = false;
        _triggerCapturedActionId.clear();
        _triggerCaptureProgrammatic = false;
        _pointerSmoothingInitialized = false;
        _interactiveBindings.clear();
        _gripScrollActive = false;
        _gripScrollTarget = nullptr;
        _gripScrollTargetTop = 0.0f;
        _gripPointerScrollAccumulator = 0.0f;
        _settingsDocument = nullptr;
        _developerDocument = nullptr;
        _itemEditDocument = nullptr;
        _modsDocument = nullptr;
        _inventoryDocument = nullptr;
        _magicDocument = nullptr;
        _journalDocument = nullptr;
        _activeDocument = nullptr;
        _modsListMarkup.clear();
        _developerCommandListMarkup.clear();
        _inventoryListMarkup.clear();
        _magicListMarkup.clear();
        _inventoryListSelectedIndex = static_cast<std::size_t>(-1);
        _magicListSelectedIndex = static_cast<std::size_t>(-1);
        _inventoryListInitialized = false;
        _magicListInitialized = false;
        _journalQuestListMarkup.clear();
        _journalActiveQuestOrder.clear();
        _registeredPanels.clear();
        _panelEvents.clear();
        _builtinDocumentLoadStep = 0;
        _eventListener.reset();
        if (_context) {
            Rml::RemoveContext(kContextName);
            _context = nullptr;
        }
        if (_rmlInitialized) {
            Rml::Shutdown();
            _rmlInitialized = false;
        }
        _fontData.clear();
        _systemLogger.reset();
        if (_renderer) {
            _renderer->Shutdown();
            _renderer.reset();
        }
    }

    bool DragonBoardRmlUi::IsReady() const
    {
        return _renderer && _renderer->IsReady() && _context;
    }

    bool DragonBoardRmlUi::IsSettingsReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _settingsDocument;
    }

    bool DragonBoardRmlUi::IsDeveloperReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _developerDocument;
    }

    bool DragonBoardRmlUi::IsItemEditReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _itemEditDocument;
    }

    bool DragonBoardRmlUi::IsModsReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _modsDocument;
    }

    bool DragonBoardRmlUi::IsInventoryReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _inventoryDocument;
    }

    bool DragonBoardRmlUi::IsMagicReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _magicDocument;
    }

    bool DragonBoardRmlUi::IsJournalReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _journalDocument;
    }

    bool DragonBoardRmlUi::ShowSettings()
    {
        if (!IsSettingsReady()) return false;
        if (_activeDocument == _settingsDocument) return true;
        HideAllDocuments();
        _settingsDocument->Show();
        _activeDocument = _settingsDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowDeveloper()
    {
        if (!IsDeveloperReady()) return false;
        if (_activeDocument == _developerDocument) return true;
        HideAllDocuments();
        _developerDocument->Show();
        _activeDocument = _developerDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowItemEdit()
    {
        if (!IsItemEditReady()) return false;
        if (_activeDocument == _itemEditDocument) return true;
        HideAllDocuments();
        _itemEditDocument->Show();
        _activeDocument = _itemEditDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowMods()
    {
        if (!IsModsReady()) return false;
        if (_activeDocument == _modsDocument) return true;
        HideAllDocuments();
        _modsDocument->Show();
        _activeDocument = _modsDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowInventory()
    {
        if (!IsInventoryReady()) return false;
        if (_activeDocument == _inventoryDocument) return true;
        HideAllDocuments();
        _inventoryDocument->Show();
        _activeDocument = _inventoryDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowMagic()
    {
        if (!IsMagicReady()) return false;
        if (_activeDocument == _magicDocument) return true;
        HideAllDocuments();
        _magicDocument->Show();
        _activeDocument = _magicDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowJournal()
    {
        if (!IsJournalReady()) return false;
        if (_activeDocument == _journalDocument) return true;
        HideAllDocuments();
        _journalDocument->Show();
        _activeDocument = _journalDocument;
        return true;
    }

    bool DragonBoardRmlUi::RegisterPanel(
        std::uint32_t handle,
        std::string panelId,
        std::string documentPath)
    {
        if (!_context || !_eventListener || handle == 0 || panelId.empty() ||
            documentPath.empty() || _registeredPanels.contains(handle)) {
            return false;
        }
        for (const auto& [existingHandle, panel] : _registeredPanels) {
            (void)existingHandle;
            if (panel.id == panelId) {
                logger::warn("DragonBoardVR: RmlUi panel id '{}' is already registered.", panelId);
                return false;
            }
        }
        if (!std::filesystem::exists(documentPath)) {
            logger::warn(
                "DragonBoardVR: RmlUi panel '{}' document does not exist: {}",
                panelId,
                documentPath);
            return false;
        }

        auto* document = _context->LoadDocument(documentPath);
        if (!document) {
            logger::error(
                "DragonBoardVR: failed to load RmlUi panel '{}' from '{}'.",
                panelId,
                documentPath);
            return false;
        }
        document->AddEventListener("change", _eventListener.get());
        document->Hide();
        _registeredPanels.emplace(handle, RegisteredPanel{
            handle, std::move(panelId), std::move(documentPath), document });
        RegisterDocumentInteractives(document);
        logger::info(
            "DragonBoardVR: registered external RmlUi panel {} ('{}').",
            handle,
            _registeredPanels.at(handle).id);
        return true;
    }

    bool DragonBoardRmlUi::UnregisterPanel(std::uint32_t handle)
    {
        const auto it = _registeredPanels.find(handle);
        if (it == _registeredPanels.end()) return false;
        auto* document = it->second.document;
        if (_activeDocument == document) {
            _activeDocument = nullptr;
        }
        std::erase_if(_interactiveBindings, [document](const InteractiveBinding& binding) {
            return binding.document == document;
        });
        if (document) document->Close();
        _registeredPanels.erase(it);
        return true;
    }

    bool DragonBoardRmlUi::IsPanelReady(std::uint32_t handle) const
    {
        const auto* panel = FindPanel(handle);
        return _renderer && _renderer->IsReady() && _context && panel && panel->document;
    }

    bool DragonBoardRmlUi::ShowPanel(std::uint32_t handle)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document) return false;
        if (_activeDocument == panel->document) return true;
        HideAllDocuments();
        panel->document->Show();
        _activeDocument = panel->document;
        return true;
    }

    bool DragonBoardRmlUi::SetElementText(
        std::uint32_t handle, const char* elementId, const char* text)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document || !elementId || !*elementId) return false;
        auto* element = panel->document->GetElementById(elementId);
        if (!element) return false;
        element->SetInnerRML(EscapeRml(text ? text : ""));
        return true;
    }

    bool DragonBoardRmlUi::SetElementAttribute(
        std::uint32_t handle,
        const char* elementId,
        const char* name,
        const char* value)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document || !elementId || !*elementId || !name || !*name) {
            return false;
        }
        auto* element = panel->document->GetElementById(elementId);
        if (!element) return false;
        if (value) element->SetAttribute(name, value);
        else element->RemoveAttribute(name);
        return true;
    }

    bool DragonBoardRmlUi::SetElementClass(
        std::uint32_t handle,
        const char* elementId,
        const char* className,
        bool enabled)
    {
        auto* panel = FindPanel(handle);
        if (!panel || !panel->document || !elementId || !*elementId ||
            !className || !*className) {
            return false;
        }
        auto* element = panel->document->GetElementById(elementId);
        if (!element) return false;
        element->SetClass(className, enabled);
        return true;
    }

    std::optional<DragonBoardRmlUi::PanelEvent> DragonBoardRmlUi::ConsumePanelEvent()
    {
        if (_panelEvents.empty()) return std::nullopt;
        auto event = std::move(_panelEvents.front());
        _panelEvents.pop_front();
        return event;
    }

    std::optional<DragonBoardRmlUi::MapCalibrationRequest>
    DragonBoardRmlUi::ConsumeMapCalibrationRequest()
    {
        auto result = _mapCalibrationRequest;
        _mapCalibrationRequest.reset();
        return result;
    }

    bool DragonBoardRmlUi::ConsumeMapCalibrationResetRequested()
    {
        return std::exchange(_mapCalibrationResetRequested, false);
    }

    void DragonBoardRmlUi::ProcessInput(
        bool pointerOnPanel,
        float pointerU,
        float pointerV,
        bool triggerDown,
        bool gripDown,
        float stickX,
        float stickY,
        int width,
        int height,
        float deltaTime)
    {
        if (!IsReady()) return;

        _currentTriggerDown = triggerDown;
        _currentGripDown = gripDown;

        const int rawPointerX = std::clamp(
            static_cast<int>(std::lround(std::clamp(pointerU, 0.0f, 1.0f) * width)), 0, width - 1);
        const int rawPointerY = std::clamp(
            static_cast<int>(std::lround(std::clamp(pointerV, 0.0f, 1.0f) * height)), 0, height - 1);
        int pointerX = rawPointerX;
        int pointerY = rawPointerY;
        _inputWidth = std::max(width, 1);
        _inputHeight = std::max(height, 1);

        if (pointerOnPanel) {
            if (!_pointerSmoothingInitialized || !_pointerWasOnPanel) {
                _smoothedPointerX = static_cast<float>(rawPointerX);
                _smoothedPointerY = static_cast<float>(rawPointerY);
                _pointerSmoothingInitialized = true;
            } else {
                const float deltaX = static_cast<float>(rawPointerX) - _smoothedPointerX;
                const float deltaY = static_cast<float>(rawPointerY) - _smoothedPointerY;
                const float distance = std::hypot(deltaX, deltaY);
                constexpr float pointerDeadzonePixels = 1.25f;
                if (distance > pointerDeadzonePixels) {
                    const float adaptive = std::clamp(
                        (distance - pointerDeadzonePixels) / 120.0f, 0.0f, 1.0f);
                    const float response = 18.0f + adaptive * 42.0f;
                    const float frameTime = std::clamp(deltaTime, 1.0f / 240.0f, 0.05f);
                    const float amount = 1.0f - std::exp(-response * frameTime);
                    _smoothedPointerX += deltaX * amount;
                    _smoothedPointerY += deltaY * amount;
                }
            }
            pointerX = std::clamp(
                static_cast<int>(std::lround(_smoothedPointerX)), 0, width - 1);
            pointerY = std::clamp(
                static_cast<int>(std::lround(_smoothedPointerY)), 0, height - 1);
            _pointerMotionActive =
                std::abs(static_cast<float>(rawPointerX) - _smoothedPointerX) >= 0.5f ||
                std::abs(static_cast<float>(rawPointerY) - _smoothedPointerY) >= 0.5f;
        } else {
            _pointerSmoothingInitialized = false;
            _pointerMotionActive = false;
        }

        // RmlUi's mouse interaction can move a scroll container while the
        // primary button is held (notably through internal draggable controls).
        // In VR the primary button is the trigger, whose contract is click/drag
        // only. Capture the scroll offsets before submitting the press and keep
        // them fixed until the matching release has completed.
        if (triggerDown && !_previousTriggerDown) {
            BeginTriggerScrollLock();
        }

        int submittedPointerX = pointerX;
        int submittedPointerY = pointerY;
        _latestPointerX = pointerX;
        _latestPointerY = pointerY;
        if (_triggerCaptureMode == TriggerCaptureMode::kButton) {
            submittedPointerX = _triggerCaptureX;
            submittedPointerY = _triggerCaptureY;
        } else if (_triggerCaptureMode == TriggerCaptureMode::kSlider) {
            submittedPointerY = _triggerCaptureY;
        }

        if (pointerOnPanel) {
            _context->ProcessMouseMove(submittedPointerX, submittedPointerY, 0);
            RestoreTriggerScrollLock();
            auto* hovered = _context->GetHoverElement();
            _previewInteractionZoneHovered = false;
            for (auto* candidate = hovered;
                 candidate && candidate != _activeDocument;
                 candidate = candidate->GetParentNode()) {
                const std::string_view id(candidate->GetId());
                if ((_activeDocument == _inventoryDocument &&
                     id == "inventory-preview-hit-zone") ||
                    (_activeDocument == _magicDocument &&
                     id == "magic-preview-hit-zone")) {
                    _previewInteractionZoneHovered = true;
                    break;
                }
            }
            while (hovered && hovered != _activeDocument &&
                   hovered->GetTagName() != "button" &&
                   hovered->GetTagName() != "input" &&
                   !IsActionCard(hovered)) {
                hovered = hovered->GetParentNode();
            }
            if (hovered && hovered != _activeDocument && !hovered->GetId().empty()) {
                HandleHover(hovered->GetId().c_str());
            } else {
                _hoveredElementId.clear();
            }
            UpdateCursor(true, pointerX, pointerY);
            _pointerWasOnPanel = true;
        } else if (_pointerWasOnPanel) {
            _context->ProcessMouseLeave();
            UpdateCursor(false, 0, 0);
            _pointerWasOnPanel = false;
            _previewInteractionZoneHovered = false;
            _hoveredElementId.clear();
        } else if (!pointerOnPanel) {
            _previewInteractionZoneHovered = false;
        }
        UpdateInventoryMarquee(deltaTime);

        // Keep the global DragonBoard grip mapping intact. Smooth the scroll
        // locally while grip is held, then discard all pending motion on
        // release so the next trigger press can never inherit inertia.
        const bool scrollArmed = gripDown && !triggerDown;
        if (scrollArmed && pointerOnPanel) {
            if (!_gripScrollActive) {
                _gripScrollActive = true;
                _gripScrollPointerY = pointerY;
                _gripPointerScrollAccumulator = 0.0f;
                auto* hovered = _context->GetHoverElement();
                _gripScrollTarget = hovered ? hovered->GetClosestScrollableContainer() : nullptr;
                if (!_gripScrollTarget && _activeDocument) {
                    _gripScrollTarget = _activeDocument->GetElementById("page-scroll");
                }
                _gripScrollTargetTop = _gripScrollTarget ?
                    _gripScrollTarget->GetScrollTop() : 0.0f;
            } else {
                const float pointerDelta = static_cast<float>(pointerY - _gripScrollPointerY);
                _gripScrollPointerY = pointerY;
                constexpr float pointerFilter = 0.32f;
                _gripPointerScrollAccumulator +=
                    (pointerDelta - _gripPointerScrollAccumulator) * pointerFilter;
                constexpr float pointerSensitivity = 0.78f;
                _gripScrollTargetTop -= _gripPointerScrollAccumulator * pointerSensitivity;
            }

            if (_gripScrollTarget) {
                constexpr float stickDeadzone = 0.15f;
                constexpr float stickPixelsPerFrame = 9.0f;
                if (std::abs(stickY) > stickDeadzone) {
                    _gripScrollTargetTop += stickY * stickPixelsPerFrame;
                }
                const float maximum = std::max(
                    0.0f,
                    _gripScrollTarget->GetScrollHeight() - _gripScrollTarget->GetClientHeight());
                _gripScrollTargetTop = std::clamp(_gripScrollTargetTop, 0.0f, maximum);

                const float current = _gripScrollTarget->GetScrollTop();
                const float difference = _gripScrollTargetTop - current;
                if (std::abs(difference) >= 0.5f) {
                    constexpr float easing = 0.24f;
                    float step = difference * easing;
                    if (std::abs(step) < 1.0f) step = std::copysign(1.0f, step);
                    _gripScrollTarget->SetScrollTop(current + step);
                }
            }
        } else {
            _gripScrollActive = false;
            _gripScrollTarget = nullptr;
            _gripScrollTargetTop = 0.0f;
            _gripPointerScrollAccumulator = 0.0f;
        }

        if (triggerDown != _previousTriggerDown) {
            logger::info(
                "DragonBoardVR: RmlUi trigger {} (pointerOnPanel={}, x={}, y={}).",
                triggerDown ? "down" : "up",
                pointerOnPanel,
                pointerX,
                pointerY);
            if (!triggerDown || pointerOnPanel) {
                if (triggerDown) {
                    _triggerCapturedSliderId.clear();
                    _sliderPointerInitialized = false;
                    _triggerCapturedActionId.clear();
                    _triggerCaptureProgrammatic = false;
                    auto* captureElement = _context->GetHoverElement();
                    while (captureElement && captureElement != _activeDocument &&
                           captureElement->GetTagName() != "button" &&
                           captureElement->GetTagName() != "input" &&
                           !IsActionCard(captureElement)) {
                        captureElement = captureElement->GetParentNode();
                    }
                    if (captureElement && captureElement->GetTagName() == "input") {
                        const auto inputType = captureElement->GetAttribute<Rml::String>(
                            "type", "text");
                        _triggerCaptureMode = inputType == "range" ?
                            TriggerCaptureMode::kSlider : TriggerCaptureMode::kButton;
                    } else if (captureElement && captureElement != _activeDocument) {
                        _triggerCaptureMode = TriggerCaptureMode::kButton;
                        const std::string_view captureId(captureElement->GetId());
                        if ((_activeDocument == _inventoryDocument &&
                             captureId.starts_with("inventory-item-")) ||
                            (_activeDocument == _magicDocument &&
                             captureId.starts_with("magic-spell-"))) {
                            _triggerCapturedActionId = std::string(captureId);
                            _triggerCaptureProgrammatic = true;
                        }
                    } else {
                        _triggerCaptureMode = TriggerCaptureMode::kNone;
                    }
                    if (_triggerCaptureMode == TriggerCaptureMode::kNone) {
                        TriggerCaptureMode fallbackMode = TriggerCaptureMode::kNone;
                        if (auto* interactive = FindInteractiveAtPoint(
                                pointerX, pointerY, fallbackMode)) {
                            _triggerCaptureMode = fallbackMode;
                            if (fallbackMode == TriggerCaptureMode::kSlider) {
                                _triggerCapturedSliderId = interactive->GetId();
                            } else if (fallbackMode == TriggerCaptureMode::kButton) {
                                _triggerCapturedActionId = interactive->GetId();
                                _triggerCaptureProgrammatic = true;
                            }
                        }
                    } else if (_triggerCaptureMode == TriggerCaptureMode::kSlider) {
                        _triggerCapturedSliderId = captureElement->GetId();
                    }
                    _triggerCaptureX = pointerX;
                    _triggerCaptureY = pointerY;
                    const char* captureName = _triggerCaptureMode == TriggerCaptureMode::kSlider ?
                        "slider" : (_triggerCaptureMode == TriggerCaptureMode::kButton ? "button" : "none");
                    logger::info("DragonBoardVR: RmlUi trigger captured {} target.", captureName);
                    if (_triggerCaptureMode != TriggerCaptureMode::kSlider &&
                        !_triggerCaptureProgrammatic) {
                        _context->ProcessMouseButtonDown(0, 0);
                    }
                } else {
                    if (_triggerCaptureMode == TriggerCaptureMode::kSlider) {
                        UpdateCapturedSlider(pointerX);
                    } else if (_inventoryLongPressTriggered) {
                        // Inventory list items use programmatic capture, so a
                        // long press favorites without also selecting it when
                        // the trigger is released.
                    } else if (_triggerCaptureProgrammatic && !_triggerCapturedActionId.empty()) {
                        HandleClick(_triggerCapturedActionId.c_str());
                    } else {
                        _context->ProcessMouseButtonUp(0, 0);
                    }
                    _triggerScrollReleasePending = true;
                    _triggerCaptureMode = TriggerCaptureMode::kNone;
                    _triggerCapturedSliderId.clear();
                    _sliderPointerInitialized = false;
                    _triggerCapturedActionId.clear();
                    _triggerCaptureProgrammatic = false;
                    ResetInventoryLongPress();
                }
            }
            _previousTriggerDown = triggerDown;
        }

        if (triggerDown && _triggerCaptureMode == TriggerCaptureMode::kSlider) {
            UpdateCapturedSlider(pointerX);
        }
        UpdateInventoryLongPress(deltaTime);

        (void)stickX;
    }

    bool DragonBoardRmlUi::RequiresContinuousRendering() const
    {
        return _pointerMotionActive || _inventoryMarqueeActive || _gripScrollActive ||
               _triggerCaptureMode == TriggerCaptureMode::kSlider ||
               !_inventoryLongPressElementId.empty();
    }

    Rml::Element* DragonBoardRmlUi::FindInteractiveAtPoint(
        int x, int y, TriggerCaptureMode& mode) const
    {
        mode = TriggerCaptureMode::kNone;
        if (!_activeDocument) return nullptr;
        const Rml::Vector2f point(static_cast<float>(x), static_cast<float>(y));
        Rml::Element* best = nullptr;
        float bestArea = std::numeric_limits<float>::max();
        for (const auto& binding : _interactiveBindings) {
            if (binding.document != _activeDocument) continue;
            auto* element = binding.document->GetElementById(binding.id);
            if (!element || !element->IsVisible(true) ||
                !element->IsPointWithinElement(point)) {
                continue;
            }
            const float area = element->GetOffsetWidth() * element->GetOffsetHeight();
            if (area <= 0.0f || area >= bestArea) continue;
            best = element;
            bestArea = area;
            mode = binding.mode;
        }
        return best;
    }

    void DragonBoardRmlUi::RegisterInteractive(
        Rml::ElementDocument* document, const char* id, TriggerCaptureMode mode)
    {
        if (!document || !id || !*id) return;
        const auto existing = std::find_if(
            _interactiveBindings.begin(), _interactiveBindings.end(),
            [document, id](const InteractiveBinding& binding) {
                return binding.document == document && binding.id == id;
            });
        if (existing != _interactiveBindings.end()) {
            existing->mode = mode;
            return;
        }
        _interactiveBindings.push_back({ document, id, mode });
    }

    void DragonBoardRmlUi::RegisterDocumentInteractives(Rml::ElementDocument* document)
    {
        if (!document) return;

        Rml::ElementList buttons;
        document->GetElementsByTagName(buttons, "button");
        for (auto* element : buttons) {
            if (element && !element->GetId().empty()) {
                BindClick(document, element->GetId().c_str());
            }
        }

        Rml::ElementList inputs;
        document->GetElementsByTagName(inputs, "input");
        for (auto* element : inputs) {
            if (!element || element->GetId().empty()) continue;
            const auto type = element->GetAttribute<Rml::String>("type", "text");
            if (type == "range") BindSlider(document, element->GetId().c_str());
            else BindClick(document, element->GetId().c_str());
        }
    }

    DragonBoardRmlUi::RegisteredPanel* DragonBoardRmlUi::FindPanel(std::uint32_t handle)
    {
        const auto it = _registeredPanels.find(handle);
        return it != _registeredPanels.end() ? &it->second : nullptr;
    }

    const DragonBoardRmlUi::RegisteredPanel* DragonBoardRmlUi::FindPanel(
        std::uint32_t handle) const
    {
        const auto it = _registeredPanels.find(handle);
        return it != _registeredPanels.end() ? &it->second : nullptr;
    }

    std::uint32_t DragonBoardRmlUi::FindPanelHandle(Rml::ElementDocument* document) const
    {
        for (const auto& [handle, panel] : _registeredPanels) {
            if (panel.document == document) return handle;
        }
        return 0;
    }

    void DragonBoardRmlUi::HideAllDocuments()
    {
        if (_settingsDocument) _settingsDocument->Hide();
        if (_developerDocument) _developerDocument->Hide();
        if (_itemEditDocument) _itemEditDocument->Hide();
        if (_modsDocument) _modsDocument->Hide();
        if (_inventoryDocument) _inventoryDocument->Hide();
        if (_magicDocument) _magicDocument->Hide();
        if (_journalDocument) _journalDocument->Hide();
        for (auto& [handle, panel] : _registeredPanels) {
            (void)handle;
            if (panel.document) panel.document->Hide();
        }
    }

    void DragonBoardRmlUi::UpdateCapturedSlider(int pointerX)
    {
        if (!_activeDocument || _triggerCapturedSliderId.empty()) return;
        auto* element = _activeDocument->GetElementById(_triggerCapturedSliderId);
        if (!element) return;

        if (!_sliderPointerInitialized) {
            _sliderAcceptedPointerX = pointerX;
            _sliderSmoothedPointerX = static_cast<float>(pointerX);
            _sliderPointerInitialized = true;
        } else {
            constexpr int sliderDeadzonePixels = 4;
            if (std::abs(pointerX - _sliderAcceptedPointerX) >= sliderDeadzonePixels) {
                _sliderAcceptedPointerX = pointerX;
            }

            const float difference =
                static_cast<float>(_sliderAcceptedPointerX) - _sliderSmoothedPointerX;
            const float distance = std::abs(difference);
            const float response =
                distance > 60.0f ? 0.48f : (distance > 20.0f ? 0.32f : 0.18f);
            _sliderSmoothedPointerX += difference * response;
            if (std::abs(
                    static_cast<float>(_sliderAcceptedPointerX) - _sliderSmoothedPointerX) <
                0.1f) {
                _sliderSmoothedPointerX = static_cast<float>(_sliderAcceptedPointerX);
            }
        }

        const float width = element->GetClientWidth();
        if (width <= 0.0f) return;
        const float minimum = std::stof(element->GetAttribute<Rml::String>("min", "0"));
        const float maximum = std::stof(element->GetAttribute<Rml::String>("max", "1"));
        const float step = std::stof(element->GetAttribute<Rml::String>("step", "0"));
        const float fraction = std::clamp(
            (_sliderSmoothedPointerX - element->GetAbsoluteLeft()) / width,
            0.0f,
            1.0f);
        float value = minimum + fraction * (maximum - minimum);
        if (step > 0.0f) value = minimum + std::round((value - minimum) / step) * step;
        value = std::clamp(value, minimum, maximum);

        const float previous = std::stof(element->GetAttribute<Rml::String>("value", "0"));
        if (std::abs(previous - value) < 0.0001f) return;
        _synchronizingSliderValues = true;
        element->SetAttribute("value", Rml::CreateString("%.6f", value));
        _synchronizingSliderValues = false;
        HandleSliderChange(_triggerCapturedSliderId.c_str(), value);
    }

    void DragonBoardRmlUi::ResetInventoryMarquee()
    {
        if (_activeDocument && !_inventoryMarqueeElementId.empty()) {
            if (auto* element =
                    _activeDocument->GetElementById(_inventoryMarqueeElementId)) {
                element->SetProperty("left", "0px");
            }
        }
        _inventoryMarqueeElementId.clear();
        _inventoryMarqueeOffset = 0.0f;
        _inventoryMarqueePause = 0.0f;
        _inventoryMarqueeAtEnd = false;
        _inventoryMarqueeActive = false;
    }

    void DragonBoardRmlUi::UpdateInventoryMarquee(float deltaTime)
    {
        const bool inventoryActive = _activeDocument == _inventoryDocument;
        const bool magicActive = _activeDocument == _magicDocument;
        const bool journalActive = _activeDocument == _journalDocument;
        std::string_view itemPrefix;
        std::string_view viewportPrefix;
        std::string_view trackPrefix;
        if (inventoryActive) {
            itemPrefix = "inventory-item-";
            viewportPrefix = "inventory-item-name-";
            trackPrefix = "inventory-item-name-track-";
        } else if (magicActive) {
            itemPrefix = "magic-spell-";
            viewportPrefix = "magic-spell-name-";
            trackPrefix = "magic-spell-name-track-";
        } else if (journalActive) {
            itemPrefix = "journal-quest-";
            viewportPrefix = "journal-quest-name-";
            trackPrefix = "journal-quest-name-track-";
        } else {
            ResetInventoryMarquee();
            return;
        }

        if (!_hoveredElementId.starts_with(itemPrefix)) {
            ResetInventoryMarquee();
            return;
        }

        const auto suffix = std::string_view(_hoveredElementId).substr(itemPrefix.size());
        if (suffix.empty()) {
            ResetInventoryMarquee();
            return;
        }
        const std::string viewportId = std::string(viewportPrefix) + std::string(suffix);
        const std::string trackId = std::string(trackPrefix) + std::string(suffix);
        if (_inventoryMarqueeElementId != trackId) {
            ResetInventoryMarquee();
            _inventoryMarqueeElementId = trackId;
            _inventoryMarqueePause = 0.65f;
        }

        auto* viewport = _activeDocument ?
            _activeDocument->GetElementById(viewportId) : nullptr;
        auto* track = _activeDocument ?
            _activeDocument->GetElementById(_inventoryMarqueeElementId) : nullptr;
        if (!viewport || !track) {
            ResetInventoryMarquee();
            return;
        }

        const auto text = track->GetInnerRML();
        const float textWidth = static_cast<float>(
            Rml::ElementUtilities::GetStringWidth(track, text));
        const float maximum = std::max(
            0.0f, textWidth - viewport->GetClientWidth());
        if (maximum <= 1.0f) {
            track->SetProperty("left", "0px");
            _inventoryMarqueeOffset = 0.0f;
            _inventoryMarqueeActive = false;
            return;
        }
        _inventoryMarqueeActive = true;

        const float frameTime = std::clamp(deltaTime, 0.0f, 0.05f);
        if (_inventoryMarqueePause > 0.0f) {
            _inventoryMarqueePause =
                std::max(0.0f, _inventoryMarqueePause - frameTime);
            return;
        }

        if (_inventoryMarqueeAtEnd) {
            _inventoryMarqueeAtEnd = false;
            _inventoryMarqueeOffset = 0.0f;
            _inventoryMarqueePause = 0.65f;
            track->SetProperty("left", "0px");
            return;
        }

        constexpr float pixelsPerSecond = 62.0f;
        _inventoryMarqueeOffset = std::min(
            maximum, _inventoryMarqueeOffset + pixelsPerSecond * frameTime);
        track->SetProperty(
            "left", Rml::CreateString("%.1fpx", -_inventoryMarqueeOffset));
        if (_inventoryMarqueeOffset >= maximum) {
            _inventoryMarqueeAtEnd = true;
            _inventoryMarqueePause = 0.80f;
        }
    }

    void DragonBoardRmlUi::ResetInventoryLongPress()
    {
        _inventoryLongPressElementId.clear();
        _inventoryLongPressTimer = 0.0f;
        _inventoryLongPressTriggered = false;
    }

    void DragonBoardRmlUi::UpdateInventoryLongPress(float deltaTime)
    {
        const bool inventoryActive = _activeDocument == _inventoryDocument;
        const bool magicActive = _activeDocument == _magicDocument;
        const std::string_view itemPrefix =
            inventoryActive ? "inventory-item-" : "magic-spell-";
        if ((!inventoryActive && !magicActive) ||
            !_currentTriggerDown ||
            _triggerCaptureMode != TriggerCaptureMode::kButton ||
            !_triggerCapturedActionId.starts_with(itemPrefix)) {
            if (!_currentTriggerDown) {
                ResetInventoryLongPress();
            }
            return;
        }

        if (_inventoryLongPressElementId != _triggerCapturedActionId) {
            ResetInventoryLongPress();
            _inventoryLongPressElementId = _triggerCapturedActionId;
        }
        if (_inventoryLongPressTriggered) return;

        _inventoryLongPressTimer += std::clamp(deltaTime, 0.0f, 0.05f);
        if (_inventoryLongPressTimer < 1.0f) return;

        try {
            const auto index = static_cast<std::size_t>(
                std::stoull(_triggerCapturedActionId.substr(itemPrefix.size())));
            if (inventoryActive) {
                _inventoryActionIndex = index;
                _inventoryAction = InventoryAction::kFavorite;
            } else {
                _magicActionIndex = index;
                _magicAction = MagicAction::kFavorite;
            }
            _inventoryLongPressTriggered = true;
            RequestHaptic(HapticCue::kStrong);
        } catch (...) {
            logger::warn(
                "DragonBoardVR: invalid list long-press id '{}'.",
                _triggerCapturedActionId);
            ResetInventoryLongPress();
        }
    }

    void DragonBoardRmlUi::BeginTriggerScrollLock()
    {
        _triggerScrollLockDocument = _activeDocument;
        auto* page = _activeDocument ? _activeDocument->GetElementById("page-scroll") : nullptr;
        auto* nested = _activeDocument ? _activeDocument->GetElementById("dev-command-list") : nullptr;
        _triggerScrollLockPageTop = page ? page->GetScrollTop() : 0.0f;
        _triggerScrollLockNestedTop = nested ? nested->GetScrollTop() : 0.0f;
        _triggerScrollLockActive = true;
        _triggerScrollReleasePending = false;
        _triggerScrollSuppressionLogged = false;
    }

    void DragonBoardRmlUi::RestoreTriggerScrollLock()
    {
        if (!_triggerScrollLockActive) return;
        if (!_activeDocument || _activeDocument != _triggerScrollLockDocument) {
            _triggerScrollLockDocument = nullptr;
            _triggerScrollLockActive = false;
            _triggerScrollReleasePending = false;
            return;
        }

        auto* page = _activeDocument->GetElementById("page-scroll");
        auto* nested = _activeDocument->GetElementById("dev-command-list");
        const bool pageMoved = page && page->GetScrollTop() != _triggerScrollLockPageTop;
        const bool nestedMoved = nested && nested->GetScrollTop() != _triggerScrollLockNestedTop;
        if (pageMoved) page->SetScrollTop(_triggerScrollLockPageTop);
        if (nestedMoved) nested->SetScrollTop(_triggerScrollLockNestedTop);

        if ((pageMoved || nestedMoved) && !_triggerScrollSuppressionLogged) {
            logger::info(
                "DragonBoardVR: suppressed RmlUi trigger drag scroll (page={}, nested={}).",
                pageMoved,
                nestedMoved);
            _triggerScrollSuppressionLogged = true;
        }

        if (_triggerScrollReleasePending) {
            _triggerScrollLockDocument = nullptr;
            _triggerScrollLockActive = false;
            _triggerScrollReleasePending = false;
        }
    }

    void DragonBoardRmlUi::TraceScrollState()
    {
        if (!_activeDocument) return;

        auto* page = _activeDocument->GetElementById("page-scroll");
        auto* nested = _activeDocument->GetElementById("dev-command-list");
        const float pageTop = page ? page->GetScrollTop() : 0.0f;
        const float nestedTop = nested ? nested->GetScrollTop() : 0.0f;

        if (_observedScrollDocument != _activeDocument) {
            _observedScrollDocument = _activeDocument;
            _observedPageScrollTop = pageTop;
            _observedNestedScrollTop = nestedTop;
            return;
        }

        if (pageTop != _observedPageScrollTop) {
            logger::info(
                "DragonBoardVR: RmlUi page scrollTop {:.1f} -> {:.1f} (trigger={}, grip={}).",
                _observedPageScrollTop,
                pageTop,
                _currentTriggerDown,
                _currentGripDown);
            _observedPageScrollTop = pageTop;
        }
        if (nestedTop != _observedNestedScrollTop) {
            logger::info(
                "DragonBoardVR: RmlUi nested scrollTop {:.1f} -> {:.1f} (trigger={}, grip={}).",
                _observedNestedScrollTop,
                nestedTop,
                _currentTriggerDown,
                _currentGripDown);
            _observedNestedScrollTop = nestedTop;
        }
    }

    bool DragonBoardRmlUi::Render(ID3D11RenderTargetView* renderTarget, int width, int height)
    {
        if (!IsReady()) return false;
        _lastRenderTiming = {};
        _lastRenderTiming.width = width;
        _lastRenderTiming.height = height;
        if (_activeDocument == _settingsDocument) _lastRenderTiming.activeDocument = "Settings";
        else if (_activeDocument == _developerDocument) _lastRenderTiming.activeDocument = "Developer";
        else if (_activeDocument == _itemEditDocument) _lastRenderTiming.activeDocument = "Item Editor";
        else if (_activeDocument == _modsDocument) _lastRenderTiming.activeDocument = "Mods";
        else if (_activeDocument == _inventoryDocument) _lastRenderTiming.activeDocument = "Inventory";
        else if (_activeDocument == _magicDocument) _lastRenderTiming.activeDocument = "Magic";
        else if (_activeDocument == _journalDocument) _lastRenderTiming.activeDocument = "Journal";
        else if (const auto handle = FindPanelHandle(_activeDocument); handle != 0) {
            if (const auto* panel = FindPanel(handle)) {
                _lastRenderTiming.activeDocument = panel->id;
            }
        }
        if (_lastRenderTiming.activeDocument.empty()) {
            _lastRenderTiming.activeDocument = "<none>";
        }

        const Rml::Vector2i dimensions(width, height);
        if (_context->GetDimensions() != dimensions) {
            _context->SetDimensions(dimensions);
        }
        const auto updateStarted = std::chrono::steady_clock::now();
        const bool updated = _context->Update();
        const auto updateEnded = std::chrono::steady_clock::now();
        _lastRenderTiming.updateMs = Milliseconds(updateStarted, updateEnded);
        if (!updated) {
            _lastRenderTiming.totalMs = _lastRenderTiming.updateMs;
            return false;
        }
        RestoreTriggerScrollLock();
        TraceScrollState();

        const auto now = std::chrono::steady_clock::now();
        if (_activeDocument != _lastDomCountDocument ||
            _lastDomCountSample.time_since_epoch().count() == 0 ||
            now - _lastDomCountSample >= std::chrono::milliseconds(250)) {
            _lastDomElementCount = CountDomElements(_activeDocument);
            _lastDomCountDocument = _activeDocument;
            _lastDomCountSample = now;
        }
        _lastRenderTiming.domElements = _lastDomElementCount;

        const auto beginStarted = std::chrono::steady_clock::now();
        const bool beganFrame = _renderer->BeginFrame(renderTarget, width, height);
        const auto beginEnded = std::chrono::steady_clock::now();
        _lastRenderTiming.beginFrameMs = Milliseconds(beginStarted, beginEnded);
        if (!beganFrame) {
            _lastRenderTiming.totalMs =
                _lastRenderTiming.updateMs + _lastRenderTiming.beginFrameMs;
            return false;
        }
        try {
            const auto renderStarted = std::chrono::steady_clock::now();
            const bool rendered = _context->Render();
            const auto renderEnded = std::chrono::steady_clock::now();
            _lastRenderTiming.renderMs = Milliseconds(renderStarted, renderEnded);

            const auto endStarted = std::chrono::steady_clock::now();
            _renderer->EndFrame();
            const auto endEnded = std::chrono::steady_clock::now();
            _lastRenderTiming.endFrameMs = Milliseconds(endStarted, endEnded);
            _lastRenderTiming.totalMs = _lastRenderTiming.updateMs +
                _lastRenderTiming.beginFrameMs + _lastRenderTiming.renderMs +
                _lastRenderTiming.endFrameMs;
            _lastRenderTiming.drawCalls = _renderer->GetDrawCallCount();
            return rendered;
        } catch (...) {
            const auto endStarted = std::chrono::steady_clock::now();
            _renderer->EndFrame();
            const auto endEnded = std::chrono::steady_clock::now();
            _lastRenderTiming.endFrameMs = Milliseconds(endStarted, endEnded);
            _lastRenderTiming.totalMs = _lastRenderTiming.updateMs +
                _lastRenderTiming.beginFrameMs + _lastRenderTiming.renderMs +
                _lastRenderTiming.endFrameMs;
            _lastRenderTiming.drawCalls = _renderer->GetDrawCallCount();
            throw;
        }
    }

    bool DragonBoardRmlUi::ConsumeCloseRequested()
    {
        return std::exchange(_closeRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeSaveRequested()
    {
        return std::exchange(_saveRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeDeveloperPanelToggleRequested()
    {
        return std::exchange(_developerPanelToggleRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeEditModeToggleRequested()
    {
        return std::exchange(_editModeToggleRequested, false);
    }

    DragonBoardRmlUi::HapticCue DragonBoardRmlUi::ConsumeHapticCue()
    {
        return std::exchange(_pendingHapticCue, HapticCue::kNone);
    }

    std::optional<DragonBoardRmlUi::SliderChange> DragonBoardRmlUi::ConsumeSliderChange()
    {
        auto result = std::move(_sliderChange);
        _sliderChange.reset();
        return result;
    }

    std::optional<std::size_t> DragonBoardRmlUi::ConsumeDeveloperCommandRequested()
    {
        auto result = _developerCommandRequested;
        _developerCommandRequested.reset();
        return result;
    }

    bool DragonBoardRmlUi::ConsumeDeveloperAddCommandRequested()
    {
        return std::exchange(_developerAddCommandRequested, false);
    }

    DragonBoardRmlUi::ItemEditAction DragonBoardRmlUi::ConsumeItemEditAction()
    {
        return std::exchange(_itemEditAction, ItemEditAction::kNone);
    }

    std::pair<DragonBoardRmlUi::ModsAction, std::size_t> DragonBoardRmlUi::ConsumeModsAction()
    {
        const auto result = std::pair{ _modsAction, _modsActionIndex };
        _modsAction = ModsAction::kNone;
        _modsActionIndex = 0;
        return result;
    }

    std::pair<DragonBoardRmlUi::InventoryAction, std::size_t>
    DragonBoardRmlUi::ConsumeInventoryAction()
    {
        const auto result = std::pair{ _inventoryAction, _inventoryActionIndex };
        _inventoryAction = InventoryAction::kNone;
        _inventoryActionIndex = 0;
        return result;
    }

    std::pair<DragonBoardRmlUi::MagicAction, std::size_t>
    DragonBoardRmlUi::ConsumeMagicAction()
    {
        const auto result = std::pair{ _magicAction, _magicActionIndex };
        _magicAction = MagicAction::kNone;
        _magicActionIndex = 0;
        return result;
    }

    DragonBoardRmlUi::JournalActionRequest DragonBoardRmlUi::ConsumeJournalAction()
    {
        const JournalActionRequest result{
            _journalAction,
            _journalActionFormID,
            _journalActionInstanceID,
            _journalActionObjectiveInstanceID,
            _journalActionObjectiveID
        };
        _journalAction = JournalAction::kNone;
        _journalActionFormID = 0;
        _journalActionInstanceID = 0;
        _journalActionObjectiveInstanceID = 0;
        _journalActionObjectiveID = 0;
        return result;
    }

    std::optional<std::size_t> DragonBoardRmlUi::GetHoveredModsIndex() const
    {
        constexpr std::string_view prefix = "mods-card-";
        if (_activeDocument != _modsDocument || !_hoveredElementId.starts_with(prefix)) {
            return std::nullopt;
        }
        try {
            return static_cast<std::size_t>(std::stoull(_hoveredElementId.substr(prefix.size())));
        } catch (...) {
            return std::nullopt;
        }
    }

    void DragonBoardRmlUi::SetMods(const std::vector<std::string>& labels)
    {
        if (!_modsDocument) return;
        auto* list = _modsDocument->GetElementById("mods-list");
        if (!list) return;
        std::string markup;
        if (labels.empty()) {
            markup = "<div class=\"mods-empty\">No mods added yet</div>";
        } else {
            for (std::size_t i = 0; i < labels.size(); ++i) {
                markup += "<div id=\"mods-card-" + std::to_string(i) +
                    "\" class=\"mod-card\" tabindex=\"0\"><span class=\"mod-card-mark\">&lt;&gt;</span>"
                    "<span class=\"mod-card-label\">" +
                    EscapeRml(labels[i]) + "</span></div>";
            }
        }
        if (markup != _modsListMarkup) {
            list->SetInnerRML(markup);
            _modsListMarkup = std::move(markup);
            _hoveredElementId.clear();
            for (std::size_t i = 0; i < labels.size(); ++i) {
                BindClick(_modsDocument, ("mods-card-" + std::to_string(i)).c_str());
            }
        }
    }

    void DragonBoardRmlUi::SetInventory(const InventoryInfo& info)
    {
        if (!_inventoryDocument) return;

        const auto setText = [this](const char* id, const std::string& value) {
            if (auto* element = _inventoryDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };

        setText(
            "inventory-player-name",
            info.playerName.empty() ? "Dragonborn" : info.playerName);
        setText("inventory-player-level", std::to_string(info.playerLevel));
        setText("inventory-gold", std::to_string(info.gold));
        setText(
            "inventory-carry-weight",
            Rml::CreateString("%.1f / %.0f", info.currentWeight, info.carryWeight));
        setText("inventory-item-count", std::to_string(info.items.size()));
        setText(
            "inventory-search-text",
            info.searchQuery.empty() ? "SEARCH" : info.searchQuery);
        if (auto* search = _inventoryDocument->GetElementById("inventory-search")) {
            search->SetClass("active", !info.searchQuery.empty());
        }
        if (auto* clear = _inventoryDocument->GetElementById("inventory-search-clear")) {
            clear->SetProperty(
                "display", info.searchQuery.empty() ? "none" : "flex");
        }

        constexpr std::array<std::pair<const char*, const char*>, 6> kInventoryFilters{ {
            { "inventory-filter-weapons", "weapons" },
            { "inventory-filter-armor", "armor" },
            { "inventory-filter-consumables", "consumables" },
            { "inventory-filter-quest", "quest" },
            { "inventory-filter-books", "books" },
            { "inventory-filter-misc", "misc" }
        } };
        for (const auto& [id, filter] : kInventoryFilters) {
            if (auto* element = _inventoryDocument->GetElementById(id)) {
                element->SetClass("active", info.activeFilter == filter);
            }
        }

        if (auto* list = _inventoryDocument->GetElementById("inventory-item-list")) {
            std::string markup;
            if (info.items.empty()) {
                markup = info.searchQuery.empty() ?
                    "<div class=\"inventory-empty\">Inventory is empty</div>" :
                    "<div class=\"inventory-empty\">No matching items</div>";
            } else {
                for (std::size_t index = 0; index < info.items.size(); ++index) {
                    const auto& item = info.items[index];
                    std::string classes = "inventory-list-item";
                    if (item.equipped) classes += " equipped";
                    if (item.favorited) classes += " favorited";
                    markup += "<button id=\"inventory-item-" + std::to_string(index) +
                        "\" class=\"" + classes + "\">"
                        "<span class=\"item-state-mark\">" +
                        EscapeRml(item.equipmentMarker) +
                        "</span><span id=\"inventory-item-name-" +
                        std::to_string(index) + "\" class=\"item-name\">" +
                        "<span id=\"inventory-item-name-track-" +
                        std::to_string(index) + "\" class=\"item-name-track\">" +
                        EscapeRml(item.name) +
                        "</span></span><span class=\"item-stack\">" +
                        (item.count > 1 ? "x" + std::to_string(item.count) : "") +
                        "</span></button>";
                }
            }

            const bool rebuildList =
                !_inventoryListInitialized || markup != _inventoryListMarkup;
            if (rebuildList) {
                ResetInventoryMarquee();
                list->SetInnerRML(markup);
                for (std::size_t index = 0; index < info.items.size(); ++index) {
                    BindClick(
                        _inventoryDocument,
                        ("inventory-item-" + std::to_string(index)).c_str());
                }
                _inventoryListMarkup = std::move(markup);
                _inventoryListInitialized = true;
                _inventoryListSelectedIndex = static_cast<std::size_t>(-1);
            }

            const auto selectedIndex = info.items.empty() ?
                static_cast<std::size_t>(-1) :
                std::min(info.selectedIndex, info.items.size() - 1);
            if (_inventoryListSelectedIndex != selectedIndex) {
                if (_inventoryListSelectedIndex != static_cast<std::size_t>(-1)) {
                    if (auto* previous = _inventoryDocument->GetElementById(
                            ("inventory-item-" +
                             std::to_string(_inventoryListSelectedIndex)).c_str())) {
                        previous->SetClass("active", false);
                    }
                }
                if (selectedIndex != static_cast<std::size_t>(-1)) {
                    if (auto* selected = _inventoryDocument->GetElementById(
                            ("inventory-item-" + std::to_string(selectedIndex)).c_str())) {
                        selected->SetClass("active", true);
                    }
                }
                _inventoryListSelectedIndex = selectedIndex;
            }
        }

        if (info.items.empty()) {
            setText("inventory-selected-category", "ITEM");
            setText("inventory-selected-name", "Inventory is empty");
            setText("inventory-attack", "--");
            setText("inventory-defense", "--");
            setText("inventory-weight", "--");
            setText("inventory-value", "--");
            setText("inventory-count", "--");
            setText("inventory-description", "No description available.");
            setText("inventory-equip-label", "EQUIP");
            if (auto* equip = _inventoryDocument->GetElementById("inventory-equip")) {
                equip->SetClass("disabled", true);
            }
            if (auto* drop = _inventoryDocument->GetElementById("inventory-drop")) {
                drop->SetClass("disabled", true);
            }
            if (auto* pin = _inventoryDocument->GetElementById("inventory-pin")) {
                pin->SetClass("disabled", true);
            }
            if (auto* left = _inventoryDocument->GetElementById("inventory-left-hand-state")) {
                left->SetClass("active", false);
            }
            if (auto* right = _inventoryDocument->GetElementById("inventory-right-hand-state")) {
                right->SetClass("active", false);
            }
            return;
        }

        const auto selectedIndex = std::min(info.selectedIndex, info.items.size() - 1);
        const auto& selected = info.items[selectedIndex];
        setText("inventory-selected-category", selected.category);
        setText("inventory-selected-name", selected.name);
        setText(
            "inventory-attack",
            selected.hasAttack ? Rml::CreateString("%.0f", selected.attack) : "--");
        setText(
            "inventory-defense",
            selected.hasDefense ? Rml::CreateString("%.0f", selected.defense) : "--");
        setText("inventory-weight", Rml::CreateString("%.1f", selected.weight));
        setText("inventory-value", std::to_string(selected.value));
        setText("inventory-count", std::to_string(selected.count));
        setText("inventory-description", selected.description);
        setText("inventory-equip-label", selected.equipped ? "UNEQUIP" : "EQUIP");

        if (auto* left = _inventoryDocument->GetElementById("inventory-left-hand-state")) {
            left->SetClass("active", selected.equippedLeft);
        }
        if (auto* right = _inventoryDocument->GetElementById("inventory-right-hand-state")) {
            right->SetClass("active", selected.equippedRight);
        }
        if (auto* equip = _inventoryDocument->GetElementById("inventory-equip")) {
            equip->SetClass("disabled", !selected.canEquip);
        }
        if (auto* drop = _inventoryDocument->GetElementById("inventory-drop")) {
            drop->SetClass("disabled", false);
        }
        if (auto* pin = _inventoryDocument->GetElementById("inventory-pin")) {
            pin->SetClass("disabled", false);
        }
    }

    void DragonBoardRmlUi::SetMagic(const MagicInfo& info)
    {
        if (!_magicDocument) return;

        const auto setText = [this](const char* id, const std::string& value) {
            if (auto* element = _magicDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };

        setText(
            "magic-player-name",
            info.playerName.empty() ? "Dragonborn" : info.playerName);
        setText("magic-player-level", std::to_string(info.playerLevel));
        setText(
            "magic-magicka",
            Rml::CreateString("%.0f / %.0f", info.currentMagicka, info.maximumMagicka));
        setText("magic-spell-count", std::to_string(info.items.size()));
        setText(
            "magic-search-text",
            info.searchQuery.empty() ? "SEARCH" : info.searchQuery);
        if (auto* search = _magicDocument->GetElementById("magic-search")) {
            search->SetClass("active", !info.searchQuery.empty());
        }
        if (auto* clear = _magicDocument->GetElementById("magic-search-clear")) {
            clear->SetProperty("display", info.searchQuery.empty() ? "none" : "flex");
        }

        constexpr std::array<std::pair<const char*, const char*>, 7> kMagicFilters{ {
            { "magic-filter-destruction", "destruction" },
            { "magic-filter-conjuration", "conjuration" },
            { "magic-filter-restoration", "restoration" },
            { "magic-filter-illusion", "illusion" },
            { "magic-filter-alteration", "alteration" },
            { "magic-filter-powers", "powers" },
            { "magic-filter-passive", "passive" }
        } };
        for (const auto& [id, filter] : kMagicFilters) {
            if (auto* element = _magicDocument->GetElementById(id)) {
                element->SetClass("active", info.activeFilter == filter);
            }
        }

        if (auto* list = _magicDocument->GetElementById("magic-spell-list")) {
            std::string markup;
            if (info.items.empty()) {
                markup = info.searchQuery.empty() ?
                    "<div class=\"magic-empty\">No spells available</div>" :
                    "<div class=\"magic-empty\">No matching spells</div>";
            } else {
                for (std::size_t index = 0; index < info.items.size(); ++index) {
                    const auto& item = info.items[index];
                    std::string classes = "magic-list-item";
                    if (item.equipped) classes += " equipped";
                    if (item.favorited) classes += " favorited";
                    std::string marker;
                    if (item.equippedLeft && item.equippedRight) marker = "[L/R]";
                    else if (item.equippedLeft) marker = "[L]";
                    else if (item.equippedRight) marker = "[R]";
                    markup += "<button id=\"magic-spell-" + std::to_string(index) +
                        "\" class=\"" + classes + "\">"
                        "<span class=\"spell-state-mark\">" + marker +
                        "</span><span id=\"magic-spell-name-" +
                        std::to_string(index) + "\" class=\"spell-name\">"
                        "<span id=\"magic-spell-name-track-" +
                        std::to_string(index) + "\" class=\"spell-name-track\">" +
                        EscapeRml(item.name) +
                        "</span></span></button>";
                }
            }

            const bool rebuildList = !_magicListInitialized || markup != _magicListMarkup;
            if (rebuildList) {
                ResetInventoryMarquee();
                list->SetInnerRML(markup);
                for (std::size_t index = 0; index < info.items.size(); ++index) {
                    BindClick(
                        _magicDocument,
                        ("magic-spell-" + std::to_string(index)).c_str());
                }
                _magicListMarkup = std::move(markup);
                _magicListInitialized = true;
                _magicListSelectedIndex = static_cast<std::size_t>(-1);
            }

            const auto selectedIndex = info.items.empty() ?
                static_cast<std::size_t>(-1) :
                std::min(info.selectedIndex, info.items.size() - 1);
            if (_magicListSelectedIndex != selectedIndex) {
                if (_magicListSelectedIndex != static_cast<std::size_t>(-1)) {
                    if (auto* previous = _magicDocument->GetElementById(
                            ("magic-spell-" +
                             std::to_string(_magicListSelectedIndex)).c_str())) {
                        previous->SetClass("active", false);
                    }
                }
                if (selectedIndex != static_cast<std::size_t>(-1)) {
                    if (auto* selected = _magicDocument->GetElementById(
                            ("magic-spell-" + std::to_string(selectedIndex)).c_str())) {
                        selected->SetClass("active", true);
                    }
                }
                _magicListSelectedIndex = selectedIndex;
            }
        }

        const auto setHandStates = [this](bool leftActive, bool rightActive) {
            if (auto* left = _magicDocument->GetElementById("magic-left-hand-state")) {
                left->SetClass("active", leftActive);
            }
            if (auto* right = _magicDocument->GetElementById("magic-right-hand-state")) {
                right->SetClass("active", rightActive);
            }
        };
        const auto setActionsDisabled = [this](bool disabled) {
            for (const auto* id : { "magic-equip", "magic-pin" }) {
                if (auto* element = _magicDocument->GetElementById(id)) {
                    element->SetClass("disabled", disabled);
                }
            }
        };

        if (info.items.empty()) {
            setText("magic-selected-category", "MAGIC");
            setText("magic-selected-name", "No spell selected");
            setText("magic-cost", "--");
            setText("magic-skill-level", "--");
            setText("magic-cast-type", "--");
            setText("magic-target", "--");
            setText("magic-duration", "--");
            setText("magic-range", "--");
            setText("magic-description", "No description available.");
            setText("magic-equip-label", "EQUIP");
            setHandStates(false, false);
            setActionsDisabled(true);
            if (auto* icon = _magicDocument->GetElementById("magic-preview-icon")) {
                icon->SetProperty("display", "none");
            }
            if (auto* edit = _magicDocument->GetElementById("magic-edit")) {
                edit->SetClass("enabled", false);
                edit->SetClass("disabled", true);
            }
            return;
        }

        const auto selectedIndex = std::min(info.selectedIndex, info.items.size() - 1);
        const auto& selected = info.items[selectedIndex];
        setText("magic-selected-category", selected.category);
        setText("magic-selected-name", selected.name);
        setText("magic-cost", selected.canEquip ?
            Rml::CreateString("%.0f", selected.magickaCost) : "--");
        setText("magic-skill-level", selected.skillLevel);
        setText("magic-cast-type", selected.castingType);
        setText("magic-target", selected.delivery);
        setText("magic-duration", selected.duration);
        setText("magic-range", selected.range);
        setText("magic-description", selected.description);
        setText("magic-equip-label", selected.equipped ? "UNEQUIP" : "EQUIP");
        setHandStates(selected.equippedLeft, selected.equippedRight);
        if (auto* equip = _magicDocument->GetElementById("magic-equip")) {
            equip->SetClass("disabled", !selected.canEquip);
        }
        if (auto* pin = _magicDocument->GetElementById("magic-pin")) {
            pin->SetClass("disabled", false);
        }
        if (auto* edit = _magicDocument->GetElementById("magic-edit")) {
            edit->SetClass("enabled", info.editModeEnabled);
            edit->SetClass("disabled", !info.editModeEnabled);
        }
        if (auto* icon = _magicDocument->GetElementById("magic-preview-icon")) {
            icon->SetAttribute(
                "src",
                selected.iconPath.empty() ? "assets/passiveicon.png" : selected.iconPath);
            icon->SetProperty("display", selected.hasModelPreview ? "none" : "block");
        }
        if (auto* menu = _magicDocument->GetElementById("magic-pin-menu")) {
            menu->SetClass("active", false);
        }
        if (auto* pin = _magicDocument->GetElementById("magic-pin")) {
            pin->SetClass("active", false);
        }
    }

    void DragonBoardRmlUi::SetJournal(const JournalInfo& info)
    {
        if (!_journalDocument) return;

        const auto setText = [this](const char* id, const std::string& value) {
            if (auto* element = _journalDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };
        const auto buildStatRows = [](const std::vector<JournalStatInfo>& stats) {
            std::string markup;
            for (const auto& stat : stats) {
                markup += "<div class=\"journal-stat-row\"><span class=\"journal-stat-label\">" +
                    EscapeRml(stat.label) + "</span><span class=\"journal-stat-value\">" +
                    EscapeRml(stat.value) + "</span></div>";
            }
            if (markup.empty()) {
                markup = "<div class=\"journal-empty\">No statistics available.</div>";
            }
            return markup;
        };

        setText("journal-player-name", info.playerName.empty() ? "Dragonborn" : info.playerName);
        setText("journal-player-level", std::to_string(info.playerLevel));

        const auto questKey = [](const JournalQuestInfo& quest) {
            return (static_cast<std::uint64_t>(quest.formID) << 32) |
                   static_cast<std::uint64_t>(quest.instanceID);
        };
        std::vector<std::uint64_t> activeQuestKeys;
        activeQuestKeys.reserve(info.quests.size());
        for (const auto& quest : info.quests) {
            if (quest.active) activeQuestKeys.push_back(questKey(quest));
        }
        std::erase_if(
            _journalActiveQuestOrder,
            [&](const std::uint64_t key) {
                return std::ranges::find(activeQuestKeys, key) == activeQuestKeys.end();
            });
        for (const auto key : activeQuestKeys) {
            if (std::ranges::find(_journalActiveQuestOrder, key) ==
                _journalActiveQuestOrder.end()) {
                _journalActiveQuestOrder.push_back(key);
            }
        }

        std::string activeQuestTitle = "None";
        if (!_journalActiveQuestOrder.empty()) {
            const auto activeKey = _journalActiveQuestOrder.back();
            if (const auto activeQuest = std::ranges::find_if(
                    info.quests,
                    [&](const JournalQuestInfo& quest) {
                        return quest.active && questKey(quest) == activeKey;
                    });
                activeQuest != info.quests.end()) {
                activeQuestTitle = activeQuest->title;
            }
        }
        setText("journal-active-quest", "Active quest: " + activeQuestTitle);

        const auto questButtonId = [](const JournalQuestInfo& quest) {
            return "journal-quest-" + std::to_string(quest.formID) + "-" +
                   std::to_string(quest.instanceID);
        };

        if (auto* list = _journalDocument->GetElementById("journal-quest-list")) {
            std::string markup;
            const auto appendSection = [&](const char* title, bool active) {
                bool headingAdded = false;
                for (std::size_t index = 0; index < info.quests.size(); ++index) {
                    const auto& quest = info.quests[index];
                    if (quest.active != active) continue;
                    if (!headingAdded) {
                        markup += "<div class=\"journal-section-label\">" +
                            std::string(title) + "</div>";
                        headingAdded = true;
                    }
                    std::string classes = "journal-quest-button";
                    if (index == info.selectedIndex) classes += " active";
                    if (quest.completed) classes += " completed";
                    if (quest.failed) classes += " failed";
                    markup += "<button id=\"" + questButtonId(quest) +
                        "\" class=\"" + classes + "\"><span class=\"journal-quest-marker\">" +
                        (quest.active ? "&gt;" : "") +
                        "</span><span id=\"journal-quest-name-" + std::to_string(index) +
                        "\" class=\"journal-quest-title\"><span id=\"journal-quest-name-track-" +
                        std::to_string(index) + "\" class=\"journal-quest-title-track\">" +
                        EscapeRml(quest.title) + "</span></span></button>";
                }
            };
            appendSection("ACTIVE QUESTS", true);
            appendSection("INACTIVE QUESTS", false);
            if (markup.empty()) {
                markup = "<div class=\"journal-empty\">No journal quests available.</div>";
            }
            if (_journalQuestListMarkup != markup) {
                ResetInventoryMarquee();
                _journalQuestListMarkup = markup;
                list->SetInnerRML(markup);
                for (std::size_t index = 0; index < info.quests.size(); ++index) {
                    BindClick(_journalDocument, questButtonId(info.quests[index]).c_str());
                }
            }
        }

        if (info.quests.empty()) {
            _journalSelectedFormID = 0;
            _journalSelectedInstanceID = 0;
            setText("journal-quest-type", "JOURNAL");
            setText("journal-quest-title", "No quest selected");
            setText("journal-quest-summary", "Quest information will appear here.");
            setText("journal-tracking-state", "NOT TRACKED");
            if (auto* objectives = _journalDocument->GetElementById("journal-objective-list")) {
                objectives->SetInnerRML(
                    "<div class=\"journal-empty\">No objectives available.</div>");
            }
            if (auto* tracking = _journalDocument->GetElementById("journal-toggle-tracking")) {
                tracking->SetClass("disabled", true);
                tracking->SetInnerRML("<span>TRACK QUEST</span>");
            }
        } else {
            const auto selectedIndex = std::min(info.selectedIndex, info.quests.size() - 1);
            const auto& selected = info.quests[selectedIndex];
            _journalSelectedFormID = selected.formID;
            _journalSelectedInstanceID = selected.instanceID;
            setText("journal-quest-type", selected.type.empty() ? "QUEST" : selected.type);
            setText("journal-quest-title", selected.title);
            setText(
                "journal-quest-summary",
                selected.summary.empty() ? "No journal entry is available for this quest." :
                                           selected.summary);
            setText("journal-tracking-state", selected.active ? "TRACKED" : "TRACK QUEST");
            if (auto* tracking = _journalDocument->GetElementById("journal-toggle-tracking")) {
                tracking->SetClass("active", selected.active);
                tracking->SetClass("disabled", false);
                tracking->SetInnerRML(
                    selected.active ?
                        "<span>UNTRACK QUEST</span>" :
                        "<span>TRACK QUEST</span>");
            }

            if (auto* objectives = _journalDocument->GetElementById("journal-objective-list")) {
                std::string markup;
                for (std::size_t index = 0; index < selected.objectives.size(); ++index) {
                    const auto& objective = selected.objectives[index];
                    if (objective.text.find_first_not_of(" \t\r\n") == std::string::npos) {
                        continue;
                    }
                    std::string classes = "journal-objective";
                    if (objective.completed) classes += " completed";
                    if (objective.failed) classes += " failed";
                    if (objective.hasTargets) classes += " has-target";
                    const auto objectiveButtonId =
                        "journal-objective-" + std::to_string(selected.formID) + "-" +
                        std::to_string(selected.instanceID) + "-" +
                        std::to_string(objective.instanceID) + "-" +
                        std::to_string(objective.objectiveID);
                    markup += "<button id=\"" + objectiveButtonId +
                        "\" class=\"" + classes + "\"><span class=\"journal-objective-state\">" +
                        EscapeRml(objective.state) +
                        "</span><span class=\"journal-objective-text\">" +
                        EscapeRml(objective.text) +
                        "</span><span class=\"journal-map-marker\">" +
                        (objective.hasTargets ? "&gt;" : "") + "</span></button>";
                }
                if (markup.empty()) {
                    markup = "<div class=\"journal-empty\">No objectives available.</div>";
                }
                objectives->SetInnerRML(markup);
                for (std::size_t index = 0; index < selected.objectives.size(); ++index) {
                    if (selected.objectives[index].text.find_first_not_of(" \t\r\n") ==
                        std::string::npos) {
                        continue;
                    }
                    const auto& objective = selected.objectives[index];
                    const auto objectiveButtonId =
                        "journal-objective-" + std::to_string(selected.formID) + "-" +
                        std::to_string(selected.instanceID) + "-" +
                        std::to_string(objective.instanceID) + "-" +
                        std::to_string(objective.objectiveID);
                    BindClick(_journalDocument, objectiveButtonId.c_str());
                }
            }
        }

        if (auto* stats = _journalDocument->GetElementById("journal-character-stats")) {
            stats->SetInnerRML(buildStatRows(info.characterStats));
        }
        if (auto* skills = _journalDocument->GetElementById("journal-skill-stats")) {
            skills->SetInnerRML(buildStatRows(info.skills));
        }
        if (auto* general = _journalDocument->GetElementById("journal-general-stats")) {
            general->SetInnerRML(buildStatRows(info.generalStats));
        }
    }

    void DragonBoardRmlUi::SetSliderValue(const char* id, float value)
    {
        if (!id) return;
        const std::string_view sliderId(id);
        auto* document = FindPanelHandle(_activeDocument) != 0 ? _activeDocument :
            (sliderId.starts_with("edit") ? _itemEditDocument : _settingsDocument);
        if (!document) return;
        auto* element = document->GetElementById(id);
        if (!element) return;

        try {
            const auto currentText = element->GetAttribute<Rml::String>("value", "");
            if (!currentText.empty() &&
                std::abs(std::stof(currentText) - value) <= 0.00001f) {
                return;
            }
        } catch (...) {
            // Invalid or missing markup value: overwrite it with the runtime value.
        }

        _synchronizingSliderValues = true;
        element->SetAttribute("value", Rml::CreateString("%.6f", value));
        _synchronizingSliderValues = false;
        UpdateSliderValueLabel(id, value);
    }

    void DragonBoardRmlUi::SetItemEditInfo(const ItemEditInfo& info)
    {
        if (!_itemEditDocument) return;
        const auto setText = [this](const char* id, std::string value) {
            if (auto* element = _itemEditDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };
        setText("edit-item-name", info.itemName.empty() ? "Unnamed item" : info.itemName);
        setText("edit-category", info.category.empty() ? "Unknown" : info.category);
        setText("edit-form-id", Rml::CreateString("%08X", info.formID));
        setText("edit-model-path", info.modelPath.empty() ? "No model" : info.modelPath);
        SetSliderValue("editPosX", info.posX);
        SetSliderValue("editPosY", info.posY);
        SetSliderValue("editPosZ", info.posZ);
        SetSliderValue("editRotX", info.rotX);
        SetSliderValue("editRotY", info.rotY);
        SetSliderValue("editRotZ", info.rotZ);
        SetSliderValue("editScale", info.scale);

        if (auto* magic = _itemEditDocument->GetElementById("edit-magic-pin-actions")) {
            magic->SetProperty("display", info.magicItem ? "block" : "none");
        }
        if (auto* dashboard = _itemEditDocument->GetElementById("edit-pin-dashboard")) {
            dashboard->SetClass("disabled", info.boardPinnedToWorld);
        }
        setText("edit-pin-dashboard-state",
            info.boardPinnedToWorld ? "Unavailable while the board is world pinned" : "Attach this item to the dashboard");
        if (auto* world = _itemEditDocument->GetElementById("edit-pin-world")) {
            world->SetClass("disabled", !info.canPinToWorld);
        }
        setText("edit-pin-world-state",
            info.canPinToWorld ? "Preserve the current world pose" : "Requires a valid 3D preview pose");
        setText("edit-label-state", info.labelHidden ? "Hidden" : "Visible");
        if (auto* label = _itemEditDocument->GetElementById("edit-toggle-label")) {
            label->SetClass("enabled", !info.labelHidden);
        }
    }

    void DragonBoardRmlUi::SetDeveloperButtonEnabled(bool enabled)
    {
        if (!_settingsDocument) return;
        if (auto* toggle = _settingsDocument->GetElementById("toggle-dev-panel")) {
            toggle->SetClass("enabled", enabled);
        }
        if (auto* state = _settingsDocument->GetElementById("toggle-dev-state")) {
            state->SetInnerRML(enabled ? "Enabled" : "Disabled");
        }
    }

    void DragonBoardRmlUi::SetEditModeEnabled(bool enabled)
    {
        if (_settingsDocument) {
            if (auto* toggle = _settingsDocument->GetElementById("toggle-edit-mode")) {
                toggle->SetClass("enabled", enabled);
            }
            if (auto* state = _settingsDocument->GetElementById("toggle-edit-state")) {
                state->SetInnerRML(enabled ? "Enabled" : "Disabled");
            }
        }
        if (_magicDocument) {
            if (auto* edit = _magicDocument->GetElementById("magic-edit")) {
                edit->SetClass("enabled", enabled);
                edit->SetClass("disabled", !enabled);
            }
        }
    }

    void DragonBoardRmlUi::SetDeveloperCommands(
        std::vector<DeveloperCommand> commands,
        std::size_t selectedIndex)
    {
        if (!_developerDocument) return;
        _developerCommands = std::move(commands);
        _selectedDeveloperCommand = _developerCommands.empty() ?
            0 : std::min(selectedIndex, _developerCommands.size() - 1);

        auto* list = _developerDocument->GetElementById("dev-command-list");
        if (!list) return;
        std::string markup;
        for (std::size_t index = 0; index < _developerCommands.size(); ++index) {
            markup += "<button id=\"dev-command-" + std::to_string(index) +
                "\" class=\"command-item\">" + EscapeRml(_developerCommands[index].label) + "</button><br />";
        }
        if (markup.empty()) markup = "<div class=\"empty-state\">No commands configured.</div>";
        if (markup != _developerCommandListMarkup) {
            list->SetInnerRML(markup);
            _developerCommandListMarkup = std::move(markup);
            for (std::size_t index = 0; index < _developerCommands.size(); ++index) {
                const std::string id = "dev-command-" + std::to_string(index);
                BindClick(_developerDocument, id.c_str());
            }
        }
        SelectDeveloperCommand(_selectedDeveloperCommand);
    }

    void DragonBoardRmlUi::SetDeveloperInfo(const DeveloperInfo& info)
    {
        if (!_developerDocument) return;
        const auto setText = [this](const char* id, std::string value) {
            if (auto* element = _developerDocument->GetElementById(id)) {
                auto escaped = EscapeRml(value);
                if (element->GetInnerRML() != escaped) {
                    element->SetInnerRML(std::move(escaped));
                }
            }
        };
        const auto formatTiming = [](const DeveloperInfo::TimingStats& stats) {
            return Rml::CreateString(
                "%.2f / %.2f / %.2f / %.2f ms",
                stats.lastMs,
                stats.averageMs,
                stats.p95Ms,
                stats.p99Ms);
        };

        setText("dev-fps", Rml::CreateString("%.1f", info.fps));
        setText("dev-frame-time", Rml::CreateString("%.2f ms", info.frameTimeMs));
        setText("dev-present-timing", formatTiming(info.present));
        setText("dev-update-timing", formatTiming(info.update));
        setText("dev-begin-timing", formatTiming(info.beginFrame));
        setText("dev-render-timing", formatTiming(info.render));
        setText("dev-end-timing", formatTiming(info.endFrame));
        setText("dev-dx11-timing", formatTiming(info.dx11State));
        setText("dev-total-timing", formatTiming(info.total));
        setText("dev-draw-calls", std::to_string(info.panelDrawCalls));
        setText("dev-dom-elements", std::to_string(info.domElements));
        setText("dev-renders-per-second", Rml::CreateString("%.1f", info.rendersPerSecond));
        setText("dev-cached-frames", std::to_string(info.cachedFrames));
        setText("dev-dirty-reason", info.dirtyReason.empty() ? "<none>" : info.dirtyReason);
        setText("dev-active-document", info.activeDocument.empty() ? "<none>" : info.activeDocument);
        setText(
            "dev-texture-size",
            Rml::CreateString("%d x %d", info.renderWidth, info.renderHeight));
        setText("dev-version", info.pluginVersion);
        setText("dev-feature-level", Rml::CreateString("0x%X", info.d3dFeatureLevel));
        setText("dev-player-position", Rml::CreateString(
            "X %.1f   Y %.1f   Z %.1f", info.playerX, info.playerY, info.playerZ));
        setText("dev-cell", info.cellName.empty() ? "<none>" : info.cellName);
        setText("dev-cell-form", Rml::CreateString("%08X", info.cellFormId));
        setText("dev-worldspace", info.worldspaceName.empty() ? "<interior or none>" : info.worldspaceName);
        setText("dev-worldspace-form", info.worldspaceFormId == 0 ? "--------" :
            Rml::CreateString("%08X", info.worldspaceFormId));
        for (std::size_t city = 0; city < kMapCalibrationCities.size(); ++city) {
            setText(
                ("dev-cal-status-" + std::to_string(city)).c_str(),
                info.mapCalibrationStatus[city]);
        }
        setText("dev-calibration-summary", info.mapCalibrationSummary);
    }

    int DragonBoardRmlUi::GetLastDrawCallCount() const
    {
        return _renderer ? _renderer->GetDrawCallCount() : 0;
    }

    const DragonBoardRmlUi::RenderTiming& DragonBoardRmlUi::GetLastRenderTiming() const
    {
        return _lastRenderTiming;
    }

    void DragonBoardRmlUi::BindClick(Rml::ElementDocument* document, const char* id)
    {
        if (!document) return;
        if (auto* element = document->GetElementById(id)) {
            // Bind the actionable box itself. ProcessEvent still resolves a
            // nested text target back to its owning button or card.
            element->AddEventListener("click", _eventListener.get());
            RegisterInteractive(document, id, TriggerCaptureMode::kButton);
        } else {
            logger::warn("DragonBoardVR: RmlUi element '{}' is missing.", id);
        }
    }

    void DragonBoardRmlUi::BindSlider(Rml::ElementDocument* document, const char* id)
    {
        if (document && document->GetElementById(id)) {
            RegisterInteractive(document, id, TriggerCaptureMode::kSlider);
        } else {
            logger::warn("DragonBoardVR: RmlUi slider '{}' is missing.", id);
        }
    }

    void DragonBoardRmlUi::HandleClick(const char* id)
    {
        if (!id) return;
        RequestHaptic(ResolveClickHaptic(id));
        logger::info("DragonBoardVR: RmlUi click on '{}'.", id);
        if (const auto panel = FindPanelHandle(_activeDocument); panel != 0) {
            std::string value;
            if (const auto* element = _activeDocument->GetElementById(id)) {
                value = element->GetAttribute<Rml::String>("value", "");
                if (element->GetAttribute<Rml::String>(
                        "data-dragonboard-action", "") == "close") {
                    _closeRequested = true;
                }
            }
            _panelEvents.push_back(PanelEvent{
                panel, PanelEventType::kClick, id, std::move(value), 0.0f });
            return;
        }
        const std::string_view value(id);
        if (value == "close" || value == "dev-close") {
            _closeRequested = true;
        } else if (value == "edit-close" || value == "edit-back") {
            _itemEditAction = ItemEditAction::kBack;
        } else if (value == "edit-apply-item") {
            _itemEditAction = ItemEditAction::kApplyItem;
        } else if (value == "edit-apply-category") {
            _itemEditAction = ItemEditAction::kApplyCategory;
        } else if (value == "edit-reset") {
            _itemEditAction = ItemEditAction::kReset;
        } else if (value == "edit-pin-dashboard" || value == "edit-tab-pin") {
            _itemEditAction = ItemEditAction::kPinDashboard;
        } else if (value == "edit-pin-left") {
            _itemEditAction = ItemEditAction::kPinLeftHand;
        } else if (value == "edit-pin-world") {
            _itemEditAction = ItemEditAction::kPinWorld;
        } else if (value == "edit-toggle-label") {
            _itemEditAction = ItemEditAction::kToggleLabel;
        } else if (value == "mods-add") {
            _modsAction = ModsAction::kAdd;
        } else if (value == "mods-close") {
            _modsAction = ModsAction::kClose;
        } else if (value.starts_with("mods-card-")) {
            try {
                _modsActionIndex = static_cast<std::size_t>(std::stoul(std::string(value.substr(10))));
                _modsAction = ModsAction::kActivate;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid mods card id '{}'.", id);
            }
        } else if (value == "inventory-equip") {
            _inventoryAction = InventoryAction::kEquip;
        } else if (value == "inventory-drop") {
            _inventoryAction = InventoryAction::kDrop;
        } else if (value == "inventory-pin") {
            _inventoryAction = InventoryAction::kPin;
        } else if (value == "inventory-close") {
            _inventoryAction = InventoryAction::kClose;
        } else if (value == "inventory-search") {
            _inventoryAction = InventoryAction::kSearch;
        } else if (value == "inventory-search-clear") {
            _inventoryAction = InventoryAction::kClearSearch;
        } else if (value == "inventory-filter-weapons") {
            _inventoryAction = InventoryAction::kFilterWeapons;
        } else if (value == "inventory-filter-armor") {
            _inventoryAction = InventoryAction::kFilterArmor;
        } else if (value == "inventory-filter-consumables") {
            _inventoryAction = InventoryAction::kFilterConsumables;
        } else if (value == "inventory-filter-quest") {
            _inventoryAction = InventoryAction::kFilterQuest;
        } else if (value == "inventory-filter-books") {
            _inventoryAction = InventoryAction::kFilterBooks;
        } else if (value == "inventory-filter-misc") {
            _inventoryAction = InventoryAction::kFilterMisc;
        } else if (value.starts_with("inventory-item-")) {
            try {
                _inventoryActionIndex =
                    static_cast<std::size_t>(std::stoul(std::string(value.substr(15))));
                _inventoryAction = InventoryAction::kSelect;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid inventory item id '{}'.", id);
            }
        } else if (value == "magic-equip") {
            _magicAction = MagicAction::kEquip;
        } else if (value == "magic-edit") {
            _magicAction = MagicAction::kEdit;
        } else if (value == "magic-pin") {
            bool active = false;
            if (auto* menu = _magicDocument ?
                    _magicDocument->GetElementById("magic-pin-menu") : nullptr) {
                active = !menu->IsClassSet("active");
                menu->SetClass("active", active);
            }
            if (auto* pin = _magicDocument ?
                    _magicDocument->GetElementById("magic-pin") : nullptr) {
                pin->SetClass("active", active);
            }
        } else if (value == "magic-pin-dashboard") {
            _magicAction = MagicAction::kPinDashboard;
        } else if (value == "magic-pin-left") {
            _magicAction = MagicAction::kPinLeftHand;
        } else if (value == "magic-pin-world") {
            _magicAction = MagicAction::kPinWorld;
        } else if (value == "magic-pin-label") {
            _magicAction = MagicAction::kToggleLabel;
        } else if (value == "magic-close") {
            _magicAction = MagicAction::kClose;
        } else if (value == "magic-search") {
            _magicAction = MagicAction::kSearch;
        } else if (value == "magic-search-clear") {
            _magicAction = MagicAction::kClearSearch;
        } else if (value == "magic-filter-destruction") {
            _magicAction = MagicAction::kFilterDestruction;
        } else if (value == "magic-filter-conjuration") {
            _magicAction = MagicAction::kFilterConjuration;
        } else if (value == "magic-filter-restoration") {
            _magicAction = MagicAction::kFilterRestoration;
        } else if (value == "magic-filter-illusion") {
            _magicAction = MagicAction::kFilterIllusion;
        } else if (value == "magic-filter-alteration") {
            _magicAction = MagicAction::kFilterAlteration;
        } else if (value == "magic-filter-powers") {
            _magicAction = MagicAction::kFilterPowers;
        } else if (value == "magic-filter-passive") {
            _magicAction = MagicAction::kFilterPassive;
        } else if (value.starts_with("magic-spell-")) {
            try {
                _magicActionIndex =
                    static_cast<std::size_t>(std::stoul(std::string(value.substr(12))));
                _magicAction = MagicAction::kSelect;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid magic spell id '{}'.", id);
            }
        } else if (value == "journal-close") {
            _journalAction = JournalAction::kClose;
        } else if (value == "journal-settings") {
            _journalAction = JournalAction::kSettings;
        } else if (value == "journal-toggle-tracking") {
            _journalActionFormID = _journalSelectedFormID;
            _journalActionInstanceID = _journalSelectedInstanceID;
            _journalAction = JournalAction::kToggleTracking;
        } else if (value == "journal-tab-quests") {
            SelectJournalPage("quests");
        } else if (value == "journal-tab-stats") {
            SelectJournalPage("stats");
        } else if (value.starts_with("journal-quest-")) {
            if (ParseJournalQuestIdentity(
                    value.substr(14),
                    _journalActionFormID,
                    _journalActionInstanceID)) {
                _journalAction = JournalAction::kSelectQuest;
            } else {
                logger::warn("DragonBoardVR: invalid journal quest id '{}'.", id);
            }
        } else if (value.starts_with("journal-objective-")) {
            if (ParseJournalObjectiveIdentity(
                    value.substr(18),
                    _journalActionFormID,
                    _journalActionInstanceID,
                    _journalActionObjectiveInstanceID,
                    _journalActionObjectiveID)) {
                _journalAction = JournalAction::kTrackObjective;
            } else {
                logger::warn("DragonBoardVR: invalid journal objective id '{}'.", id);
            }
        } else if (value == "save") {
            _saveRequested = true;
        } else if (value == "toggle-edit-mode") {
            _editModeToggleRequested = true;
        } else if (value == "toggle-dev-panel") {
            _developerPanelToggleRequested = true;
        } else if (value.starts_with("tab-")) {
            SelectSettingsPage(id + 4);
        } else if (value.starts_with("dev-tab-")) {
            SelectDeveloperPage(id + 8);
        } else if (value.starts_with("dev-cal-city-")) {
            try {
                _selectedMapCalibrationCity = std::min<std::size_t>(
                    std::stoul(std::string(value.substr(13))),
                    kMapCalibrationCities.size() - 1);
                for (std::size_t city = 0; city < kMapCalibrationCities.size(); ++city) {
                    if (auto* button = _developerDocument->GetElementById(
                            "dev-cal-city-" + std::to_string(city))) {
                        button->SetClass("active", city == _selectedMapCalibrationCity);
                    }
                }
            } catch (...) {
                logger::warn("DragonBoardVR: invalid map calibration city id '{}'.", id);
            }
        } else if (value == "dev-calibration-surface") {
            _mapCalibrationRequest = MapCalibrationRequest{
                _selectedMapCalibrationCity,
                0.0f,
                0.0f
            };
        } else if (value == "dev-calibration-reset") {
            _mapCalibrationResetRequested = true;
        } else if (value.starts_with("edit-tab-")) {
            SelectItemEditPage(id + 9);
        } else if (value.starts_with("dev-command-")) {
            try {
                SelectDeveloperCommand(static_cast<std::size_t>(std::stoul(std::string(value.substr(12)))));
            } catch (...) {
                logger::warn("DragonBoardVR: invalid RmlUi developer command id '{}'.", id);
            }
        } else if (value == "dev-add-command") {
            _developerAddCommandRequested = true;
        } else if (value == "dev-execute" && _selectedDeveloperCommand < _developerCommands.size()) {
            _developerCommandRequested = _selectedDeveloperCommand;
        }
    }

    void DragonBoardRmlUi::HandleHover(const char* id)
    {
        if (!id || !*id || _hoveredElementId == id) return;
        _hoveredElementId = id;
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastHoverHaptic < std::chrono::milliseconds(45)) return;
        _lastHoverHaptic = now;
        RequestHaptic(HapticCue::kHover);
    }

    void DragonBoardRmlUi::HandleSliderChange(const char* id, float value)
    {
        if (_synchronizingSliderValues || !id || !*id) return;
        logger::info("DragonBoardVR: RmlUi slider '{}' changed to {:.3f}.", id, value);
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastSliderHaptic >= std::chrono::milliseconds(55)) {
            _lastSliderHaptic = now;
            RequestHaptic(HapticCue::kSliderTick);
        }
        UpdateSliderValueLabel(id, value);
        if (const auto panel = FindPanelHandle(_activeDocument); panel != 0) {
            _panelEvents.push_back(PanelEvent{
                panel,
                PanelEventType::kChange,
                id,
                Rml::CreateString("%.6f", value),
                value });
            return;
        }
        _sliderChange = SliderChange{ id, value };
    }

    void DragonBoardRmlUi::RequestHaptic(HapticCue cue)
    {
        if (static_cast<std::uint8_t>(cue) >
            static_cast<std::uint8_t>(_pendingHapticCue)) {
            _pendingHapticCue = cue;
        }
    }

    DragonBoardRmlUi::HapticCue DragonBoardRmlUi::ResolveClickHaptic(const char* id) const
    {
        if (!id || !_activeDocument) return HapticCue::kPress;
        if (const auto* element = _activeDocument->GetElementById(id)) {
            const auto type = element->GetAttribute<Rml::String>("data-haptic", "");
            if (type == "none") return HapticCue::kNone;
            if (type == "light") return HapticCue::kHover;
            if (type == "slider") return HapticCue::kSliderTick;
            if (type == "strong") return HapticCue::kStrong;
            if (type == "error") return HapticCue::kError;
        }
        return HapticCue::kPress;
    }

    void DragonBoardRmlUi::UpdateSliderValueLabel(const char* id, float value)
    {
        if (!id) return;
        const std::string_view sliderId(id);
        auto* document = sliderId.starts_with("edit") ? _itemEditDocument : _settingsDocument;
        if (!document) return;
        const std::string valueId = std::string("value-") + id;
        auto* label = document->GetElementById(valueId);
        if (!label) return;

        const bool degrees = sliderId.starts_with("menuRot") || sliderId.starts_with("editRot");
        label->SetInnerRML(Rml::CreateString(degrees ? "%.1f deg" : "%.2f", value));
    }

    void DragonBoardRmlUi::SelectSettingsPage(const char* selectedPage)
    {
        if (!_settingsDocument || !selectedPage) return;
        for (const auto* page : kPages) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("tab-") + page;
            const std::string pageId = std::string("page-") + page;
            if (auto* tab = _settingsDocument->GetElementById(tabId)) tab->SetClass("active", active);
            if (auto* content = _settingsDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
    }

    void DragonBoardRmlUi::SelectDeveloperPage(const char* selectedPage)
    {
        if (!_developerDocument || !selectedPage) return;
        for (const auto* page : kDeveloperPages) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("dev-tab-") + page;
            const std::string pageId = std::string("dev-page-") + page;
            if (auto* tab = _developerDocument->GetElementById(tabId)) tab->SetClass("active", active);
            if (auto* content = _developerDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
        if (auto* app = _developerDocument->GetElementById("app")) {
            app->SetClass("calibration-mode", std::string_view(selectedPage) == "calibration");
        }
    }

    void DragonBoardRmlUi::SelectItemEditPage(const char* selectedPage)
    {
        if (!_itemEditDocument || !selectedPage) return;
        for (const auto* page : kItemEditPages) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("edit-tab-") + page;
            const std::string pageId = std::string("edit-page-") + page;
            if (auto* tab = _itemEditDocument->GetElementById(tabId)) tab->SetClass("active", active);
            if (auto* content = _itemEditDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
    }

    void DragonBoardRmlUi::SelectJournalPage(const char* selectedPage)
    {
        if (!_journalDocument || !selectedPage) return;
        const bool questsSelected = std::string_view(selectedPage) == "quests";
        for (const auto* page : { "quests", "stats" }) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("journal-tab-") + page;
            const std::string pageId = std::string("journal-page-") + page;
            if (auto* tab = _journalDocument->GetElementById(tabId)) {
                tab->SetClass("active", active);
            }
            if (auto* content = _journalDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
        if (auto* tracking = _journalDocument->GetElementById("journal-toggle-tracking")) {
            tracking->SetProperty("display", questsSelected ? "flex" : "none");
        }
    }

    void DragonBoardRmlUi::SelectDeveloperCommand(std::size_t index)
    {
        if (!_developerDocument || _developerCommands.empty()) {
            _selectedDeveloperCommand = 0;
            UpdateDeveloperCommandDetails();
            return;
        }
        _selectedDeveloperCommand = std::min(index, _developerCommands.size() - 1);
        for (std::size_t commandIndex = 0; commandIndex < _developerCommands.size(); ++commandIndex) {
            const std::string id = "dev-command-" + std::to_string(commandIndex);
            if (auto* element = _developerDocument->GetElementById(id)) {
                element->SetClass("active", commandIndex == _selectedDeveloperCommand);
            }
        }
        UpdateDeveloperCommandDetails();
    }

    void DragonBoardRmlUi::UpdateDeveloperCommandDetails()
    {
        if (!_developerDocument) return;
        const auto setText = [this](const char* id, std::string value) {
            if (auto* element = _developerDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };
        const bool valid = _selectedDeveloperCommand < _developerCommands.size();
        if (!valid) {
            setText("dev-command-title", "No command selected");
            setText("dev-command-description", "Add commands to DragonBoardVR_DevCommands.ini.");
            setText("dev-command-input", "");
            if (auto* warning = _developerDocument->GetElementById("dev-command-warning")) {
                warning->SetProperty("display", "none");
            }
            if (auto* execute = _developerDocument->GetElementById("dev-execute")) {
                execute->SetAttribute("disabled", "");
                execute->SetAttribute("data-haptic", "error");
            }
            return;
        }

        const auto& command = _developerCommands[_selectedDeveloperCommand];
        setText("dev-command-title", command.label);
        setText("dev-command-description", command.description);
        setText("dev-command-input", command.command);
        if (auto* warning = _developerDocument->GetElementById("dev-command-warning")) {
            warning->SetProperty("display", command.dangerous ? "block" : "none");
        }
        if (auto* execute = _developerDocument->GetElementById("dev-execute")) {
            execute->RemoveAttribute("disabled");
            execute->SetAttribute("data-haptic", command.dangerous ? "strong" : "normal");
        }
    }

    void DragonBoardRmlUi::UpdateCursor(bool visible, int x, int y)
    {
        auto* cursor = _activeDocument ? _activeDocument->GetElementById("vr-cursor") : nullptr;
        if (!cursor) return;
        cursor->SetProperty("display", visible ? "block" : "none");
        if (visible) {
            cursor->SetProperty("left", std::to_string(x) + "px");
            cursor->SetProperty("top", std::to_string(y) + "px");
        }
    }
}
