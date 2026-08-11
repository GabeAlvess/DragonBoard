#include "ui/rml/DragonBoardRmlUi.h"
#include "ui/rml/DragonBoardRmlRenderer.h"
#include "ui/rml/LocalizationManager.h"
#include "vrui/VRUISettings.h"

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
                   id == "edit-pin-right" || id == "edit-toggle-label" ||
                   id.starts_with("mods-card-");
        }

        bool IsScrollbarElement(const Rml::Element* element)
        {
            for (auto* candidate = element; candidate;
                 candidate = candidate->GetParentNode()) {
                const std::string_view tag(candidate->GetTagName());
                if (tag == "sliderbar" || tag == "slidertrack" ||
                    tag == "sliderarrowdec" || tag == "sliderarrowinc" ||
                    tag == "scrollbarvertical" || tag == "scrollbarhorizontal" ||
                    tag == "scrollbarcorner") {
                    return true;
                }
                if (candidate->GetTagName() == "body") break;
            }
            return false;
        }

        bool IsInventoryScrollbarElement(const Rml::Element* element)
        {
            for (auto* candidate = element; candidate;
                 candidate = candidate->GetParentNode()) {
                if (candidate->GetId() == "inventory-scroll-proxy") return true;
                if (candidate->GetTagName() == "body") break;
            }
            return false;
        }

        bool IsMagicScrollbarElement(const Rml::Element* element)
        {
            for (auto* candidate = element; candidate;
                 candidate = candidate->GetParentNode()) {
                if (candidate->GetId() == "magic-scroll-proxy") return true;
                if (candidate->GetTagName() == "body") break;
            }
            return false;
        }

        constexpr const char* kContextName = "dragonboard_local_panels_rml";
        constexpr std::size_t kBuiltinDocumentCount = 10;

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

        constexpr std::array<const char*, 3> kGalleryDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/gallery.rml",
            "SKSE/Plugins/DragonBoardVR/ui/gallery.rml",
            "Assets/ui/rml/gallery.rml"
        };

        constexpr std::array<const char*, 3> kKeyboardDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/keyboard.rml",
            "SKSE/Plugins/DragonBoardVR/ui/keyboard.rml",
            "Assets/ui/rml/keyboard.rml"
        };

        constexpr std::array<const char*, 3> kWelcomeDocumentCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/welcome.rml",
            "SKSE/Plugins/DragonBoardVR/ui/welcome.rml",
            "Assets/ui/rml/welcome.rml"
        };

        struct KeyboardKeyDefinition
        {
            const char* id;
            char normal;
            char shifted;
        };

        constexpr std::array<KeyboardKeyDefinition, 47> kKeyboardKeys{ {
            { "keyboard-key-grave", '`', '~' },
            { "keyboard-key-1", '1', '!' },
            { "keyboard-key-2", '2', '@' },
            { "keyboard-key-3", '3', '#' },
            { "keyboard-key-4", '4', '$' },
            { "keyboard-key-5", '5', '%' },
            { "keyboard-key-6", '6', '^' },
            { "keyboard-key-7", '7', '&' },
            { "keyboard-key-8", '8', '*' },
            { "keyboard-key-9", '9', '(' },
            { "keyboard-key-0", '0', ')' },
            { "keyboard-key-minus", '-', '_' },
            { "keyboard-key-equals", '=', '+' },
            { "keyboard-key-q", 'q', 'Q' },
            { "keyboard-key-w", 'w', 'W' },
            { "keyboard-key-e", 'e', 'E' },
            { "keyboard-key-r", 'r', 'R' },
            { "keyboard-key-t", 't', 'T' },
            { "keyboard-key-y", 'y', 'Y' },
            { "keyboard-key-u", 'u', 'U' },
            { "keyboard-key-i", 'i', 'I' },
            { "keyboard-key-o", 'o', 'O' },
            { "keyboard-key-p", 'p', 'P' },
            { "keyboard-key-left-bracket", '[', '{' },
            { "keyboard-key-right-bracket", ']', '}' },
            { "keyboard-key-backslash", '\\', '|' },
            { "keyboard-key-a", 'a', 'A' },
            { "keyboard-key-s", 's', 'S' },
            { "keyboard-key-d", 'd', 'D' },
            { "keyboard-key-f", 'f', 'F' },
            { "keyboard-key-g", 'g', 'G' },
            { "keyboard-key-h", 'h', 'H' },
            { "keyboard-key-j", 'j', 'J' },
            { "keyboard-key-k", 'k', 'K' },
            { "keyboard-key-l", 'l', 'L' },
            { "keyboard-key-semicolon", ';', ':' },
            { "keyboard-key-apostrophe", '\'', '"' },
            { "keyboard-key-z", 'z', 'Z' },
            { "keyboard-key-x", 'x', 'X' },
            { "keyboard-key-c", 'c', 'C' },
            { "keyboard-key-v", 'v', 'V' },
            { "keyboard-key-b", 'b', 'B' },
            { "keyboard-key-n", 'n', 'N' },
            { "keyboard-key-m", 'm', 'M' },
            { "keyboard-key-comma", ',', '<' },
            { "keyboard-key-period", '.', '>' },
            { "keyboard-key-slash", '/', '?' }
        } };

        constexpr std::array<const char*, 6> kKeyboardControlIds{
            "keyboard-backspace",
            "keyboard-shift",
            "keyboard-clear",
            "keyboard-space",
            "keyboard-cancel",
            "keyboard-confirm"
        };

        constexpr std::array<const char*, 3> kFontCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/assets/Fonts/DragonBoardVR_Font.ttf",
            "SKSE/Plugins/DragonBoardVR/ui/assets/Fonts/DragonBoardVR_Font.ttf",
            "Assets/ui/rml/assets/Fonts/DragonBoardVR_Font.ttf"
        };

        constexpr std::array<const char*, 3> kFallbackFontCandidates{
            "Data/SKSE/Plugins/DragonBoardVR/ui/assets/Fonts/NotoSansCJKsc-Regular.otf",
            "SKSE/Plugins/DragonBoardVR/ui/assets/Fonts/NotoSansCJKsc-Regular.otf",
            "Assets/ui/rml/assets/Fonts/NotoSansCJKsc-Regular.otf"
        };

        constexpr std::array<const char*, 5> kPages{
            "general", "visuals", "items", "widgets", "tutorials"
        };

        constexpr std::array<const char*, 6> kTutorialPages{
            "general-use", "pin-items", "inventory-magic",
            "mods", "journal", "gallery"
        };

        constexpr std::array<const char*, 6> kTutorialPageTitles{
            "General use", "Pin items and widgets", "Inventory and Magic Panel",
            "Mods Panel", "Journal", "Gallery"
        };

        constexpr std::array<const char*, 6> kSliders{
            "reticleScale", "itemWeaponScale", "itemArmorScale",
            "itemPotionScale", "itemFoodScale", "itemMiscScale"
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
                logger::trace("DragonBoardVR RmlUi: {}", message);
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

        _localization = std::make_unique<LocalizationManager>();
        const auto& requestedLanguage = vrui::VRUISettings::get().uiLanguage;
        _localization->Load(requestedLanguage);
        _settingsLanguageCode = _localization->ActiveCode();
        logger::info(
            "DragonBoardVR: RmlUi localization initialized (requested='{}', active='{}').",
            requestedLanguage,
            _settingsLanguageCode);

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
                logger::trace("DragonBoardVR: RmlUi font '{}' registered as DragonBoard.", path);
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

        for (const auto* path : kFallbackFontCandidates) {
            if (!std::filesystem::exists(path)) continue;
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) continue;
            const auto size = stream.tellg();
            if (size <= 0) continue;
            _fallbackFontData.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            if (!stream.read(
                    reinterpret_cast<char*>(_fallbackFontData.data()), size)) {
                _fallbackFontData.clear();
                continue;
            }
            const Rml::Span<const Rml::byte> fontBytes(
                _fallbackFontData.data(), _fallbackFontData.size());
            if (Rml::LoadFontFace(
                    fontBytes,
                    "DragonBoardCJK",
                    Rml::Style::FontStyle::Normal,
                    Rml::Style::FontWeight::Normal,
                    true)) {
                logger::trace(
                    "DragonBoardVR: RmlUi fallback font '{}' registered for CJK glyphs.",
                    path);
                break;
            }
            _fallbackFontData.clear();
        }

        _eventListener = std::make_unique<UiEventListener>(*this);
        _builtinDocumentLoadStep = 0;
        return true;
    }

    bool DragonBoardRmlUi::ReloadLanguage(std::string_view code)
    {
        if (!_context || !_localization) return false;

        HideAllDocuments();
        const std::array<Rml::ElementDocument**, 10> documents{ {
            &_settingsDocument,
            &_developerDocument,
            &_itemEditDocument,
            &_modsDocument,
            &_inventoryDocument,
            &_magicDocument,
            &_journalDocument,
            &_galleryDocument,
            &_welcomeDocument,
            &_keyboardDocument,
        } };
        for (auto** document : documents) {
            if (!*document) continue;
            const auto* unloading = *document;
            std::erase_if(_interactiveBindings, [unloading](const auto& binding) {
                return binding.document == unloading;
            });
            _context->UnloadDocument(*document);
            *document = nullptr;
        }

        _activeDocument = nullptr;
        _keyboardReturnDocument = nullptr;
        _modsListMarkup.clear();
        _developerCommandListMarkup.clear();
        _journalQuestListMarkup.clear();
        _journalActiveQuestOrder.clear();
        ResetInventoryVirtualRows();
        ResetMagicVirtualRows();
        _builtinDocumentLoadStep = 0;
        _localization->Load(code);
        _settingsLanguageCode = _localization->ActiveCode();
        logger::info(
            "DragonBoardVR: rebuilding RmlUi documents for interface language '{}'.",
            _settingsLanguageCode);
        return true;
    }

    bool DragonBoardRmlUi::LoadNextBuiltinDocument()
    {
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
                    std::ifstream stream(path, std::ios::binary);
                    std::string source{
                        std::istreambuf_iterator<char>(stream),
                        std::istreambuf_iterator<char>() };
                    if (_localization) {
                        source = _localization->TranslateMarkup(source);
                    }
                    destination = _context->LoadDocumentFromMemory(source, path);
                    if (destination && _localization &&
                        _localization->ActiveCode() == "ru") {
                        auto* languageRoot = destination->GetElementById("app");
                        if (!languageRoot) {
                            languageRoot = destination->GetElementById("welcome-app");
                        }
                        if (!languageRoot) {
                            languageRoot = destination->GetElementById("keyboard-app");
                        }
                        if (!languageRoot) languageRoot = destination;
                        languageRoot->SetClass("lang-ru", true);
                    }
                    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
                    logger::trace(
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
                for (const auto* slider : kSliders) {
                    BindSlider(_settingsDocument, slider);
                    if (std::string_view(slider).starts_with("item")) {
                        BindClick(_settingsDocument, (std::string(slider) + "-decrease").c_str());
                        BindClick(_settingsDocument, (std::string(slider) + "-increase").c_str());
                    }
                }
                BindClick(_settingsDocument, "save");
                BindClick(_settingsDocument, "position-adjustment");
                BindClick(_settingsDocument, "toggle-lock-pins");
                BindClick(_settingsDocument, "toggle-dev-panel");
                BindClick(_settingsDocument, "toggle-show-tutorials");
                BindClick(_settingsDocument, "toggle-status-widget");
                BindClick(_settingsDocument, "language-previous");
                BindClick(_settingsDocument, "language-next");
                BindClick(_settingsDocument, "toggle-world-pin");
                BindClick(_settingsDocument, "restart-dragonboard");
                for (const auto* tutorial : kTutorialPages) {
                    BindClick(_settingsDocument, (std::string("tutorial-card-") + tutorial).c_str());
                    BindClick(_settingsDocument, (std::string("tutorial-back-") + tutorial).c_str());
                }
                SelectSettingsPage("general");
                _settingsDocument->Hide();
                logger::trace("DragonBoardVR: external RmlUi settings loaded from '{}'.", path);
            }
            break;
        }
        case 1: {
            const auto* path = loadDocument(
                kDeveloperDocumentCandidates, _developerDocument, "Developer");
            loadedDocument = _developerDocument;
            if (_developerDocument) {
                for (const auto* page : { "commands", "info" }) {
                    const std::string tabId = std::string("dev-tab-") + page;
                    BindClick(_developerDocument, tabId.c_str());
                }
                BindClick(_developerDocument, "dev-add-command");
                BindClick(_developerDocument, "dev-execute");
                BindClick(_developerDocument, "dev-calibration-surface");
                BindClick(_developerDocument, "dev-calibration-reset");
                for (std::size_t city = 0; city < kMapCalibrationCities.size(); ++city) {
                    const auto id = "dev-cal-city-" + std::to_string(city);
                    BindClick(_developerDocument, id.c_str());
                }
                SelectDeveloperPage("commands");
                _developerDocument->Hide();
                logger::trace("DragonBoardVR: external RmlUi developer panel loaded from '{}'.", path);
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
                         "edit-pin-dashboard", "edit-pin-left", "edit-pin-right", "edit-toggle-label" }) {
                    BindClick(_itemEditDocument, id);
                }
                SelectItemEditPage("position");
                _itemEditDocument->Hide();
                logger::trace("DragonBoardVR: external RmlUi item editor loaded from '{}'.", path);
            }
            break;
        }
        case 3: {
            const auto* path = loadDocument(kModsDocumentCandidates, _modsDocument, "Mods");
            loadedDocument = _modsDocument;
            if (_modsDocument) {
                BindClick(_modsDocument, "mods-add");
                BindClick(_modsDocument, "mods-tab-actions");
                BindClick(_modsDocument, "mods-tab-ini");
                BindClick(_modsDocument, "mods-ini-refresh");
                BindClick(_modsDocument, "mods-ini-save");
                BindClick(_modsDocument, "mods-ini-discard");
                BindClick(_modsDocument, "mods-ini-search");
                BindClick(_modsDocument, "mods-ini-search-clear");
                BindClick(_modsDocument, "mods-ini-show-hidden");
                SelectModsPage(false);
                _modsDocument->Hide();
                logger::trace("DragonBoardVR: external RmlUi mods panel loaded from '{}'.", path);
            }
            break;
        }
        case 4: {
            const auto* path = loadDocument(
                kInventoryDocumentCandidates, _inventoryDocument, "Inventory");
            loadedDocument = _inventoryDocument;
            if (_inventoryDocument) {
                for (const auto* id : {
                         "inventory-equip", "inventory-drop", "inventory-pin",
                         "inventory-search", "inventory-search-clear",
                         "inventory-filter-favorites",
                         "inventory-filter-weapons", "inventory-filter-armor",
                         "inventory-filter-potions", "inventory-filter-food",
                         "inventory-filter-quest",
                         "inventory-filter-books", "inventory-filter-misc" }) {
                    BindClick(_inventoryDocument, id);
                }
                _inventoryDocument->Hide();
                logger::trace("DragonBoardVR: external RmlUi inventory panel loaded from '{}'.", path);
            }
            break;
        }
        case 5: {
            const auto* path = loadDocument(kMagicDocumentCandidates, _magicDocument, "Magic");
            loadedDocument = _magicDocument;
            if (_magicDocument) {
                for (const auto* id : {
                         "magic-equip", "magic-pin",
                         "magic-search", "magic-search-clear",
                         "magic-filter-favorites",
                         "magic-filter-destruction", "magic-filter-conjuration",
                         "magic-filter-restoration", "magic-filter-illusion",
                         "magic-filter-alteration", "magic-filter-powers",
                         "magic-filter-passive", "magic-pin-dashboard",
                         "magic-pin-left", "magic-pin-right" }) {
                    BindClick(_magicDocument, id);
                }
                _magicDocument->Hide();
                logger::trace("DragonBoardVR: external RmlUi magic panel loaded from '{}'.", path);
            }
            break;
        }
        case 6: {
            const auto* path = loadDocument(kJournalDocumentCandidates, _journalDocument, "Journal");
            loadedDocument = _journalDocument;
            if (_journalDocument) {
                for (const auto* id : {
                         "journal-tab-quests", "journal-tab-stats",
                         "journal-toggle-tracking", "journal-filter-all",
                         "journal-filter-main", "journal-filter-side",
                         "journal-filter-misc" }) {
                    BindClick(_journalDocument, id);
                }
                SelectJournalPage("quests");
                _journalDocument->Hide();
                logger::trace("DragonBoardVR: external RmlUi journal loaded from '{}'.", path);
            }
            break;
        }
        case 7: {
            const auto* path = loadDocument(
                kGalleryDocumentCandidates, _galleryDocument, "Gallery");
            loadedDocument = _galleryDocument;
            if (_galleryDocument) {
                for (const auto* id : {
                         "gallery-capture", "gallery-timer", "gallery-favorite",
                         "gallery-delete", "gallery-pin-map", "gallery-pin-panel" }) {
                    BindClick(_galleryDocument, id);
                }
                _galleryDocument->Hide();
                logger::trace("DragonBoardVR: Gallery panel loaded from '{}'.", path);
            }
            break;
        }
        case 8: {
            const auto* path = loadDocument(
                kWelcomeDocumentCandidates, _welcomeDocument, "Welcome");
            loadedDocument = _welcomeDocument;
            if (_welcomeDocument) {
                BindClick(_welcomeDocument, "welcome-close");
                BindClick(_welcomeDocument, "welcome-next-1");
                BindClick(_welcomeDocument, "welcome-next-2");
                BindClick(_welcomeDocument, "welcome-next-3");
                SetWelcomePage(1, false);
                _welcomeDocument->Hide();
                logger::trace(
                    "DragonBoardVR: Welcome tutorial loaded from '{}'.", path);
            }
            break;
        }
        case 9: {
            const auto* path = loadDocument(
                kKeyboardDocumentCandidates, _keyboardDocument, "Keyboard");
            loadedDocument = _keyboardDocument;
            if (_keyboardDocument) {
                for (const auto& key : kKeyboardKeys) {
                    BindClick(_keyboardDocument, key.id);
                }
                for (const auto* id : kKeyboardControlIds) {
                    BindClick(_keyboardDocument, id);
                }
                _keyboardDocument->Hide();
                logger::trace(
                    "DragonBoardVR: shared RmlUi keyboard loaded from '{}'.", path);
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
        return _builtinDocumentLoadStep >= kBuiltinDocumentCount;
    }

    void DragonBoardRmlUi::Shutdown()
    {
        _triggerScrollLockDocument = nullptr;
        _triggerScrollTarget = nullptr;
        _triggerScrollLockActive = false;
        _triggerScrollReleasePending = false;
        _triggerCaptureMode = TriggerCaptureMode::kNone;
        _triggerCapturedSliderId.clear();
        _sliderPointerInitialized = false;
        _triggerCapturedActionId.clear();
        _triggerCaptureProgrammatic = false;
        _pointerSmoothingInitialized = false;
        _interactiveBindings.clear();
        ClearGripScrollHoverLock();
        _gripScrollActive = false;
        _gripScrollTarget = nullptr;
        _gripScrollTargetTop = 0.0f;
        _gripPointerScrollAccumulator = 0.0f;
        _gripScrollMovedLogged = false;
        _settingsDocument = nullptr;
        _developerDocument = nullptr;
        _itemEditDocument = nullptr;
        _modsDocument = nullptr;
        _inventoryDocument = nullptr;
        _magicDocument = nullptr;
        _journalDocument = nullptr;
        _welcomeDocument = nullptr;
        _keyboardDocument = nullptr;
        _activeDocument = nullptr;
        _keyboardReturnDocument = nullptr;
        _keyboardText.clear();
        _keyboardPrompt.clear();
        _keyboardResult.reset();
        _keyboardShift = false;
        _keyboardMaximumLength = 0;
        _modsListMarkup.clear();
        _developerCommandListMarkup.clear();
        ResetInventoryVirtualRows();
        _inventoryVirtualItems.clear();
        _inventoryVirtualContextKey.clear();
        ResetMagicVirtualRows();
        _magicVirtualItems.clear();
        _magicVirtualContextKey.clear();
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
        _fallbackFontData.clear();
        _localization.reset();
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

    bool DragonBoardRmlUi::IsGalleryReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _galleryDocument;
    }

    bool DragonBoardRmlUi::IsWelcomeReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _welcomeDocument;
    }

    bool DragonBoardRmlUi::IsKeyboardReady() const
    {
        return _renderer && _renderer->IsReady() && _context && _keyboardDocument;
    }

    bool DragonBoardRmlUi::IsKeyboardOpen() const
    {
        return IsKeyboardReady() && _activeDocument == _keyboardDocument;
    }

    bool DragonBoardRmlUi::ShowSettings()
    {
        if (!IsSettingsReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _settingsDocument) return true;
        HideAllDocuments();
        _settingsDocument->Show();
        _activeDocument = _settingsDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowDeveloper()
    {
        if (!IsDeveloperReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _developerDocument) return true;
        HideAllDocuments();
        _developerDocument->Show();
        _activeDocument = _developerDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowItemEdit()
    {
        if (!IsItemEditReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _itemEditDocument) return true;
        HideAllDocuments();
        _itemEditDocument->Show();
        _activeDocument = _itemEditDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowMods()
    {
        if (!IsModsReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _modsDocument) return true;
        HideAllDocuments();
        _modsDocument->Show();
        _activeDocument = _modsDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowInventory()
    {
        if (!IsInventoryReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _inventoryDocument) return true;
        HideAllDocuments();
        _inventoryDocument->Show();
        _activeDocument = _inventoryDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowMagic()
    {
        if (!IsMagicReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _magicDocument) return true;
        HideAllDocuments();
        _magicDocument->Show();
        _activeDocument = _magicDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowJournal()
    {
        if (!IsJournalReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _journalDocument) return true;
        HideAllDocuments();
        _journalDocument->Show();
        _activeDocument = _journalDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowGallery()
    {
        if (!IsGalleryReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _galleryDocument) return true;
        HideAllDocuments();
        _galleryDocument->Show();
        _activeDocument = _galleryDocument;
        return true;
    }

    bool DragonBoardRmlUi::ShowWelcome()
    {
        if (!IsWelcomeReady()) return false;
        if (IsKeyboardOpen()) return true;
        if (_activeDocument == _welcomeDocument) return true;
        HideAllDocuments();
        _welcomeDocument->Show();
        _activeDocument = _welcomeDocument;
        return true;
    }

    bool DragonBoardRmlUi::OpenKeyboard(
        std::string prompt,
        std::string initialText,
        std::size_t maximumLength)
    {
        if (!IsKeyboardReady() || IsKeyboardOpen()) return false;
        _keyboardReturnDocument = _activeDocument;
        _keyboardPrompt = Tr(prompt);
        _keyboardMaximumLength = std::max<std::size_t>(1, maximumLength);
        if (initialText.size() > _keyboardMaximumLength) {
            initialText.resize(_keyboardMaximumLength);
        }
        _keyboardText = std::move(initialText);
        _keyboardShift = false;
        _keyboardResult.reset();
        UpdateKeyboardVisuals();
        HideAllDocuments();
        _keyboardDocument->Show();
        _activeDocument = _keyboardDocument;
        logger::info(
            "DragonBoardVR: shared RmlUi keyboard opened (limit={}).",
            _keyboardMaximumLength);
        return true;
    }

    std::optional<DragonBoardRmlUi::KeyboardResult>
    DragonBoardRmlUi::ConsumeKeyboardResult()
    {
        auto result = std::move(_keyboardResult);
        _keyboardResult.reset();
        return result;
    }

    void DragonBoardRmlUi::CancelKeyboard()
    {
        CloseKeyboard(false);
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
        if (IsKeyboardOpen()) return true;
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
        bool fingerTouchActive,
        bool fingerTouchScrolling,
        float stickX,
        float stickY,
        int width,
        int height,
        float deltaTime)
    {
        if (!IsReady()) return;

        _currentTriggerDown = triggerDown;
        _currentGripDown = gripDown;
        const bool scrollArmed = gripDown && !triggerDown;
        if (!scrollArmed && _gripScrollActive) {
            ClearGripScrollHoverLock();
            _gripScrollActive = false;
            _gripScrollDirectVirtualScrollbar = false;
        }

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
        if (scrollArmed && (pointerOnPanel || _gripScrollActive)) {
            if (!_gripScrollActive) {
                _gripScrollActive = true;
                _gripScrollHorizontal = false;
                _gripScrollPointerX = pointerX;
                _gripScrollPointerY = pointerY;
                _gripPointerScrollAccumulator = 0.0f;
                _gripScrollMovedLogged = false;
                auto* hovered = _context->GetHoverElement();
                _gripScrollTarget = nullptr;
                _gripScrollDirectVirtualScrollbar = false;
                if (_activeDocument == _modsDocument && _modsIniEditorSelected) {
                    for (auto* candidate = hovered;
                         candidate && candidate != _activeDocument;
                         candidate = candidate->GetParentNode()) {
                        const std::string_view id(candidate->GetId());
                        if (id == "mods-ini-list" || id == "mods-ini-detail" ||
                            id == "mods-ini-file-tabs") {
                            _gripScrollTarget = candidate;
                            _gripScrollHorizontal = id == "mods-ini-file-tabs";
                            break;
                        }
                    }
                    if (!_gripScrollTarget) {
                        if (auto* tabs =
                                _activeDocument->GetElementById("mods-ini-file-tabs")) {
                            const auto offset =
                                tabs->GetAbsoluteOffset(Rml::BoxArea::Border);
                            const float right =
                                offset.x + static_cast<float>(tabs->GetClientWidth());
                            const float bottom =
                                offset.y + static_cast<float>(tabs->GetClientHeight());
                            if (static_cast<float>(pointerX) >= offset.x &&
                                static_cast<float>(pointerX) <= right &&
                                static_cast<float>(pointerY) >= offset.y &&
                                static_cast<float>(pointerY) <= bottom) {
                                _gripScrollTarget = tabs;
                                _gripScrollHorizontal = true;
                            }
                        }
                    }
                    if (!_gripScrollTarget) {
                        const char* fallbackId =
                            pointerX < 468 ? "mods-ini-list" : "mods-ini-detail";
                        _gripScrollTarget =
                            _activeDocument->GetElementById(fallbackId);
                    }
                }
                if (!_gripScrollTarget && hovered &&
                    _activeDocument == _galleryDocument) {
                    auto* browser =
                        _galleryDocument->GetElementById("gallery-browser");
                    auto* grid =
                        _galleryDocument->GetElementById("gallery-grid");
                    for (auto* element = hovered;
                         element && element != _galleryDocument;
                         element = element->GetParentNode()) {
                        if (element == browser || element == grid) {
                            _gripScrollTarget = browser;
                            _gripScrollHorizontal = true;
                            break;
                        }
                    }
                }
                if (!_gripScrollTarget && hovered) {
                    if (_activeDocument == _inventoryDocument) {
                        auto* list =
                            _inventoryDocument->GetElementById("inventory-item-list");
                        auto* proxy =
                            _inventoryDocument->GetElementById("inventory-scroll-proxy");
                        for (auto* element = hovered;
                             element && element != _inventoryDocument;
                             element = element->GetParentNode()) {
                            if (element == list || element == proxy) {
                                _gripScrollTarget = proxy;
                                break;
                            }
                        }
                    }
                }
                if (!_gripScrollTarget && hovered) {
                    if (_activeDocument == _magicDocument) {
                        auto* list =
                            _magicDocument->GetElementById("magic-spell-list");
                        auto* proxy =
                            _magicDocument->GetElementById("magic-scroll-proxy");
                        for (auto* element = hovered;
                             element && element != _magicDocument;
                             element = element->GetParentNode()) {
                            if (element == list || element == proxy) {
                                _gripScrollTarget = proxy;
                                break;
                            }
                        }
                    }
                }
                if (!_gripScrollTarget && hovered) {
                    _gripScrollTarget = hovered->GetClosestScrollableContainer();
                }
                if (!_gripScrollTarget && _activeDocument) {
                    _gripScrollTarget = _activeDocument->GetElementById("page-scroll");
                }
                const bool inventoryLogicalScroll =
                    _activeDocument == _inventoryDocument &&
                    _gripScrollTarget &&
                    _gripScrollTarget->GetId() == "inventory-scroll-proxy";
                const bool magicLogicalScroll =
                    _activeDocument == _magicDocument &&
                    _gripScrollTarget &&
                    _gripScrollTarget->GetId() == "magic-scroll-proxy";
                const bool virtualLogicalScroll =
                    inventoryLogicalScroll || magicLogicalScroll;
                _gripScrollDirectVirtualScrollbar =
                    fingerTouchActive && fingerTouchScrolling &&
                    ((inventoryLogicalScroll &&
                      IsInventoryScrollbarElement(hovered)) ||
                     (magicLogicalScroll &&
                      IsMagicScrollbarElement(hovered)));
                _gripScrollTargetTop = inventoryLogicalScroll ?
                    _inventorySyncedScrollTop :
                    (magicLogicalScroll ? _magicSyncedScrollTop :
                    (_gripScrollTarget ?
                        (_gripScrollHorizontal ?
                            _gripScrollTarget->GetScrollLeft() :
                            _gripScrollTarget->GetScrollTop()) :
                        0.0f));
                if (_gripScrollDirectVirtualScrollbar) {
                    UpdateVirtualScrollbarPosition(
                        pointerY, inventoryLogicalScroll, magicLogicalScroll);
                    _gripScrollTargetTop = inventoryLogicalScroll ?
                        _inventorySyncedScrollTop : _magicSyncedScrollTop;
                }
                if (_gripScrollTarget) {
                    const float maximum = virtualLogicalScroll ?
                        std::max(
                            0.0f,
                            static_cast<float>(
                                inventoryLogicalScroll ?
                                    _inventoryVirtualItems.size() :
                                    _magicVirtualItems.size()) *
                                    120.0f -
                                600.0f) :
                        (_gripScrollHorizontal ?
                            std::max(
                                0.0f,
                                _gripScrollTarget->GetScrollWidth() -
                                    _gripScrollTarget->GetClientWidth()) :
                            std::max(
                                0.0f,
                                _gripScrollTarget->GetScrollHeight() -
                                    _gripScrollTarget->GetClientHeight()));
                    logger::trace(
                        "DragonBoardVR: grip scroll captured '{}' "
                        "(tag={}, top={:.1f}, max={:.1f}, pointerOnPanel={}).",
                        _gripScrollTarget->GetId(),
                        _gripScrollTarget->GetTagName(),
                        _gripScrollTargetTop,
                        maximum,
                        pointerOnPanel);
                } else {
                    logger::trace(
                        "DragonBoardVR: grip scroll started without a scroll target.");
                }
                CaptureGripScrollHoverLock();
            } else {
                const float pointerDeltaX =
                    static_cast<float>(pointerX - _gripScrollPointerX);
                const float pointerDelta = static_cast<float>(pointerY - _gripScrollPointerY);
                _gripScrollPointerX = pointerX;
                _gripScrollPointerY = pointerY;
                const bool inventoryLogicalScroll =
                    _activeDocument == _inventoryDocument &&
                    _gripScrollTarget &&
                    _gripScrollTarget->GetId() == "inventory-scroll-proxy";
                const bool magicLogicalScroll =
                    _activeDocument == _magicDocument &&
                    _gripScrollTarget &&
                    _gripScrollTarget->GetId() == "magic-scroll-proxy";
                if (_gripScrollDirectVirtualScrollbar) {
                    UpdateVirtualScrollbarPosition(
                        pointerY, inventoryLogicalScroll, magicLogicalScroll);
                    _gripScrollTargetTop = inventoryLogicalScroll ?
                        _inventorySyncedScrollTop : _magicSyncedScrollTop;
                } else if (inventoryLogicalScroll || magicLogicalScroll) {
                    constexpr float pointerSensitivity = 1.0f;
                    _gripScrollTargetTop -= pointerDelta * pointerSensitivity;
                } else {
                    constexpr float pointerFilter = 0.32f;
                    const float axisDelta =
                        _gripScrollHorizontal ? pointerDeltaX : pointerDelta;
                    _gripPointerScrollAccumulator +=
                        (axisDelta - _gripPointerScrollAccumulator) * pointerFilter;
                    constexpr float pointerSensitivity = 0.78f;
                    if (_gripScrollHorizontal) {
                        _gripScrollTargetTop -=
                            _gripPointerScrollAccumulator * pointerSensitivity;
                    } else {
                        _gripScrollTargetTop -=
                            _gripPointerScrollAccumulator * pointerSensitivity;
                    }
                }
            }

            if (_gripScrollTarget) {
                const bool inventoryLogicalScroll =
                    _activeDocument == _inventoryDocument &&
                    _gripScrollTarget->GetId() == "inventory-scroll-proxy";
                const bool magicLogicalScroll =
                    _activeDocument == _magicDocument &&
                    _gripScrollTarget->GetId() == "magic-scroll-proxy";
                const bool virtualLogicalScroll =
                    inventoryLogicalScroll || magicLogicalScroll;
                constexpr float stickDeadzone = 0.15f;
                const float stickPixelsPerFrame =
                    virtualLogicalScroll ? 15.0f : 9.0f;
                const float stickAxis = _gripScrollHorizontal ? -stickX : stickY;
                if (std::abs(stickAxis) > stickDeadzone) {
                    _gripScrollTargetTop += stickAxis * stickPixelsPerFrame;
                }
                const float maximum = virtualLogicalScroll ?
                    std::max(
                        0.0f,
                        static_cast<float>(
                            inventoryLogicalScroll ?
                                _inventoryVirtualItems.size() :
                                _magicVirtualItems.size()) *
                                120.0f -
                                600.0f) :
                    (_gripScrollHorizontal ?
                        std::max(
                            0.0f,
                            _gripScrollTarget->GetScrollWidth() -
                                _gripScrollTarget->GetClientWidth()) :
                        std::max(
                            0.0f,
                            _gripScrollTarget->GetScrollHeight() -
                                _gripScrollTarget->GetClientHeight()));
                _gripScrollTargetTop = std::clamp(_gripScrollTargetTop, 0.0f, maximum);

                const float current = inventoryLogicalScroll ?
                    _inventorySyncedScrollTop :
                    (magicLogicalScroll ?
                        _magicSyncedScrollTop :
                        (_gripScrollHorizontal ?
                            _gripScrollTarget->GetScrollLeft() :
                            _gripScrollTarget->GetScrollTop()));
                const float difference = _gripScrollTargetTop - current;
                if (std::abs(difference) >= 0.5f) {
                    float step = difference;
                    if (!virtualLogicalScroll) {
                        constexpr float easing = 0.24f;
                        step *= easing;
                        if (std::abs(step) < 1.0f) {
                            step = std::copysign(1.0f, step);
                        }
                    }
                    const float next = std::clamp(current + step, 0.0f, maximum);
                    if (inventoryLogicalScroll) {
                        _inventorySyncedScrollTop = next;
                    } else if (magicLogicalScroll) {
                        _magicSyncedScrollTop = next;
                    } else {
                        if (_gripScrollHorizontal) {
                            _gripScrollTarget->SetScrollLeft(next);
                        } else {
                            _gripScrollTarget->SetScrollTop(next);
                        }
                    }
                    if (!_gripScrollMovedLogged) {
                        logger::trace(
                            "DragonBoardVR: grip scroll moved '{}' {:.1f} -> {:.1f}.",
                            _gripScrollTarget->GetId(),
                            current,
                            inventoryLogicalScroll ?
                                _inventorySyncedScrollTop :
                                (magicLogicalScroll ?
                                    _magicSyncedScrollTop :
                                    (_gripScrollHorizontal ?
                                        _gripScrollTarget->GetScrollLeft() :
                                        _gripScrollTarget->GetScrollTop())));
                        _gripScrollMovedLogged = true;
                    }
                }
            }
        } else {
            if (_gripScrollActive) ClearGripScrollHoverLock();
            _gripScrollActive = false;
            _gripScrollDirectVirtualScrollbar = false;
            _gripScrollTarget = nullptr;
            _gripScrollHorizontal = false;
            _gripScrollPointerX = 0;
            _gripScrollTargetTop = 0.0f;
            _gripPointerScrollAccumulator = 0.0f;
            _gripScrollMovedLogged = false;
        }
        if (triggerDown) {
            UpdateVirtualScrollbarPosition(
                pointerY,
                _inventoryScrollbarDragging,
                _magicScrollbarDragging);
        }
        if (_activeDocument == _inventoryDocument && _inventoryVirtualInitialized) {
            UpdateInventoryVirtualRows();
        } else if (_activeDocument == _magicDocument && _magicVirtualInitialized) {
            UpdateMagicVirtualRows();
        }
        ApplyGripScrollHoverLock();

        if (triggerDown != _previousTriggerDown) {
            logger::trace(
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
                    const bool inventoryScrollbarCapture =
                        _activeDocument == _inventoryDocument &&
                        IsInventoryScrollbarElement(captureElement);
                    const bool magicScrollbarCapture =
                        _activeDocument == _magicDocument &&
                        IsMagicScrollbarElement(captureElement);
                    const bool scrollbarCapture =
                        inventoryScrollbarCapture || magicScrollbarCapture ||
                        IsScrollbarElement(captureElement);
                    _inventoryScrollbarDragging = inventoryScrollbarCapture;
                    _magicScrollbarDragging = magicScrollbarCapture;
                    if (scrollbarCapture) {
                        UpdateVirtualScrollbarPosition(
                            pointerY,
                            inventoryScrollbarCapture,
                            magicScrollbarCapture);
                    }
                    if (!scrollbarCapture) {
                        while (captureElement && captureElement != _activeDocument &&
                               captureElement->GetTagName() != "button" &&
                               captureElement->GetTagName() != "input" &&
                               !IsActionCard(captureElement)) {
                            captureElement = captureElement->GetParentNode();
                        }
                    }
                    if (scrollbarCapture) {
                        _triggerCaptureMode = TriggerCaptureMode::kScrollbar;
                        _triggerScrollLockDocument = nullptr;
                        _triggerScrollTarget = nullptr;
                        _triggerScrollLockActive = false;
                        _triggerScrollReleasePending = false;
                        _triggerScrollSuppressionLogged = false;
                    } else if (captureElement &&
                               captureElement->GetTagName() == "input") {
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
                        "slider" :
                        (_triggerCaptureMode == TriggerCaptureMode::kScrollbar ?
                             "scrollbar" :
                             (_triggerCaptureMode == TriggerCaptureMode::kButton ?
                                  "button" :
                                  "none"));
                    logger::trace("DragonBoardVR: RmlUi trigger captured {} target.", captureName);
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
                    _inventoryScrollbarDragging = false;
                    _magicScrollbarDragging = false;
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

    void DragonBoardRmlUi::CaptureGripScrollHoverLock()
    {
        ClearGripScrollHoverLock();
        const std::string_view hoveredId(_hoveredElementId);
        std::string_view suffix;
        if (_activeDocument == _inventoryDocument &&
            hoveredId.starts_with("inventory-item-")) {
            _gripScrollHoverList = GripScrollHoverList::kInventory;
            suffix = hoveredId.substr(std::string_view("inventory-item-").size());
        } else if (_activeDocument == _magicDocument &&
                   hoveredId.starts_with("magic-spell-")) {
            _gripScrollHoverList = GripScrollHoverList::kMagic;
            suffix = hoveredId.substr(std::string_view("magic-spell-").size());
        } else {
            return;
        }

        std::uint32_t itemIndex = 0;
        if (!ParseDecimal(suffix, itemIndex)) {
            _gripScrollHoverList = GripScrollHoverList::kNone;
            return;
        }
        _gripScrollHoverItemIndex = static_cast<std::size_t>(itemIndex);
        if (auto* list = _gripScrollHoverList == GripScrollHoverList::kInventory ?
                _inventoryDocument->GetElementById("inventory-item-list") :
                _magicDocument->GetElementById("magic-spell-list")) {
            list->SetProperty("pointer-events", "none");
        }
    }

    void DragonBoardRmlUi::ApplyGripScrollHoverLock()
    {
        if (!_gripScrollActive) return;
        for (auto& row : _inventoryVirtualRows) {
            if (!row.button) continue;
            const bool locked =
                _gripScrollHoverList == GripScrollHoverList::kInventory &&
                row.itemIndex == _gripScrollHoverItemIndex;
            row.button->SetPseudoClass("hover", false);
            row.button->SetClass("grip-scroll-locked", locked);
        }
        for (auto& row : _magicVirtualRows) {
            if (!row.button) continue;
            const bool locked =
                _gripScrollHoverList == GripScrollHoverList::kMagic &&
                row.itemIndex == _gripScrollHoverItemIndex;
            row.button->SetPseudoClass("hover", false);
            row.button->SetClass("grip-scroll-locked", locked);
        }
    }

    void DragonBoardRmlUi::ClearGripScrollHoverLock()
    {
        for (auto& row : _inventoryVirtualRows) {
            if (row.button) {
                row.button->SetPseudoClass("hover", false);
                row.button->SetClass("grip-scroll-locked", false);
            }
        }
        for (auto& row : _magicVirtualRows) {
            if (row.button) {
                row.button->SetPseudoClass("hover", false);
                row.button->SetClass("grip-scroll-locked", false);
            }
        }
        if (_inventoryDocument) {
            if (auto* list = _inventoryDocument->GetElementById("inventory-item-list")) {
                list->RemoveProperty("pointer-events");
            }
        }
        if (_magicDocument) {
            if (auto* list = _magicDocument->GetElementById("magic-spell-list")) {
                list->RemoveProperty("pointer-events");
            }
        }
        _gripScrollHoverList = GripScrollHoverList::kNone;
        _gripScrollHoverItemIndex = static_cast<std::size_t>(-1);
    }

    bool DragonBoardRmlUi::RequiresContinuousRendering() const
    {
        return _pointerMotionActive || _inventoryMarqueeActive || _gripScrollActive ||
               _triggerCaptureMode == TriggerCaptureMode::kSlider ||
               _triggerCaptureMode == TriggerCaptureMode::kScrollbar ||
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
        if (_galleryDocument) _galleryDocument->Hide();
        if (_welcomeDocument) _welcomeDocument->Hide();
        if (_keyboardDocument) _keyboardDocument->Hide();
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

    void DragonBoardRmlUi::UpdateVirtualScrollbarPosition(
        int pointerY,
        bool inventoryScrollbar,
        bool magicScrollbar)
    {
        constexpr float kViewportHeight = 600.0f;
        constexpr float kRowHeight = 120.0f;

        const auto update = [pointerY](
                                Rml::ElementDocument* document,
                                const char* proxyId,
                                std::size_t itemCount,
                                float& scrollTop) {
            if (!document) return;
            auto* proxy = document->GetElementById(proxyId);
            if (!proxy) return;

            const float totalHeight =
                static_cast<float>(itemCount) * kRowHeight;
            const float maximumScroll =
                std::max(0.0f, totalHeight - kViewportHeight);
            const float thumbHeight = totalHeight > 0.0f ?
                std::clamp(
                    kViewportHeight * kViewportHeight / totalHeight,
                    104.0f,
                    kViewportHeight) :
                kViewportHeight;
            const float thumbTravel = kViewportHeight - thumbHeight;
            const float pointerOnTrack =
                static_cast<float>(pointerY) -
                proxy->GetAbsoluteOffset(Rml::BoxArea::Border).y -
                thumbHeight * 0.5f;
            const float ratio = thumbTravel > 0.0f ?
                std::clamp(pointerOnTrack / thumbTravel, 0.0f, 1.0f) :
                0.0f;
            scrollTop = ratio * maximumScroll;
        };

        if (inventoryScrollbar && _activeDocument == _inventoryDocument) {
            update(
                _inventoryDocument,
                "inventory-scroll-proxy",
                _inventoryVirtualItems.size(),
                _inventorySyncedScrollTop);
        }
        if (magicScrollbar && _activeDocument == _magicDocument) {
            update(
                _magicDocument,
                "magic-scroll-proxy",
                _magicVirtualItems.size(),
                _magicSyncedScrollTop);
        }
    }

    void DragonBoardRmlUi::ResetInventoryMarquee()
    {
        if (_activeDocument && !_inventoryMarqueeElementId.empty()) {
            if (auto* element =
                    _activeDocument->GetElementById(_inventoryMarqueeElementId)) {
                element->SetProperty("left", "0px");
                element->RemoveProperty("width");
                element->RemoveProperty("max-width");
                element->RemoveProperty("overflow");
                element->RemoveProperty("text-overflow");
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
        const auto trackWidth =
            Rml::CreateString("%.1fpx", std::max(textWidth, viewport->GetClientWidth()));
        track->SetProperty("width", trackWidth);
        track->SetProperty("max-width", trackWidth);
        track->SetProperty("overflow", "visible");
        track->SetProperty("text-overflow", "clip");
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
        auto* hovered = _context ? _context->GetHoverElement() : nullptr;
        if (IsScrollbarElement(hovered)) {
            _triggerScrollLockDocument = nullptr;
            _triggerScrollTarget = nullptr;
            _triggerScrollLockActive = false;
            _triggerScrollReleasePending = false;
            _triggerScrollSuppressionLogged = false;
            return;
        }
        _triggerScrollTarget = hovered ? hovered->GetClosestScrollableContainer() : nullptr;
        _triggerScrollLockPageTop = page ? page->GetScrollTop() : 0.0f;
        _triggerScrollLockNestedTop = nested ? nested->GetScrollTop() : 0.0f;
        _triggerScrollTargetTop = _triggerScrollTarget ?
            _triggerScrollTarget->GetScrollTop() : 0.0f;
        _triggerScrollLockActive = true;
        _triggerScrollReleasePending = false;
        _triggerScrollSuppressionLogged = false;
    }

    void DragonBoardRmlUi::RestoreTriggerScrollLock()
    {
        if (!_triggerScrollLockActive) return;
        if (!_activeDocument || _activeDocument != _triggerScrollLockDocument) {
            _triggerScrollLockDocument = nullptr;
            _triggerScrollTarget = nullptr;
            _triggerScrollLockActive = false;
            _triggerScrollReleasePending = false;
            return;
        }

        auto* page = _activeDocument->GetElementById("page-scroll");
        auto* nested = _activeDocument->GetElementById("dev-command-list");
        const bool pageMoved = page && page->GetScrollTop() != _triggerScrollLockPageTop;
        const bool nestedMoved = nested && nested->GetScrollTop() != _triggerScrollLockNestedTop;
        const bool targetMoved = _triggerScrollTarget &&
            _triggerScrollTarget->GetScrollTop() != _triggerScrollTargetTop;
        if (pageMoved) page->SetScrollTop(_triggerScrollLockPageTop);
        if (nestedMoved) nested->SetScrollTop(_triggerScrollLockNestedTop);
        if (targetMoved) _triggerScrollTarget->SetScrollTop(_triggerScrollTargetTop);

        if ((pageMoved || nestedMoved || targetMoved) && !_triggerScrollSuppressionLogged) {
            logger::trace(
                "DragonBoardVR: suppressed RmlUi trigger drag scroll (page={}, nested={}, target={}).",
                pageMoved,
                nestedMoved,
                targetMoved);
            _triggerScrollSuppressionLogged = true;
        }

        if (_triggerScrollReleasePending) {
            _triggerScrollLockDocument = nullptr;
            _triggerScrollTarget = nullptr;
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
            logger::trace(
                "DragonBoardVR: RmlUi page scrollTop {:.1f} -> {:.1f} (trigger={}, grip={}).",
                _observedPageScrollTop,
                pageTop,
                _currentTriggerDown,
                _currentGripDown);
            _observedPageScrollTop = pageTop;
        }
        if (nestedTop != _observedNestedScrollTop) {
            logger::trace(
                "DragonBoardVR: RmlUi nested scrollTop {:.1f} -> {:.1f} (trigger={}, grip={}).",
                _observedNestedScrollTop,
                nestedTop,
                _currentTriggerDown,
                _currentGripDown);
            _observedNestedScrollTop = nestedTop;
        }
    }

    bool DragonBoardRmlUi::Render(
        ID3D11RenderTargetView* renderTarget,
        int renderWidth,
        int renderHeight,
        int logicalWidth,
        int logicalHeight)
    {
        if (!IsReady()) return false;
        _lastRenderTiming = {};
        _lastRenderTiming.width = renderWidth;
        _lastRenderTiming.height = renderHeight;
        if (_activeDocument == _settingsDocument) _lastRenderTiming.activeDocument = "Settings";
        else if (_activeDocument == _developerDocument) _lastRenderTiming.activeDocument = "Developer";
        else if (_activeDocument == _itemEditDocument) _lastRenderTiming.activeDocument = "Item Editor";
        else if (_activeDocument == _modsDocument) _lastRenderTiming.activeDocument = "Mods";
        else if (_activeDocument == _inventoryDocument) _lastRenderTiming.activeDocument = "Inventory";
        else if (_activeDocument == _magicDocument) _lastRenderTiming.activeDocument = "Magic";
        else if (_activeDocument == _journalDocument) _lastRenderTiming.activeDocument = "Journal";
        else if (_activeDocument == _welcomeDocument) _lastRenderTiming.activeDocument = "Welcome";
        else if (_activeDocument == _keyboardDocument) _lastRenderTiming.activeDocument = "Keyboard";
        else if (const auto handle = FindPanelHandle(_activeDocument); handle != 0) {
            if (const auto* panel = FindPanel(handle)) {
                _lastRenderTiming.activeDocument = panel->id;
            }
        }
        if (_lastRenderTiming.activeDocument.empty()) {
            _lastRenderTiming.activeDocument = "<none>";
        }

        const Rml::Vector2i dimensions(logicalWidth, logicalHeight);
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
        const bool beganFrame = _renderer->BeginFrame(
            renderTarget,
            renderWidth,
            renderHeight,
            logicalWidth,
            logicalHeight);
        const auto beginEnded = std::chrono::steady_clock::now();
        _lastRenderTiming.beginFrameMs = Milliseconds(beginStarted, beginEnded);
        if (!beganFrame) {
            _lastRenderTiming.totalMs =
                _lastRenderTiming.updateMs + _lastRenderTiming.beginFrameMs;
            return false;
        }
        const auto storeStateTiming = [this] {
            const auto& timing = _renderer->GetLastStateTiming();
            _lastRenderTiming.dx11StateMs = timing.totalMs;
            _lastRenderTiming.dx11RenderTargetsMs = timing.renderTargetsMs;
            _lastRenderTiming.dx11ViewportScissorMs = timing.viewportScissorMs;
            _lastRenderTiming.dx11RasterizerMs = timing.rasterizerMs;
            _lastRenderTiming.dx11BlendDepthMs = timing.blendDepthMs;
            _lastRenderTiming.dx11InputAssemblyMs = timing.inputAssemblyMs;
            _lastRenderTiming.dx11ShadersMs = timing.shadersMs;
            _lastRenderTiming.dx11ResourcesMs = timing.resourcesMs;
        };
        try {
            const auto renderStarted = std::chrono::steady_clock::now();
            const bool rendered = _context->Render();
            const auto renderEnded = std::chrono::steady_clock::now();
            _lastRenderTiming.renderMs = Milliseconds(renderStarted, renderEnded);

            const auto endStarted = std::chrono::steady_clock::now();
            _renderer->EndFrame();
            const auto endEnded = std::chrono::steady_clock::now();
            _lastRenderTiming.endFrameMs = Milliseconds(endStarted, endEnded);
            storeStateTiming();
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
            storeStateTiming();
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

    bool DragonBoardRmlUi::ConsumePositionAdjustmentRequested()
    {
        return std::exchange(_positionAdjustmentRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeDeveloperPanelToggleRequested()
    {
        return std::exchange(_developerPanelToggleRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeLockPinsToggleRequested()
    {
        return std::exchange(_lockPinsToggleRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeShowTutorialsToggleRequested()
    {
        return std::exchange(_showTutorialsToggleRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeStatusWidgetToggleRequested()
    {
        return std::exchange(_statusWidgetToggleRequested, false);
    }

    std::optional<std::string>
    DragonBoardRmlUi::ConsumeLanguageSelectionRequested()
    {
        return std::exchange(_languageSelectionRequested, std::nullopt);
    }

    bool DragonBoardRmlUi::ConsumeWorldPinToggleRequested()
    {
        return std::exchange(_worldPinToggleRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeRestartRequested()
    {
        return std::exchange(_restartRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeWelcomeNextRequested()
    {
        return std::exchange(_welcomeNextRequested, false);
    }

    bool DragonBoardRmlUi::ConsumeWelcomeCloseRequested()
    {
        return std::exchange(_welcomeCloseRequested, false);
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

    std::pair<DragonBoardRmlUi::GalleryAction, std::size_t>
    DragonBoardRmlUi::ConsumeGalleryAction()
    {
        const auto result = std::pair{ _galleryAction, _galleryActionIndex };
        _galleryAction = GalleryAction::kNone;
        _galleryActionIndex = 0;
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
            markup = "<div class=\"mods-empty\">" +
                EscapeRml(Tr("No mods added yet")) + "</div>";
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

    void DragonBoardRmlUi::SetIniMods(
        const std::vector<IniModInfo>& mods,
        std::string_view status,
        bool scanning,
        std::size_t selectedIndex,
        std::string_view searchQuery,
        bool showHidden,
        std::size_t hiddenCount)
    {
        if (!_modsDocument) return;
        if (auto* statusElement = _modsDocument->GetElementById("mods-ini-status")) {
            statusElement->SetInnerRML(EscapeRml(status));
        }
        if (auto* refresh = _modsDocument->GetElementById("mods-ini-refresh")) {
            refresh->SetClass("disabled", scanning);
        }
        if (auto* searchText =
                _modsDocument->GetElementById("mods-ini-search-text")) {
            searchText->SetInnerRML(EscapeRml(
                searchQuery.empty() ? Tr("SEARCH") : std::string(searchQuery)));
        }
        if (auto* search = _modsDocument->GetElementById("mods-ini-search")) {
            search->SetClass("active", !searchQuery.empty());
        }
        if (auto* clear = _modsDocument->GetElementById("mods-ini-search-clear")) {
            clear->SetProperty("display", searchQuery.empty() ? "none" : "flex");
        }
        if (auto* hidden = _modsDocument->GetElementById("mods-ini-show-hidden")) {
            hidden->SetClass("active", showHidden);
            hidden->SetInnerRML(
                "<span>" + EscapeRml(Tr(showHidden ? "VISIBLE" : "HIDDEN")) +
                (showHidden ? "" : " (") +
                (showHidden ? "" : std::to_string(hiddenCount) + ")") + "</span>");
        }
        auto* list = _modsDocument->GetElementById("mods-ini-list");
        if (!list) return;
        std::string markup;
        if (mods.empty()) {
            markup = "<div class=\"mods-empty\">" +
                EscapeRml(Tr(showHidden ?
                    "No hidden INI mods" :
                    "No matching editable INIs")) +
                "</div>";
        } else {
            for (std::size_t index = 0; index < mods.size(); ++index) {
                const auto& mod = mods[index];
                markup += "<div class=\"ini-mod-row\"><div id=\"ini-mod-" +
                    std::to_string(index) +
                    "\" class=\"ini-mod-card";
                if (index == selectedIndex) markup += " active";
                if (mod.conflictCount > 0) markup += " ini-conflict";
                if (mod.hidden) markup += " hidden-entry";
                markup += "\" tabindex=\"0\">"
                    "<div class=\"sidebar-fade\"></div>"
                    "<span class=\"ini-mod-name\">" +
                    EscapeRml(mod.name) + "</span></div>"
                    "<button id=\"ini-visibility-" + std::to_string(index) +
                    "\" class=\"ini-row-visibility" +
                    std::string(mod.hidden ? " restore" : "") +
                    "\"><span>" + (mod.hidden ? "+" : "X") +
                    "</span></button></div>";
            }
        }
        if (markup == _iniModsListMarkup) return;
        list->SetInnerRML(markup);
        _iniModsListMarkup = std::move(markup);
        _hoveredElementId.clear();
        for (std::size_t index = 0; index < mods.size(); ++index) {
            BindClick(_modsDocument, ("ini-mod-" + std::to_string(index)).c_str());
            BindClick(
                _modsDocument,
                ("ini-visibility-" + std::to_string(index)).c_str());
        }
    }

    void DragonBoardRmlUi::SetIniEditor(const std::optional<IniEditorInfo>& info)
    {
        if (!_modsDocument) return;
        auto* head = _modsDocument->GetElementById("mods-ini-editor-head");
        auto* title = _modsDocument->GetElementById("mods-ini-mod-title");
        auto* tabs = _modsDocument->GetElementById("mods-ini-file-tabs");
        auto* detail = _modsDocument->GetElementById("mods-ini-detail");
        if (!head || !title || !tabs || !detail) return;

        std::string markup;
        std::string tabsMarkup;
        std::vector<bool> booleanTypes;
        if (!info) {
            head->SetProperty("display", "none");
            title->SetInnerRML("");
            if (!_iniFileTabsMarkup.empty()) {
                tabs->SetInnerRML("");
                _iniFileTabsMarkup.clear();
            }
            markup =
                "<div class=\"ini-detail-empty\">" +
                EscapeRml(Tr("Select a mod to view its settings")) +
                "</div>";
        } else {
            head->SetProperty("display", "flex");
            title->SetInnerRML(EscapeRml(info->modName));
            for (std::size_t fileIndex = 0; fileIndex < info->files.size(); ++fileIndex) {
                const auto& file = info->files[fileIndex];
                tabsMarkup += "<button id=\"ini-file-tab-" +
                    std::to_string(fileIndex) + "\" class=\"ini-file-tab";
                if (fileIndex == info->selectedFileIndex) tabsMarkup += " active";
                if (file.hasConflict) tabsMarkup += " conflict";
                tabsMarkup += "\"><span>" + EscapeRml(file.name) + "</span></button>";
            }
            if (tabsMarkup != _iniFileTabsMarkup) {
                tabs->SetInnerRML(tabsMarkup);
                _iniFileTabsMarkup = tabsMarkup;
                for (std::size_t fileIndex = 0; fileIndex < info->files.size();
                     ++fileIndex) {
                    BindClick(
                        _modsDocument,
                        ("ini-file-tab-" + std::to_string(fileIndex)).c_str());
                }
            }

            std::size_t settingIndex = 0;
            if (info->selectedFileIndex < info->files.size()) {
                const auto& file = info->files[info->selectedFileIndex];
                markup += "<div class=\"ini-file";
                if (file.hasConflict) markup += " conflict";
                markup += "\">";
                std::string currentSection;
                bool sectionStarted = false;
                for (const auto& setting : file.settings) {
                    if (!sectionStarted || setting.section != currentSection) {
                        currentSection = setting.section;
                        sectionStarted = true;
                        markup += "<div class=\"ini-section-name\">[" +
                            EscapeRml(
                                currentSection.empty() ? Tr("General") : currentSection) +
                            "]</div>";
                    }
                    markup += "<div class=\"ini-setting-row\"><span class=\"ini-setting-key\">" +
                        EscapeRml(setting.key) +
                        "</span><div class=\"ini-setting-value-column\">";
                    if (!setting.description.empty()) {
                        markup += "<div class=\"ini-setting-description\">" +
                            EscapeRml(setting.description) + "</div>";
                    }
                    markup += "<button id=\"";
                    if (setting.isBoolean) {
                        markup += "ini-toggle-" + std::to_string(settingIndex) +
                            "\" class=\"ini-setting-value boolean";
                        if (setting.booleanValue) markup += " enabled";
                    } else {
                        markup += "ini-value-" + std::to_string(settingIndex) +
                            "\" class=\"ini-setting-value";
                    }
                    if (setting.dirty) markup += " dirty";
                    markup += "\"><span>";
                    if (setting.sensitive) {
                        markup += "********";
                    } else if (setting.isBoolean) {
                        markup += setting.booleanValue ? "true" : "false";
                    } else {
                        markup += EscapeRml(setting.value);
                    }
                    markup += "</span></button></div></div>";
                    booleanTypes.push_back(setting.isBoolean);
                    ++settingIndex;
                }
                markup += "</div>";
            }
        }

        if (markup != _iniEditorMarkup) {
            detail->SetInnerRML(markup);
            _iniEditorMarkup = std::move(markup);
            for (std::size_t index = 0; index < booleanTypes.size(); ++index) {
                const auto prefix = booleanTypes[index] ? "ini-toggle-" : "ini-value-";
                BindClick(_modsDocument, (prefix + std::to_string(index)).c_str());
            }
        }

        const auto dirtyCount = info ? info->dirtyCount : 0;
        if (auto* dirty = _modsDocument->GetElementById("mods-ini-dirty-status")) {
            dirty->SetInnerRML(
                dirtyCount == 0 ?
                    Tr("NO UNSAVED CHANGES") :
                    std::to_string(dirtyCount) + " " +
                        Tr(dirtyCount == 1 ? "UNSAVED CHANGE" : "UNSAVED CHANGES"));
        }
        for (const auto* id : { "mods-ini-save", "mods-ini-discard" }) {
            if (auto* button = _modsDocument->GetElementById(id)) {
                button->SetClass("disabled", dirtyCount == 0);
            }
        }
    }

    void DragonBoardRmlUi::ResetInventoryVirtualRows()
    {
        ResetInventoryMarquee();
        _inventoryVirtualRows.clear();
        _inventoryVirtualList.Reset();
        _inventoryVirtualSelectedIndex = static_cast<std::size_t>(-1);
        _inventoryVirtualInitialized = false;
        _inventorySyncedScrollTop = 0.0f;
        if (_hoveredElementId.starts_with("inventory-item-")) {
            _hoveredElementId.clear();
        }
    }

    void DragonBoardRmlUi::UpdateInventoryVirtualRows(bool refreshVisibleRows)
    {
        if (!_inventoryDocument || _inventoryVirtualItems.empty()) return;
        auto* list = _inventoryDocument->GetElementById("inventory-item-list");
        if (!list) return;

        _inventoryVirtualList.SetItemCount(_inventoryVirtualItems.size());
        const auto poolSize = _inventoryVirtualList.GetPoolSize();
        auto* content = _inventoryDocument->GetElementById("inventory-virtual-content");
        if (!_inventoryVirtualInitialized || !content ||
            _inventoryVirtualRows.size() != poolSize) {
            ResetInventoryMarquee();
            std::string markup =
                "<div id=\"inventory-virtual-content\" class=\"inventory-virtual-content\">";
            for (std::size_t slot = 0; slot < poolSize; ++slot) {
                const auto suffix = std::to_string(slot);
                markup +=
                    "<button id=\"inventory-virtual-row-slot-" + suffix +
                    "\" class=\"inventory-list-item\" style=\"display: none;\">"
                    "<span class=\"row-fade\"></span>"
                    "<span id=\"inventory-virtual-state-slot-" + suffix +
                    "\" class=\"item-state-mark\"></span>"
                    "<span id=\"inventory-virtual-name-slot-" + suffix +
                    "\" class=\"item-name\"><span id=\"inventory-virtual-track-slot-" +
                    suffix + "\" class=\"item-name-track\"></span></span>"
                    "<span id=\"inventory-virtual-stack-slot-" + suffix +
                    "\" class=\"item-stack\"></span></button>";
            }
            markup += "</div>";
            list->SetInnerRML(markup);
            content = _inventoryDocument->GetElementById("inventory-virtual-content");
            _inventoryVirtualRows.clear();
            _inventoryVirtualRows.reserve(poolSize);
            for (std::size_t slot = 0; slot < poolSize; ++slot) {
                const auto suffix = std::to_string(slot);
                VirtualListRow row;
                row.button = _inventoryDocument->GetElementById(
                    "inventory-virtual-row-slot-" + suffix);
                row.state = _inventoryDocument->GetElementById(
                    "inventory-virtual-state-slot-" + suffix);
                row.nameViewport = _inventoryDocument->GetElementById(
                    "inventory-virtual-name-slot-" + suffix);
                row.nameTrack = _inventoryDocument->GetElementById(
                    "inventory-virtual-track-slot-" + suffix);
                row.stack = _inventoryDocument->GetElementById(
                    "inventory-virtual-stack-slot-" + suffix);
                if (row.button) {
                    row.button->AddEventListener("click", _eventListener.get());
                }
                _inventoryVirtualRows.push_back(std::move(row));
            }
            _inventoryVirtualInitialized = true;
            _inventoryVirtualList.Reset();
            _inventoryVirtualList.SetItemCount(_inventoryVirtualItems.size());
            refreshVisibleRows = true;
            logger::trace(
                "DragonBoardVR: virtualized Inventory uses {} pooled rows for {} items.",
                _inventoryVirtualRows.size(),
                _inventoryVirtualItems.size());
        }
        if (!content) return;

        constexpr float kViewportHeight = 600.0f;
        constexpr float kRowHeight = 120.0f;
        const float totalHeight =
            static_cast<float>(_inventoryVirtualItems.size()) * kRowHeight;
        const float maximumScroll = std::max(0.0f, totalHeight - kViewportHeight);
        const float scrollTop =
            std::clamp(_inventorySyncedScrollTop, 0.0f, maximumScroll);
        _inventorySyncedScrollTop = scrollTop;
        list->SetScrollTop(0.0f);
        const bool windowChanged =
            _inventoryVirtualList.Update(scrollTop + kRowHeight * 0.5f);
        const auto& window = _inventoryVirtualList.GetWindow();
        content->SetProperty("height", "600px");
        content->RemoveProperty("top");
        if (auto* thumb =
                _inventoryDocument->GetElementById("inventory-scroll-thumb")) {
            const float thumbHeight = totalHeight > 0.0f ?
                std::clamp(
                    kViewportHeight * kViewportHeight / totalHeight,
                    104.0f,
                    kViewportHeight) :
                kViewportHeight;
            const float thumbTravel = kViewportHeight - thumbHeight;
            const float thumbTop = maximumScroll > 0.0f ?
                thumbTravel * scrollTop / maximumScroll : 0.0f;
            thumb->SetProperty(
                "height", Rml::CreateString("%.0fpx", thumbHeight));
            thumb->SetProperty("top", Rml::CreateString("%.0fpx", thumbTop));
        }
        if (!windowChanged && !refreshVisibleRows) return;
        if (windowChanged) {
            ResetInventoryMarquee();
            if (_hoveredElementId.starts_with("inventory-item-")) {
                _hoveredElementId.clear();
            }
        }

        if (windowChanged) {
            // Move every pooled element to a temporary id first, avoiding
            // duplicates while the real item indices change after scrolling.
            for (std::size_t slot = 0; slot < _inventoryVirtualRows.size(); ++slot) {
                auto& row = _inventoryVirtualRows[slot];
                const auto suffix = std::to_string(slot);
                if (row.button) row.button->SetId("inventory-virtual-row-slot-" + suffix);
                if (row.nameViewport) {
                    row.nameViewport->SetId("inventory-virtual-name-slot-" + suffix);
                }
                if (row.nameTrack) {
                    row.nameTrack->SetId("inventory-virtual-track-slot-" + suffix);
                }
            }
        }

        for (std::size_t slot = 0; slot < _inventoryVirtualRows.size(); ++slot) {
            auto& row = _inventoryVirtualRows[slot];
            const auto itemIndex = window.firstIndex + slot;
            if (!row.button || itemIndex >= _inventoryVirtualItems.size() ||
                slot >= window.rowCount) {
                if (row.button) row.button->SetProperty("display", "none");
                row.itemIndex = static_cast<std::size_t>(-1);
                row.contentKey.clear();
                row.classNames.clear();
                continue;
            }

            const auto& item = _inventoryVirtualItems[itemIndex];
            const auto indexText = std::to_string(itemIndex);
            if (windowChanged) {
                row.button->SetId("inventory-item-" + indexText);
                if (row.nameViewport) {
                    row.nameViewport->SetId("inventory-item-name-" + indexText);
                }
                if (row.nameTrack) {
                    row.nameTrack->SetId("inventory-item-name-track-" + indexText);
                }
                row.button->RemoveProperty("display");
            }
            row.button->SetProperty(
                "top",
                Rml::CreateString(
                    "%.0fpx",
                    static_cast<float>(itemIndex - window.firstIndex) *
                        kRowHeight));

            std::string classes = "inventory-list-item";
            if (itemIndex == _inventoryVirtualSelectedIndex) classes += " active";
            if (item.equipped) classes += " equipped";
            if (item.favorited) classes += " favorited";
            if (classes != row.classNames) {
                row.button->SetClassNames(classes);
                row.classNames = std::move(classes);
            }

            std::string contentKey = item.equipmentMarker;
            contentKey.push_back('\x1f');
            contentKey += item.name;
            contentKey.push_back('\x1f');
            contentKey += std::to_string(item.count);
            if (contentKey != row.contentKey || row.itemIndex != itemIndex) {
                if (row.state) row.state->SetInnerRML(EscapeRml(item.equipmentMarker));
                if (row.nameTrack) row.nameTrack->SetInnerRML(EscapeRml(item.name));
                if (row.stack) {
                    row.stack->SetInnerRML(
                        item.count > 1 ? "x" + std::to_string(item.count) : "");
                }
                row.contentKey = std::move(contentKey);
            }
            row.itemIndex = itemIndex;
        }
    }

    void DragonBoardRmlUi::SetInventory(InventoryInfo info)
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
        const auto setVital = [&](const char* textId,
                                  const char* fillId,
                                  float current,
                                  float maximum) {
            const float safeMaximum = std::max(0.0f, maximum);
            const float safeCurrent = std::clamp(current, 0.0f, safeMaximum);
            setText(
                textId,
                Rml::CreateString("%.0f / %.0f", safeCurrent, safeMaximum));
            if (auto* fill = _inventoryDocument->GetElementById(fillId)) {
                const float percent = safeMaximum > 0.0f ?
                    safeCurrent * 100.0f / safeMaximum : 0.0f;
                fill->SetProperty(
                    "width", Rml::CreateString("%.0fpx", 528.0f * percent / 100.0f));
            }
        };
        setVital(
            "inventory-health-text",
            "inventory-health-fill",
            info.currentHealth,
            info.maximumHealth);
        setVital(
            "inventory-stamina-text",
            "inventory-stamina-fill",
            info.currentStamina,
            info.maximumStamina);
        setVital(
            "inventory-magicka-text",
            "inventory-magicka-fill",
            info.currentMagicka,
            info.maximumMagicka);
        setText("inventory-item-count", std::to_string(info.items.size()));
        setText(
            "inventory-search-text",
            info.searchQuery.empty() ? Tr("SEARCH") : info.searchQuery);
        if (auto* search = _inventoryDocument->GetElementById("inventory-search")) {
            search->SetClass("active", !info.searchQuery.empty());
        }
        if (auto* clear = _inventoryDocument->GetElementById("inventory-search-clear")) {
            clear->SetProperty(
                "display", info.searchQuery.empty() ? "none" : "flex");
        }

        constexpr std::array<std::pair<const char*, const char*>, 8> kInventoryFilters{ {
            { "inventory-filter-favorites", "favorites" },
            { "inventory-filter-weapons", "weapons" },
            { "inventory-filter-armor", "armor" },
            { "inventory-filter-potions", "potions" },
            { "inventory-filter-food", "food" },
            { "inventory-filter-quest", "quest" },
            { "inventory-filter-books", "books" },
            { "inventory-filter-misc", "misc" }
        } };
        for (const auto& [id, filter] : kInventoryFilters) {
            if (auto* element = _inventoryDocument->GetElementById(id)) {
                element->SetClass("active", info.activeFilter == filter);
            }
        }

        const std::string contextKey = info.activeFilter + "\n" + info.searchQuery;
        const bool resetScroll = !_inventoryVirtualInitialized ||
            contextKey != _inventoryVirtualContextKey;
        _inventoryVirtualContextKey = contextKey;
        _inventoryVirtualSelectedIndex = info.items.empty() ?
            static_cast<std::size_t>(-1) :
            std::min(info.selectedIndex, info.items.size() - 1);
        _inventoryVirtualItems = std::move(info.items);

        if (auto* list = _inventoryDocument->GetElementById("inventory-item-list")) {
            if (_inventoryVirtualItems.empty()) {
                ResetInventoryVirtualRows();
                if (auto* thumb =
                        _inventoryDocument->GetElementById("inventory-scroll-thumb")) {
                    thumb->SetProperty("top", "0px");
                    thumb->SetProperty("height", "600px");
                }
                list->SetInnerRML(
                    "<div class=\"inventory-empty\">" +
                    EscapeRml(Tr(info.searchQuery.empty() ?
                        "Inventory is empty" :
                        "No matching items")) +
                    "</div>");
            } else {
                _inventoryVirtualList.SetItemCount(_inventoryVirtualItems.size());
                if (resetScroll) {
                    _inventorySyncedScrollTop = 0.0f;
                }
                UpdateInventoryVirtualRows(true);
            }
        }

        if (_inventoryVirtualItems.empty()) {
            setText("inventory-selected-category", Tr("ITEM"));
            setText("inventory-selected-name", Tr("Inventory is empty"));
            setText("inventory-attack", "--");
            setText("inventory-defense", "--");
            setText("inventory-weight", "--");
            setText("inventory-value", "--");
            setText("inventory-count", "--");
            setText("inventory-description", Tr("No description available."));
            setText("inventory-equip-label", Tr("EQUIP"));
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

        const auto selectedIndex = std::min(
            _inventoryVirtualSelectedIndex, _inventoryVirtualItems.size() - 1);
        const auto& selected = _inventoryVirtualItems[selectedIndex];
        setText("inventory-selected-category", Tr(selected.category));
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
        setText("inventory-description", Tr(selected.description));
        setText("inventory-equip-label", Tr(selected.equipped ? "UNEQUIP" : "EQUIP"));

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

    void DragonBoardRmlUi::ResetMagicVirtualRows()
    {
        ResetInventoryMarquee();
        _magicVirtualRows.clear();
        _magicVirtualList.Reset();
        _magicSyncedScrollTop = 0.0f;
        _magicVirtualSelectedIndex = static_cast<std::size_t>(-1);
        _magicVirtualInitialized = false;
        if (_hoveredElementId.starts_with("magic-spell-")) {
            _hoveredElementId.clear();
        }
    }

    void DragonBoardRmlUi::UpdateMagicVirtualRows(bool refreshVisibleRows)
    {
        if (!_magicDocument || _magicVirtualItems.empty()) return;
        auto* list = _magicDocument->GetElementById("magic-spell-list");
        if (!list) return;

        _magicVirtualList.SetItemCount(_magicVirtualItems.size());
        const auto poolSize = _magicVirtualList.GetPoolSize();
        auto* content = _magicDocument->GetElementById("magic-virtual-content");
        if (!_magicVirtualInitialized || !content ||
            _magicVirtualRows.size() != poolSize) {
            ResetInventoryMarquee();
            std::string markup =
                "<div id=\"magic-virtual-content\" class=\"magic-virtual-content\">";
            for (std::size_t slot = 0; slot < poolSize; ++slot) {
                const auto suffix = std::to_string(slot);
                markup +=
                    "<button id=\"magic-virtual-row-slot-" + suffix +
                    "\" class=\"magic-list-item\" style=\"display: none;\">"
                    "<span class=\"row-fade\"></span>"
                    "<span id=\"magic-virtual-state-slot-" + suffix +
                    "\" class=\"spell-state-mark\"></span>"
                    "<span id=\"magic-virtual-name-slot-" + suffix +
                    "\" class=\"spell-name\"><span id=\"magic-virtual-track-slot-" +
                    suffix + "\" class=\"spell-name-track\"></span></span></button>";
            }
            markup += "</div>";
            list->SetInnerRML(markup);
            content = _magicDocument->GetElementById("magic-virtual-content");
            _magicVirtualRows.clear();
            _magicVirtualRows.reserve(poolSize);
            for (std::size_t slot = 0; slot < poolSize; ++slot) {
                const auto suffix = std::to_string(slot);
                VirtualListRow row;
                row.button = _magicDocument->GetElementById(
                    "magic-virtual-row-slot-" + suffix);
                row.state = _magicDocument->GetElementById(
                    "magic-virtual-state-slot-" + suffix);
                row.nameViewport = _magicDocument->GetElementById(
                    "magic-virtual-name-slot-" + suffix);
                row.nameTrack = _magicDocument->GetElementById(
                    "magic-virtual-track-slot-" + suffix);
                if (row.button) {
                    row.button->AddEventListener("click", _eventListener.get());
                }
                _magicVirtualRows.push_back(std::move(row));
            }
            _magicVirtualInitialized = true;
            _magicVirtualList.Reset();
            _magicVirtualList.SetItemCount(_magicVirtualItems.size());
            refreshVisibleRows = true;
            logger::trace(
                "DragonBoardVR: virtualized Magic uses {} pooled rows for {} spells.",
                _magicVirtualRows.size(),
                _magicVirtualItems.size());
        }
        if (!content) return;

        constexpr float kViewportHeight = 600.0f;
        constexpr float kRowHeight = 120.0f;
        const float totalHeight =
            static_cast<float>(_magicVirtualItems.size()) * kRowHeight;
        const float maximumScroll =
            std::max(0.0f, totalHeight - kViewportHeight);
        const float scrollTop =
            std::clamp(_magicSyncedScrollTop, 0.0f, maximumScroll);
        _magicSyncedScrollTop = scrollTop;
        list->SetScrollTop(0.0f);
        const bool windowChanged =
            _magicVirtualList.Update(scrollTop + kRowHeight * 0.5f);
        const auto& window = _magicVirtualList.GetWindow();
        content->SetProperty("height", "600px");
        if (auto* thumb = _magicDocument->GetElementById("magic-scroll-thumb")) {
            const float thumbHeight = totalHeight > 0.0f ?
                std::clamp(
                    kViewportHeight * kViewportHeight / totalHeight,
                    104.0f,
                    kViewportHeight) :
                kViewportHeight;
            const float thumbTravel = kViewportHeight - thumbHeight;
            const float thumbTop = maximumScroll > 0.0f ?
                thumbTravel * scrollTop / maximumScroll :
                0.0f;
            thumb->SetProperty(
                "height", Rml::CreateString("%.0fpx", thumbHeight));
            thumb->SetProperty(
                "top", Rml::CreateString("%.0fpx", thumbTop));
        }
        if (!windowChanged && !refreshVisibleRows) return;
        if (windowChanged) {
            ResetInventoryMarquee();
            if (_hoveredElementId.starts_with("magic-spell-")) {
                _hoveredElementId.clear();
            }
            for (std::size_t slot = 0; slot < _magicVirtualRows.size(); ++slot) {
                auto& row = _magicVirtualRows[slot];
                const auto suffix = std::to_string(slot);
                if (row.button) row.button->SetId("magic-virtual-row-slot-" + suffix);
                if (row.nameViewport) {
                    row.nameViewport->SetId("magic-virtual-name-slot-" + suffix);
                }
                if (row.nameTrack) {
                    row.nameTrack->SetId("magic-virtual-track-slot-" + suffix);
                }
            }
        }

        for (std::size_t slot = 0; slot < _magicVirtualRows.size(); ++slot) {
            auto& row = _magicVirtualRows[slot];
            const auto itemIndex = window.firstIndex + slot;
            if (!row.button || itemIndex >= _magicVirtualItems.size() ||
                slot >= window.rowCount) {
                if (row.button) row.button->SetProperty("display", "none");
                row.itemIndex = static_cast<std::size_t>(-1);
                row.contentKey.clear();
                row.classNames.clear();
                continue;
            }

            const auto& item = _magicVirtualItems[itemIndex];
            const auto indexText = std::to_string(itemIndex);
            if (windowChanged) {
                row.button->SetId("magic-spell-" + indexText);
                if (row.nameViewport) {
                    row.nameViewport->SetId("magic-spell-name-" + indexText);
                }
                if (row.nameTrack) {
                    row.nameTrack->SetId("magic-spell-name-track-" + indexText);
                }
                row.button->RemoveProperty("display");
                row.button->SetProperty(
                    "top",
                    Rml::CreateString("%.0fpx", static_cast<float>(slot) * kRowHeight));
            }

            std::string classes = "magic-list-item";
            if (itemIndex == _magicVirtualSelectedIndex) classes += " active";
            if (item.equipped) classes += " equipped";
            if (item.favorited) classes += " favorited";
            if (classes != row.classNames) {
                row.button->SetClassNames(classes);
                row.classNames = std::move(classes);
            }

            std::string marker;
            if (item.equippedLeft && item.equippedRight) marker = "[L/R]";
            else if (item.equippedLeft) marker = "[L]";
            else if (item.equippedRight) marker = "[R]";
            std::string contentKey = marker;
            contentKey.push_back('\x1f');
            contentKey += item.name;
            if (contentKey != row.contentKey || row.itemIndex != itemIndex) {
                if (row.state) row.state->SetInnerRML(marker);
                if (row.nameTrack) row.nameTrack->SetInnerRML(EscapeRml(item.name));
                row.contentKey = std::move(contentKey);
            }
            row.itemIndex = itemIndex;
        }
    }

    void DragonBoardRmlUi::SetMagic(MagicInfo info)
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
        const float safeMaximumMagicka = std::max(0.0f, info.maximumMagicka);
        const float safeCurrentMagicka =
            std::clamp(info.currentMagicka, 0.0f, safeMaximumMagicka);
        setText(
            "magic-magicka",
            Rml::CreateString(
                "%.0f / %.0f", safeCurrentMagicka, safeMaximumMagicka));
        if (auto* fill = _magicDocument->GetElementById("magic-magicka-fill")) {
            const float ratio = safeMaximumMagicka > 0.0f ?
                safeCurrentMagicka / safeMaximumMagicka :
                0.0f;
            fill->SetProperty("width", Rml::CreateString("%.0fpx", 528.0f * ratio));
        }
        setText("magic-spell-count", std::to_string(info.items.size()));
        setText(
            "magic-search-text",
            info.searchQuery.empty() ? Tr("SEARCH") : info.searchQuery);
        if (auto* search = _magicDocument->GetElementById("magic-search")) {
            search->SetClass("active", !info.searchQuery.empty());
        }
        if (auto* clear = _magicDocument->GetElementById("magic-search-clear")) {
            clear->SetProperty("display", info.searchQuery.empty() ? "none" : "flex");
        }

        constexpr std::array<std::pair<const char*, const char*>, 8> kMagicFilters{ {
            { "magic-filter-favorites", "favorites" },
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

        const std::string contextKey = info.activeFilter + "\n" + info.searchQuery;
        const bool resetScroll = !_magicVirtualInitialized ||
            contextKey != _magicVirtualContextKey;
        _magicVirtualContextKey = contextKey;
        _magicVirtualSelectedIndex = info.items.empty() ?
            static_cast<std::size_t>(-1) :
            std::min(info.selectedIndex, info.items.size() - 1);
        _magicVirtualItems = std::move(info.items);

        if (auto* list = _magicDocument->GetElementById("magic-spell-list")) {
            if (_magicVirtualItems.empty()) {
                ResetMagicVirtualRows();
                list->SetInnerRML(
                    "<div class=\"magic-empty\">" +
                    EscapeRml(Tr(info.searchQuery.empty() ?
                        "No spells available" :
                        "No matching spells")) +
                    "</div>");
                if (auto* thumb =
                        _magicDocument->GetElementById("magic-scroll-thumb")) {
                    thumb->SetProperty("top", "0px");
                    thumb->SetProperty("height", "600px");
                }
            } else {
                _magicVirtualList.SetItemCount(_magicVirtualItems.size());
                if (resetScroll) {
                    _magicSyncedScrollTop = 0.0f;
                    list->SetScrollTop(0.0f);
                }
                UpdateMagicVirtualRows(true);
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

        if (_magicVirtualItems.empty()) {
            setText("magic-selected-category", Tr("MAGIC"));
            setText("magic-selected-name", Tr("No spell selected"));
            setText("magic-cost", "--");
            setText("magic-skill-level", "--");
            setText("magic-cast-type", "--");
            setText("magic-target", "--");
            setText("magic-duration", "--");
            setText("magic-range", "--");
            setText("magic-description", Tr("No description available."));
            setText("magic-equip-label", Tr("EQUIP"));
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

        const auto selectedIndex = std::min(
            _magicVirtualSelectedIndex, _magicVirtualItems.size() - 1);
        const auto& selected = _magicVirtualItems[selectedIndex];
        setText("magic-selected-category", Tr(selected.category));
        setText("magic-selected-name", selected.name);
        setText("magic-cost", selected.canEquip ?
            Rml::CreateString("%.0f", selected.magickaCost) : "--");
        setText("magic-skill-level", Tr(selected.skillLevel));
        setText("magic-cast-type", Tr(selected.castingType));
        setText("magic-target", Tr(selected.delivery));
        setText("magic-duration", Tr(selected.duration));
        setText("magic-range", Tr(selected.range));
        setText("magic-description", Tr(selected.description));
        setText("magic-equip-label", Tr(selected.equipped ? "UNEQUIP" : "EQUIP"));
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
                selected.iconPath.empty() ? "assets/Icons/passiveicon.png" : selected.iconPath);
            icon->SetProperty("display", selected.hasModelPreview ? "none" : "block");
        }
        if (auto* menu = _magicDocument->GetElementById("magic-pin-menu")) {
            menu->SetClass("active", false);
        }
        if (auto* pin = _magicDocument->GetElementById("magic-pin")) {
            pin->SetClass("active", false);
        }
    }


    void DragonBoardRmlUi::SetGallery(
        const std::vector<GalleryPhotoInfo>& photos,
        std::size_t selectedIndex,
        bool captureBusy,
        int captureTimerSeconds,
        int countdownSeconds,
        int gridColumns,
        bool deleteConfirmationPending)
    {
        if (!_galleryDocument) return;
        _galleryPhotos = photos;
        _gallerySelectedIndex = photos.empty() ? 0 : std::min(selectedIndex, photos.size() - 1);
        (void)gridColumns;
        std::string markup;
        for (std::size_t index = 0; index < photos.size(); ++index) {
            const auto& photo = photos[index];
            const auto indexText = std::to_string(index);
            markup += "<div class=\"gallery-card";
            if (index == _gallerySelectedIndex) markup += " selected";
            if (photo.favorite) markup += " favorite";
            markup += "\"><button id=\"gallery-photo-" + indexText +
                "\" class=\"gallery-card-select\"><img class=\"gallery-card-photo\" src=\"" +
                EscapeRml(photo.thumbnailPath.empty() ?
                    photo.imagePath : photo.thumbnailPath) +
                "\" /><div class=\"gallery-card-name\">" + EscapeRml(photo.location) +
                "</div></button><button id=\"gallery-card-favorite-" + indexText +
                "\" class=\"gallery-card-icon gallery-card-favorite" +
                (photo.favorite ? " active" : "") +
                "\"><img src=\"assets/Icons/favoriteIcon.png\" /></button>" +
                "<button id=\"gallery-card-delete-" + indexText +
                "\" class=\"gallery-card-icon gallery-card-delete" +
                (deleteConfirmationPending && index == _gallerySelectedIndex ? " confirm" : "") +
                "\"><img src=\"assets/Icons/trashIcon.png\" /></button></div>";
        }
        if (photos.empty()) markup = "<div class=\"gallery-empty\">No photos captured yet.</div>";
        if (markup != _galleryGridMarkup) {
            _galleryGridMarkup = markup;
            if (auto* grid = _galleryDocument->GetElementById("gallery-grid")) {
                grid->SetInnerRML(markup);
            }
            for (std::size_t index = 0; index < photos.size(); ++index) {
                BindClick(_galleryDocument, ("gallery-photo-" + std::to_string(index)).c_str());
                BindClick(_galleryDocument, ("gallery-card-favorite-" + std::to_string(index)).c_str());
                BindClick(_galleryDocument, ("gallery-card-delete-" + std::to_string(index)).c_str());
            }
        }
        if (auto* status = _galleryDocument->GetElementById("gallery-status")) {
            std::string statusText;
            if (countdownSeconds > 0) {
                statusText = "Snapshot in " + std::to_string(countdownSeconds) + "...";
            } else if (captureBusy) {
                statusText = "Capturing screenshot...";
            } else {
                statusText = std::to_string(photos.size()) + " photos";
            }
            status->SetInnerRML(EscapeRml(statusText));
        }
        const GalleryPhotoInfo* selected = photos.empty() ? nullptr : &photos[_gallerySelectedIndex];
        const auto setText = [&](const char* id, std::string_view value) {
            if (auto* element = _galleryDocument->GetElementById(id)) element->SetInnerRML(EscapeRml(value));
        };
        setText("gallery-name", selected ? selected->name : "No photo selected");
        setText("gallery-location", selected ? selected->location : "Location");
        setText("gallery-date", selected ? (selected->skyrimDate.empty() ? selected->capturedAt : selected->skyrimDate) : "Date");
        if (auto* preview = _galleryDocument->GetElementById("gallery-preview")) {
            preview->SetProperty("display", selected ? "block" : "none");
            if (selected) preview->SetAttribute("src", selected->imagePath);
            else preview->RemoveAttribute("src");
        }
        const auto setButtonText = [&](const char* id, std::string_view value) {
            if (auto* element = _galleryDocument->GetElementById(id)) {
                element->SetInnerRML("<span>" + EscapeRml(value) + "</span>");
            }
        };
        setButtonText("gallery-pin-map", selected && selected->mapPinned ? "REMOVE MARKER" : "MARKER ON MAP");
        setButtonText("gallery-pin-panel", selected && selected->panelPinned ? "UNPIN" : "PIN");
        setButtonText("gallery-favorite", selected && selected->favorite ? "UNFAVORITE" : "FAVORITE");
        setButtonText("gallery-delete", deleteConfirmationPending ? "CONFIRM DELETE" : "DELETE");
        setButtonText("gallery-timer", captureTimerSeconds > 0 ?
            "TIMER " + std::to_string(captureTimerSeconds) + "S" : "TIMER OFF");
        if (auto* timer = _galleryDocument->GetElementById("gallery-timer")) {
            timer->SetClass("active", captureTimerSeconds > 0);
        }
        if (auto* favorite = _galleryDocument->GetElementById("gallery-favorite")) {
            favorite->SetClass("active", selected && selected->favorite);
        }
        if (auto* remove = _galleryDocument->GetElementById("gallery-delete")) {
            remove->SetClass("confirm", deleteConfirmationPending);
        }
    }

    void DragonBoardRmlUi::SetJournal(const JournalInfo& info)
    {
        if (!_journalDocument) return;
        _journalInfo = info;

        const auto setText = [this](const char* id, const std::string& value) {
            if (auto* element = _journalDocument->GetElementById(id)) {
                element->SetInnerRML(EscapeRml(value));
            }
        };
        const auto buildStatRows = [this](
                                           const std::vector<JournalStatInfo>& stats,
                                           std::string_view valueClass = {}) {
            std::string markup;
            for (const auto& stat : stats) {
                markup += "<div class=\"journal-stat-row\"><span class=\"journal-stat-label\">" +
                    EscapeRml(stat.label) + "</span>";
                if (stat.secondaryValue.empty()) {
                    markup += "<span class=\"journal-stat-value" +
                        std::string(valueClass) + "\">" + EscapeRml(stat.value) + "</span>";
                } else {
                    markup += "<span class=\"journal-skill-values\"><span class=\"journal-stat-value journal-skill-level\">" +
                        EscapeRml(stat.value) +
                        "</span><span class=\"journal-skill-experience\">" +
                        EscapeRml(stat.secondaryValue) + "</span></span>";
                }
                markup += "</div>";
            }
            if (markup.empty()) {
                markup = "<div class=\"journal-empty\">" +
                    EscapeRml(Tr("No statistics available.")) + "</div>";
            }
            return markup;
        };

        setText("journal-player-name", info.playerName.empty() ? "Dragonborn" : info.playerName);
        setText("journal-player-level", std::to_string(info.playerLevel));

        const auto questMatchesFilter = [this](const JournalQuestInfo& quest) {
            if (_journalQuestFilter == "all") return true;
            if (_journalQuestFilter == "main") return quest.type == "MAIN QUEST";
            if (_journalQuestFilter == "misc") return quest.type == "MISCELLANEOUS";
            return quest.type != "MAIN QUEST" && quest.type != "MISCELLANEOUS";
        };
        for (const auto* filter : { "all", "main", "side", "misc" }) {
            const std::string id = std::string("journal-filter-") + filter;
            if (auto* button = _journalDocument->GetElementById(id)) {
                button->SetClass("active", _journalQuestFilter == filter);
            }
        }
        std::vector<std::size_t> visibleQuestIndices;
        visibleQuestIndices.reserve(info.quests.size());
        for (std::size_t index = 0; index < info.quests.size(); ++index) {
            if (questMatchesFilter(info.quests[index])) {
                visibleQuestIndices.push_back(index);
            }
        }
        std::size_t selectedQuestIndex = static_cast<std::size_t>(-1);
        if (!visibleQuestIndices.empty()) {
            selectedQuestIndex =
                std::ranges::find(visibleQuestIndices, info.selectedIndex) !=
                        visibleQuestIndices.end() ?
                    info.selectedIndex :
                    visibleQuestIndices.front();
        }

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

        std::string activeQuestTitle = Tr("None");
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
        setText("journal-active-quest", Tr("Active quest:") + " " + activeQuestTitle);

        const auto questButtonId = [](const JournalQuestInfo& quest) {
            return "journal-quest-" + std::to_string(quest.formID) + "-" +
                   std::to_string(quest.instanceID);
        };

        if (auto* list = _journalDocument->GetElementById("journal-quest-list")) {
            std::string markup;
            const auto appendSection = [&](std::string_view title, bool active) {
                bool headingAdded = false;
                for (const auto index : visibleQuestIndices) {
                    const auto& quest = info.quests[index];
                    if (quest.active != active) continue;
                    if (!headingAdded) {
                        markup += "<div class=\"journal-section-label " +
                            std::string(active ? "active-section" : "inactive-section") +
                            "\"><span>" +
                            EscapeRml(title) + "</span></div>";
                        headingAdded = true;
                    }
                    const auto identity =
                        std::to_string(quest.formID) + "-" +
                        std::to_string(quest.instanceID);
                    std::string classes = "journal-quest-button";
                    if (quest.type == "MAIN QUEST") {
                        classes += " quest-category-main";
                    } else if (quest.type == "MISCELLANEOUS") {
                        classes += " quest-category-misc";
                    } else {
                        classes += " quest-category-side";
                    }
                    if (index == selectedQuestIndex) classes += " active";
                    if (quest.completed) classes += " completed";
                    if (quest.failed) classes += " failed";
                    markup += "<button id=\"" + questButtonId(quest) +
                        "\" class=\"" + classes + "\"><i class=\"fade-layer\"></i>"
                        "<span class=\"journal-quest-marker\">" +
                        (quest.active ? "&gt;" : "") +
                        "</span><span id=\"journal-quest-name-" + identity +
                        "\" class=\"journal-quest-title\"><span id=\"journal-quest-name-track-" +
                        identity + "\" class=\"journal-quest-title-track\">" +
                        EscapeRml(quest.title) + "</span></span></button>";
                }
            };
            appendSection(Tr("ACTIVE QUESTS"), true);
            appendSection(Tr("INACTIVE QUESTS"), false);
            if (markup.empty()) {
                markup = "<div class=\"journal-empty\">" +
                    EscapeRml(Tr("No journal quests available.")) + "</div>";
            }
            if (_journalQuestListMarkup != markup) {
                ResetInventoryMarquee();
                _journalQuestListMarkup = markup;
                list->SetInnerRML(markup);
                for (const auto index : visibleQuestIndices) {
                    BindClick(
                        _journalDocument,
                        questButtonId(info.quests[index]).c_str());
                }
            }
        }

        if (visibleQuestIndices.empty()) {
            _journalSelectedFormID = 0;
            _journalSelectedInstanceID = 0;
            setText("journal-quest-type", Tr("QUEST FILTER"));
            setText("journal-quest-title", Tr("No matching quests"));
            setText(
                "journal-quest-summary",
                Tr("No quests are available in the selected category."));
            setText("journal-tracking-state", Tr("NOT TRACKED"));
            if (auto* objectives = _journalDocument->GetElementById("journal-objective-list")) {
                objectives->SetInnerRML(
                    "<div class=\"journal-empty\">" +
                    EscapeRml(Tr("No objectives available.")) + "</div>");
            }
            if (auto* tracking = _journalDocument->GetElementById("journal-toggle-tracking")) {
                tracking->SetClass("disabled", true);
                tracking->SetInnerRML(
                    "<span>" + EscapeRml(Tr("TRACK QUEST")) + "</span>");
            }
        } else {
            const auto& selected = info.quests[selectedQuestIndex];
            _journalSelectedFormID = selected.formID;
            _journalSelectedInstanceID = selected.instanceID;
            setText(
                "journal-quest-type",
                selected.type.empty() ? Tr("QUEST") : Tr(selected.type));
            setText("journal-quest-title", selected.title);
            setText(
                "journal-quest-summary",
                selected.summary.empty() ? Tr("No journal entry is available for this quest.") :
                    selected.summary);
            setText("journal-tracking-state", Tr(selected.active ? "TRACKED" : "TRACK QUEST"));
            if (auto* tracking = _journalDocument->GetElementById("journal-toggle-tracking")) {
                tracking->SetClass("active", selected.active);
                tracking->SetClass("disabled", false);
                tracking->SetInnerRML(
                    "<span>" + EscapeRml(Tr(
                        selected.active ? "UNTRACK QUEST" : "TRACK QUEST")) +
                    "</span>");
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
                        EscapeRml(Tr(objective.state)) +
                        "</span><span class=\"journal-objective-text\">" +
                        EscapeRml(objective.text) +
                        "</span><span class=\"journal-map-marker\">" +
                        (objective.hasTargets ? "&gt;" : "") + "</span></button>";
                }
                if (markup.empty()) {
                    markup = "<div class=\"journal-empty\">" +
                        EscapeRml(Tr("No objectives available.")) + "</div>";
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
            general->SetInnerRML(buildStatRows(info.generalStats, " journal-number-value"));
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
        setText("edit-item-name", info.itemName.empty() ? Tr("Unnamed item") : info.itemName);
        setText("edit-category", info.category.empty() ? Tr("Unknown") : info.category);
        setText("edit-form-id", Rml::CreateString("%08X", info.formID));
        setText("edit-model-path", info.modelPath.empty() ? Tr("No model") : info.modelPath);
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
            Tr(info.boardPinnedToWorld ?
                "Unavailable while the board is world pinned" :
                "Attach this item to the dashboard"));
        if (auto* right = _itemEditDocument->GetElementById("edit-pin-right")) {
            right->SetClass("disabled", !info.canPinToWorld);
        }
        setText("edit-pin-right-state",
            Tr(info.canPinToWorld ?
                "Attach this spell to the right hand" :
                "Requires a valid 3D preview pose"));
        setText("edit-label-state", Tr(info.labelHidden ? "Hidden" : "Visible"));
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
            state->SetInnerRML(EscapeRml(
                Tr(enabled ? "Enabled" : "Disabled")));
        }
    }

    void DragonBoardRmlUi::SetPinsLocked(bool locked)
    {
        if (!_settingsDocument) return;
        if (auto* toggle = _settingsDocument->GetElementById("toggle-lock-pins")) {
            toggle->SetClass("enabled", locked);
        }
        if (auto* state = _settingsDocument->GetElementById("toggle-lock-pins-state")) {
            state->SetInnerRML(EscapeRml(
                Tr(locked ? "Locked" : "Unlocked")));
        }
    }

    void DragonBoardRmlUi::SetPositionAdjustmentActive(bool active)
    {
        if (!_settingsDocument) return;
        if (auto* button =
                _settingsDocument->GetElementById("position-adjustment")) {
            button->SetClass("enabled", active);
        }
        if (auto* state =
                _settingsDocument->GetElementById("position-adjustment-state")) {
            state->SetInnerRML(EscapeRml(
                Tr(active ? "Adjusting..." : "Start adjustment")));
        }
        if (auto* help =
                _settingsDocument->GetElementById("position-adjustment-help")) {
            help->SetInnerRML(EscapeRml(Tr(active ?
                "Laser-hand grip moves the board; hold both grips and move the controllers apart or together to scale" :
                "Grab with the laser hand; hold both grips and move the controllers apart or together to scale")));
        }
        if (auto* status = _settingsDocument->QuerySelector(".save-status")) {
            status->SetInnerRML(EscapeRml(Tr(active ?
                "Adjusting relative to menu hand - select Save changes when finished" :
                "Ready to save")));
        }
    }

    void DragonBoardRmlUi::SetShowTutorialsEnabled(bool enabled)
    {
        if (!_settingsDocument) return;
        if (auto* toggle =
                _settingsDocument->GetElementById("toggle-show-tutorials")) {
            toggle->SetClass("enabled", enabled);
        }
        if (auto* state =
                _settingsDocument->GetElementById("toggle-show-tutorials-state")) {
            state->SetInnerRML(EscapeRml(
                Tr(enabled ? "Enabled" : "Disabled")));
        }
    }

    void DragonBoardRmlUi::SetStatusWidgetEnabled(bool enabled)
    {
        if (!_settingsDocument) return;
        if (auto* toggle =
                _settingsDocument->GetElementById("toggle-status-widget")) {
            toggle->SetClass("enabled", enabled);
        }
        if (auto* state =
                _settingsDocument->GetElementById("toggle-status-widget-state")) {
            state->SetInnerRML(EscapeRml(
                Tr(enabled ? "Enabled" : "Disabled")));
        }
    }

    void DragonBoardRmlUi::SetLanguageSelection(std::string_view code)
    {
        _settingsLanguageCode = LocalizationManager::NormalizeCode(code);
        if (!_settingsDocument) return;
        if (auto* current =
                _settingsDocument->GetElementById("language-current")) {
            current->SetInnerRML(std::string(
                LocalizationManager::NativeName(_settingsLanguageCode)));
        }
    }

    std::string DragonBoardRmlUi::ActiveLanguageCode() const
    {
        return _localization ? _localization->ActiveCode() : "en";
    }

    std::string DragonBoardRmlUi::Tr(std::string_view english) const
    {
        return _localization ?
            _localization->Translate(english) :
            std::string(english);
    }

    void DragonBoardRmlUi::SetWelcomePage(
        std::uint8_t page,
        bool grabCompleted)
    {
        if (!_welcomeDocument) return;
        page = std::clamp<std::uint8_t>(page, 1, 5);
        if (auto* pinTutorial =
                _welcomeDocument->GetElementById("pin-tutorial-page")) {
            pinTutorial->SetClass("active", false);
            pinTutorial->SetProperty("display", "none");
        }
        const auto visiblePage = page <= 2 ? page : static_cast<std::uint8_t>(page - 1);
        for (std::uint8_t candidate = 1; candidate <= 4; ++candidate) {
            const auto id = "welcome-page-" + std::to_string(candidate);
            if (auto* element = _welcomeDocument->GetElementById(id)) {
                const bool active = candidate == visiblePage;
                element->SetClass("active", active);
                element->SetProperty("display", active ? "block" : "none");
            }
        }
        const bool showGrabInstruction = page == 2 && !grabCompleted;
        const bool showGrabResult = page == 2 && grabCompleted;
        const bool showScaleInstruction = page == 3;
        if (auto* pageTwo =
                _welcomeDocument->GetElementById("welcome-page-2")) {
            pageTwo->SetClass("scale-instruction", showScaleInstruction);
        }
        if (auto* instruction =
                _welcomeDocument->GetElementById("welcome-grab-instruction")) {
            instruction->SetClass("completed", grabCompleted);
            instruction->SetProperty("display", showGrabInstruction ? "flex" : "none");
        }
        if (auto* result =
                _welcomeDocument->GetElementById("welcome-grab-result")) {
            result->SetClass("revealed", showGrabResult);
            result->SetProperty("display", showGrabResult ? "flex" : "none");
        }
        if (auto* scale =
                _welcomeDocument->GetElementById("welcome-scale-instruction")) {
            scale->SetClass("revealed", showScaleInstruction);
            scale->SetProperty("display", showScaleInstruction ? "flex" : "none");
        }
        if (auto* next =
                _welcomeDocument->GetElementById("welcome-next-2")) {
            next->SetClass("disabled", page == 2 && !grabCompleted);
        }
    }

    void DragonBoardRmlUi::SetPinTutorial()
    {
        if (!_welcomeDocument) return;
        for (std::uint8_t candidate = 1; candidate <= 4; ++candidate) {
            const auto id = "welcome-page-" + std::to_string(candidate);
            if (auto* element = _welcomeDocument->GetElementById(id)) {
                element->SetClass("active", false);
                element->SetProperty("display", "none");
            }
        }
        if (auto* pinTutorial =
                _welcomeDocument->GetElementById("pin-tutorial-page")) {
            pinTutorial->SetClass("active", true);
            pinTutorial->SetProperty("display", "block");
        }
    }

    void DragonBoardRmlUi::SetWorldPinned(bool pinned)
    {
        if (!_settingsDocument) return;
        if (auto* toggle = _settingsDocument->GetElementById("toggle-world-pin")) {
            toggle->SetClass("enabled", pinned);
        }
        if (auto* state = _settingsDocument->GetElementById("toggle-world-pin-state")) {
            state->SetInnerRML(EscapeRml(Tr(pinned ? "On" : "Off")));
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
                "\" class=\"command-item\"><span>" +
                EscapeRml(_developerCommands[index].label) + "</span></button><br />";
        }
        if (markup.empty()) {
            markup = "<div class=\"empty-state\">" +
                EscapeRml(Tr("No commands configured.")) + "</div>";
        }
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
                "%.2f / %.2f ms",
                stats.averageMs,
                stats.p95Ms);
        };

        setText("dev-fps", Rml::CreateString("%.1f", info.fps));
        setText("dev-frame-time", Rml::CreateString("%.2f ms", info.frameTimeMs));
        setText("dev-update-timing", formatTiming(info.update));
        setText("dev-render-timing", formatTiming(info.render));
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

    void DragonBoardRmlUi::UpdateKeyboardVisuals()
    {
        if (!_keyboardDocument) return;
        if (auto* prompt = _keyboardDocument->GetElementById("keyboard-prompt")) {
            prompt->SetInnerRML(EscapeRml(_keyboardPrompt));
        }
        if (auto* display = _keyboardDocument->GetElementById("keyboard-text")) {
            display->SetClass("empty", _keyboardText.empty());
            display->SetInnerRML(
                EscapeRml(_keyboardText.empty() ?
                    Tr("Tap a key to begin") :
                    _keyboardText));
        }
        if (auto* count = _keyboardDocument->GetElementById("keyboard-count")) {
            count->SetInnerRML(
                std::to_string(_keyboardText.size()) + " / " +
                std::to_string(_keyboardMaximumLength));
        }
        if (auto* shift = _keyboardDocument->GetElementById("keyboard-shift")) {
            shift->SetClass("active", _keyboardShift);
        }
        for (const auto& key : kKeyboardKeys) {
            if (auto* element = _keyboardDocument->GetElementById(key.id)) {
                const char character = _keyboardShift ? key.shifted : key.normal;
                element->SetInnerRML(
                    "<span>" + EscapeRml(std::string(1, character)) + "</span>");
            }
        }
    }

    void DragonBoardRmlUi::CloseKeyboard(bool accepted)
    {
        if (!IsKeyboardOpen()) return;
        _keyboardResult = KeyboardResult{ accepted, accepted ? _keyboardText : std::string{} };
        _keyboardDocument->Hide();
        _activeDocument = nullptr;
        if (_keyboardReturnDocument) {
            _keyboardReturnDocument->Show();
            _activeDocument = _keyboardReturnDocument;
        }
        _keyboardReturnDocument = nullptr;
        _keyboardShift = false;
        logger::info(
            "DragonBoardVR: shared RmlUi keyboard {}.",
            accepted ? "confirmed" : "cancelled");
    }

    void DragonBoardRmlUi::HandleClick(const char* id)
    {
        if (!id) return;
        RequestHaptic(ResolveClickHaptic(id));
        logger::trace("DragonBoardVR: RmlUi click on '{}'.", id);
        const std::string_view value(id);
        if (_activeDocument == _keyboardDocument) {
            if (value == "keyboard-cancel") {
                CloseKeyboard(false);
                return;
            }
            if (value == "keyboard-confirm") {
                CloseKeyboard(true);
                return;
            }
            if (value == "keyboard-clear") {
                _keyboardText.clear();
                UpdateKeyboardVisuals();
                return;
            }
            if (value == "keyboard-backspace") {
                if (!_keyboardText.empty()) {
                    auto offset = _keyboardText.size() - 1;
                    while (offset > 0 &&
                           (static_cast<unsigned char>(_keyboardText[offset]) & 0xC0) == 0x80) {
                        --offset;
                    }
                    _keyboardText.erase(offset);
                    UpdateKeyboardVisuals();
                }
                return;
            }
            if (value == "keyboard-shift") {
                _keyboardShift = !_keyboardShift;
                UpdateKeyboardVisuals();
                return;
            }
            if (value == "keyboard-space") {
                if (_keyboardText.size() < _keyboardMaximumLength) {
                    _keyboardText.push_back(' ');
                    UpdateKeyboardVisuals();
                } else {
                    RequestHaptic(HapticCue::kError);
                }
                return;
            }
            const auto key = std::find_if(
                kKeyboardKeys.begin(),
                kKeyboardKeys.end(),
                [value](const KeyboardKeyDefinition& candidate) {
                    return value == candidate.id;
                });
            if (key != kKeyboardKeys.end()) {
                if (_keyboardText.size() < _keyboardMaximumLength) {
                    _keyboardText.push_back(_keyboardShift ? key->shifted : key->normal);
                    if (_keyboardShift) _keyboardShift = false;
                    UpdateKeyboardVisuals();
                } else {
                    RequestHaptic(HapticCue::kError);
                }
                return;
            }
            return;
        }
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
        if (value.ends_with("-decrease") || value.ends_with("-increase")) {
            const bool increase = value.ends_with("-increase");
            const auto suffixLength = increase ? std::string_view("-increase").size() :
                std::string_view("-decrease").size();
            const std::string sliderId(value.substr(0, value.size() - suffixLength));
            if (auto* slider = _settingsDocument ?
                    _settingsDocument->GetElementById(sliderId) : nullptr) {
                try {
                    const float minimum = std::stof(slider->GetAttribute<Rml::String>("min", "0"));
                    const float maximum = std::stof(slider->GetAttribute<Rml::String>("max", "1"));
                    const float step = std::stof(slider->GetAttribute<Rml::String>("step", "0.01"));
                    const float current = std::stof(slider->GetAttribute<Rml::String>("value", "0"));
                    const float next = std::clamp(current + (increase ? step : -step), minimum, maximum);
                    _synchronizingSliderValues = true;
                    slider->SetAttribute("value", Rml::CreateString("%.6f", next));
                    _synchronizingSliderValues = false;
                    HandleSliderChange(sliderId.c_str(), next);
                } catch (...) {
                    logger::warn("DragonBoardVR: invalid stepper value for '{}'.", sliderId);
                }
            }
        } else if (value == "welcome-close" || value == "pin-tutorial-close") {
            _welcomeCloseRequested = true;
        } else if (value == "welcome-next-1" || value == "welcome-next-2" ||
                   value == "welcome-next-3") {
            _welcomeNextRequested = true;
        } else if (value == "close" || value == "dev-close") {
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
        } else if (value == "edit-pin-right") {
            _itemEditAction = ItemEditAction::kPinRightHand;
        } else if (value == "edit-toggle-label") {
            _itemEditAction = ItemEditAction::kToggleLabel;
        } else if (value == "mods-add") {
            _modsAction = ModsAction::kAdd;
        } else if (value == "mods-close") {
            _modsAction = ModsAction::kClose;
        } else if (value == "mods-tab-actions") {
            SelectModsPage(false);
        } else if (value == "mods-tab-ini") {
            SelectModsPage(true);
        } else if (value == "mods-ini-refresh") {
            _modsAction = ModsAction::kRefreshIni;
        } else if (value == "mods-ini-save") {
            _modsAction = ModsAction::kSaveIni;
        } else if (value == "mods-ini-discard") {
            _modsAction = ModsAction::kDiscardIni;
        } else if (value == "mods-ini-search-clear") {
            _modsAction = ModsAction::kClearIniSearch;
        } else if (value == "mods-ini-search") {
            _modsAction = ModsAction::kSearchIni;
        } else if (value == "mods-ini-show-hidden") {
            _modsAction = ModsAction::kToggleHiddenIniView;
        } else if (value.starts_with("ini-visibility-")) {
            try {
                _modsActionIndex =
                    static_cast<std::size_t>(std::stoul(std::string(value.substr(15))));
                _modsAction = ModsAction::kToggleIniModVisibility;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid INI visibility id '{}'.", id);
            }
        } else if (value.starts_with("ini-toggle-")) {
            try {
                _modsActionIndex =
                    static_cast<std::size_t>(std::stoul(std::string(value.substr(11))));
                _modsAction = ModsAction::kToggleIniValue;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid INI toggle id '{}'.", id);
            }
        } else if (value.starts_with("ini-value-")) {
            try {
                _modsActionIndex =
                    static_cast<std::size_t>(std::stoul(std::string(value.substr(10))));
                _modsAction = ModsAction::kEditIniValue;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid INI value id '{}'.", id);
            }
        } else if (value.starts_with("ini-file-tab-")) {
            try {
                _modsActionIndex =
                    static_cast<std::size_t>(std::stoul(std::string(value.substr(13))));
                _modsAction = ModsAction::kSelectIniFile;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid INI file tab id '{}'.", id);
            }
        } else if (value.starts_with("ini-mod-")) {
            try {
                _modsActionIndex =
                    static_cast<std::size_t>(std::stoul(std::string(value.substr(8))));
                _modsAction = ModsAction::kSelectIniMod;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid INI mod id '{}'.", id);
            }
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
        } else if (value == "inventory-filter-favorites") {
            _inventoryAction = InventoryAction::kFilterFavorites;
        } else if (value == "inventory-filter-weapons") {
            _inventoryAction = InventoryAction::kFilterWeapons;
        } else if (value == "inventory-filter-armor") {
            _inventoryAction = InventoryAction::kFilterArmor;
        } else if (value == "inventory-filter-potions") {
            _inventoryAction = InventoryAction::kFilterPotions;
        } else if (value == "inventory-filter-food") {
            _inventoryAction = InventoryAction::kFilterFood;
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
        } else if (value == "magic-pin-right") {
            _magicAction = MagicAction::kPinRightHand;
        } else if (value == "magic-pin-label") {
            _magicAction = MagicAction::kToggleLabel;
        } else if (value == "magic-close") {
            _magicAction = MagicAction::kClose;
        } else if (value == "magic-search") {
            _magicAction = MagicAction::kSearch;
        } else if (value == "magic-search-clear") {
            _magicAction = MagicAction::kClearSearch;
        } else if (value == "magic-filter-favorites") {
            _magicAction = MagicAction::kFilterFavorites;
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
        } else if (value == "journal-toggle-tracking") {
            _journalActionFormID = _journalSelectedFormID;
            _journalActionInstanceID = _journalSelectedInstanceID;
            _journalAction = JournalAction::kToggleTracking;
        } else if (value == "journal-tab-quests") {
            SelectJournalPage("quests");
        } else if (value == "journal-tab-stats") {
            SelectJournalPage("stats");
        } else if (value == "journal-filter-all" ||
                   value == "journal-filter-main" ||
                   value == "journal-filter-side" ||
                   value == "journal-filter-misc") {
            _journalQuestFilter = std::string(value.substr(15));
            _journalQuestListMarkup.clear();
            const auto snapshot = _journalInfo;
            SetJournal(snapshot);
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
        } else if (value == "gallery-capture") {
            _galleryAction = GalleryAction::kCapture;
        } else if (value == "gallery-timer") {
            _galleryAction = GalleryAction::kCycleTimer;
        } else if (value == "gallery-close") {
            _galleryAction = GalleryAction::kClose;
        } else if (value == "gallery-rename") {
            _galleryAction = GalleryAction::kRename;
        } else if (value == "gallery-favorite") {
            _galleryAction = GalleryAction::kToggleFavorite;
        } else if (value == "gallery-delete") {
            _galleryAction = GalleryAction::kDelete;
        } else if (value == "gallery-pin-map") {
            _galleryAction = GalleryAction::kToggleMapPin;
        } else if (value == "gallery-pin-panel") {
            _galleryAction = GalleryAction::kTogglePanelPin;
        } else if (value.starts_with("gallery-card-favorite-")) {
            try {
                constexpr std::string_view prefix = "gallery-card-favorite-";
                _galleryActionIndex = static_cast<std::size_t>(std::stoull(std::string(value.substr(prefix.size()))));
                _galleryAction = GalleryAction::kToggleFavoriteAt;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid gallery favorite id '{}'.", id);
            }
        } else if (value.starts_with("gallery-card-delete-")) {
            try {
                constexpr std::string_view prefix = "gallery-card-delete-";
                _galleryActionIndex = static_cast<std::size_t>(std::stoull(std::string(value.substr(prefix.size()))));
                _galleryAction = GalleryAction::kDeleteAt;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid gallery delete id '{}'.", id);
            }
        } else if (value.starts_with("gallery-photo-")) {
            try {
                _galleryActionIndex = static_cast<std::size_t>(std::stoull(std::string(value.substr(14))));
                _galleryAction = GalleryAction::kSelect;
            } catch (...) {
                logger::warn("DragonBoardVR: invalid gallery photo id '{}'.", id);
            }
        } else if (value == "save") {
            _saveRequested = true;
        } else if (value == "position-adjustment") {
            _positionAdjustmentRequested = true;
        } else if (value == "toggle-lock-pins") {
            _lockPinsToggleRequested = true;
        } else if (value == "toggle-dev-panel") {
            _developerPanelToggleRequested = true;
        } else if (value == "toggle-show-tutorials") {
            _showTutorialsToggleRequested = true;
        } else if (value == "toggle-status-widget") {
            _statusWidgetToggleRequested = true;
        } else if (value == "language-previous") {
            _settingsLanguageCode =
                LocalizationManager::PreviousCode(_settingsLanguageCode);
            SetLanguageSelection(_settingsLanguageCode);
            _languageSelectionRequested = _settingsLanguageCode;
        } else if (value == "language-next") {
            _settingsLanguageCode =
                LocalizationManager::NextCode(_settingsLanguageCode);
            SetLanguageSelection(_settingsLanguageCode);
            _languageSelectionRequested = _settingsLanguageCode;
        } else if (value == "toggle-world-pin") {
            _worldPinToggleRequested = true;
        } else if (value == "restart-dragonboard") {
            _restartRequested = true;
        } else if (value.starts_with("tutorial-card-")) {
            SelectTutorialPage(id + 14);
        } else if (value.starts_with("tutorial-back-")) {
            SelectTutorialPage(nullptr);
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
        // Grip without trigger is the RmlUi scroll gesture.  Keep hover state
        // and visuals current, but do not vibrate for every row crossed by the
        // smoothed pointer while the list moves underneath it.
        if (_currentGripDown && !_currentTriggerDown) return;
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastHoverHaptic < std::chrono::milliseconds(45)) return;
        _lastHoverHaptic = now;
        RequestHaptic(HapticCue::kHover);
    }

    void DragonBoardRmlUi::HandleSliderChange(const char* id, float value)
    {
        if (_synchronizingSliderValues || !id || !*id) return;
        logger::trace("DragonBoardVR: RmlUi slider '{}' changed to {:.3f}.", id, value);
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
        const bool visualsContext =
            std::string_view(selectedPage) == "visuals" ||
            std::string_view(selectedPage) == "items";
        for (const auto* page : kPages) {
            const bool active = std::string_view(page) == selectedPage;
            const std::string tabId = std::string("tab-") + page;
            const std::string pageId = std::string("page-") + page;
            if (auto* tab = _settingsDocument->GetElementById(tabId)) {
                tab->SetClass("active", active || (std::string_view(page) == "visuals" && visualsContext));
            }
            if (auto* content = _settingsDocument->GetElementById(pageId)) {
                content->SetProperty("display", active ? "block" : "none");
            }
        }
        if (auto* tabs = _settingsDocument->GetElementById("visuals-top-tabs")) {
            tabs->SetProperty("display", visualsContext ? "flex" : "none");
        }
        if (std::string_view(selectedPage) == "tutorials") {
            SelectTutorialPage(nullptr);
        } else if (auto* title =
                       _settingsDocument->GetElementById("tutorial-header-title")) {
            title->SetProperty("display", "none");
        }
    }

    void DragonBoardRmlUi::SelectTutorialPage(const char* selectedPage)
    {
        if (!_settingsDocument) return;
        if (auto* list = _settingsDocument->GetElementById("tutorial-list-view")) {
            list->SetProperty("display", selectedPage ? "none" : "block");
        }
        if (auto* title =
                _settingsDocument->GetElementById("tutorial-header-title")) {
            if (selectedPage) {
                for (std::size_t index = 0; index < kTutorialPages.size(); ++index) {
                    if (std::string_view(kTutorialPages[index]) == selectedPage) {
                        if (auto* text = _settingsDocument->GetElementById(
                                "tutorial-header-title-text")) {
                            text->SetInnerRML(EscapeRml(Tr(kTutorialPageTitles[index])));
                        }
                        break;
                    }
                }
            }
            title->SetProperty("display", selectedPage ? "flex" : "none");
        }
        for (const auto* page : kTutorialPages) {
            if (auto* content = _settingsDocument->GetElementById(
                    std::string("tutorial-page-") + page)) {
                content->SetProperty(
                    "display",
                    selectedPage && std::string_view(page) == selectedPage ? "block" : "none");
            }
        }
        if (auto* scroll = _settingsDocument->GetElementById("page-scroll")) {
            scroll->SetScrollTop(0.0f);
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

    void DragonBoardRmlUi::SelectModsPage(bool iniEditor)
    {
        if (!_modsDocument) return;
        _modsIniEditorSelected = iniEditor;
        if (auto* tab = _modsDocument->GetElementById("mods-tab-actions")) {
            tab->SetClass("active", !iniEditor);
        }
        if (auto* tab = _modsDocument->GetElementById("mods-tab-ini")) {
            tab->SetClass("active", iniEditor);
        }
        if (auto* view = _modsDocument->GetElementById("mods-actions-view")) {
            view->SetProperty("display", iniEditor ? "none" : "block");
        }
        if (auto* view = _modsDocument->GetElementById("mods-ini-view")) {
            view->SetProperty("display", iniEditor ? "block" : "none");
        }
        if (auto* list = _modsDocument->GetElementById("mods-ini-list")) {
            list->SetProperty("display", iniEditor ? "block" : "none");
        }
        if (auto* tools =
                _modsDocument->GetElementById("mods-ini-sidebar-tools")) {
            tools->SetProperty("display", iniEditor ? "block" : "none");
        }
        if (auto* hidden =
                _modsDocument->GetElementById("mods-ini-show-hidden")) {
            hidden->SetProperty("display", iniEditor ? "flex" : "none");
        }
        if (auto* footer = _modsDocument->GetElementById("mods-actions-footer")) {
            footer->SetProperty("display", iniEditor ? "none" : "flex");
        }
        if (auto* footer = _modsDocument->GetElementById("mods-ini-footer")) {
            footer->SetProperty("display", iniEditor ? "block" : "none");
        }
        if (auto* record = _modsDocument->GetElementById("mods-add")) {
            record->SetProperty("display", iniEditor ? "none" : "flex");
        }
        if (auto* save = _modsDocument->GetElementById("mods-ini-save")) {
            save->SetProperty("display", iniEditor ? "flex" : "none");
        }
        if (auto* discard = _modsDocument->GetElementById("mods-ini-discard")) {
            discard->SetProperty("display", iniEditor ? "flex" : "none");
        }
    }

    void DragonBoardRmlUi::SelectJournalPage(const char* selectedPage)
    {
        if (!_journalDocument || !selectedPage) return;
        const bool questsSelected = std::string_view(selectedPage) == "quests";
        if (auto* app = _journalDocument->GetElementById("app")) {
            app->SetClass("journal-statistics-mode", !questsSelected);
        }
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
        if (auto* filters = _journalDocument->GetElementById("journal-quest-filters")) {
            filters->SetProperty("display", questsSelected ? "flex" : "none");
        }
        if (auto* list = _journalDocument->GetElementById("journal-quest-list")) {
            list->SetProperty("display", questsSelected ? "block" : "none");
        }
        if (auto* activeQuest = _journalDocument->QuerySelector(".journal-footer-active")) {
            activeQuest->SetProperty("display", questsSelected ? "flex" : "none");
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
            setText("dev-command-title", Tr("No command selected"));
            setText(
                "dev-command-description",
                Tr("Add commands to DragonBoardVR_DevCommands.ini."));
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
