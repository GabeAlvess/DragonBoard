#include "ModActionManager.h"
#include "game/actions/ActionExecutor.h"
#include <RE/S/ScriptEventSourceHolder.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/S/SpellItem.h>
#include <RE/T/TESShout.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/A/AlchemyItem.h>
#include <RE/S/ScrollItem.h>
#include <RE/T/TESFullName.h>
#include <RE/U/UI.h>
#include "vrui/VRMenuManager.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <format>
#include <thread>
#include <atomic>

namespace vrui {
    namespace {
        std::atomic<std::uint64_t> s_saveGeneration{0};
        std::mutex s_saveIOMutex;

        std::string normalizeModActionIconPath(const std::string& iconPath)
        {
            if (iconPath == "words\\wordofpower.nif") {
                return "magic\\ward.nif";
            }
            return iconPath;
        }
    }

    void ModActionManager::initialize()
    {
        logger::trace("DragonBoardVR: ModActionManager initializing...");
        bool needsMigrationSave = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _actions.clear();

            std::filesystem::path iniPath = "Data/SKSE/Plugins/DragonBoardVR_Mods.ini";
            if (!std::filesystem::exists(iniPath)) {
                logger::trace("DragonBoardVR: No DragonBoardVR_Mods.ini found loosely. A new one will be created when actions are added.");
            } else {
                std::ifstream file(iniPath);
                if (file.is_open()) {
                    std::string line;
                    while (std::getline(file, line)) {
                        if (line.empty() || line[0] == ';' || line[0] == '[') continue;
                        
                        size_t eqPos = line.find('=');
                        if (eqPos == std::string::npos) continue;

                        std::string valStr = line.substr(eqPos + 1);
                        size_t firstNonSpace = valStr.find_first_not_of(" \t");
                        if (firstNonSpace != std::string::npos) valStr = valStr.substr(firstNonSpace);

                        size_t firstPipe = valStr.find('|');
                        if (firstPipe == std::string::npos) continue;

                        size_t secondPipe = valStr.find('|', firstPipe + 1);
                        if (secondPipe == std::string::npos) continue;

                        ModAction action;
                        action.label = valStr.substr(0, firstPipe);
                        action.iconPath = valStr.substr(firstPipe + 1, secondPipe - firstPipe - 1);
                        action.command = valStr.substr(secondPipe + 1);
                        const auto normalizedIconPath = normalizeModActionIconPath(action.iconPath);
                        if (normalizedIconPath != action.iconPath) {
                            action.iconPath = normalizedIconPath;
                            needsMigrationSave = true;
                        }
                        
                        _actions.push_back(action);
                    }
                    logger::trace("DragonBoardVR: Loaded {} mod actions from INI.", _actions.size());
                }
            }

            if (needsMigrationSave) {
                logger::trace("DragonBoardVR: Migrating legacy mod action icon paths in INI.");
                saveActions();
            }
        }

        if (auto* scriptEventSource = RE::ScriptEventSourceHolder::GetSingleton()) {
            scriptEventSource->GetEventSource<RE::TESEquipEvent>()->AddEventSink(this);
            scriptEventSource->GetEventSource<RE::TESSpellCastEvent>()->AddEventSink(this);
            logger::trace("DragonBoardVR: ModActionManager registered for TESEquipEvent and TESSpellCastEvent.");
        }
    }

    void ModActionManager::startListening()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (_isListening) return;

        _isListening = true;
        _listenTimer = 10.0f;
        // RE::DebugNotification("DragonBoardVR: Listening for 10s. Equip a spell or item...");
        logger::trace("DragonBoardVR: ModActionManager started listening for actions.");
    }

    void ModActionManager::update(float deltaTime)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_isListening) return;

        _listenTimer -= deltaTime;
        if (_listenTimer <= 0.0f) {
            _isListening = false;
            // RE::DebugNotification("DragonBoardVR: Stopped listening. No action recorded.");
            logger::trace("DragonBoardVR: ModActionManager stopped listening (timeout).");
        }
    }

    RE::BSEventNotifyControl ModActionManager::ProcessEvent(const RE::TESSpellCastEvent* event, [[maybe_unused]] RE::BSTEventSource<RE::TESSpellCastEvent>* eventSource)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (!_isListening) return RE::BSEventNotifyControl::kContinue;
        if (!event) return RE::BSEventNotifyControl::kContinue;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || event->object.get() != player) return RE::BSEventNotifyControl::kContinue;

        auto* form = RE::TESForm::LookupByID(event->spell);
        if (!form) return RE::BSEventNotifyControl::kContinue;

        std::string formName = "Unknown Spell/Power";
        if (auto* fullName = form->As<RE::TESFullName>()) {
            formName = fullName->GetFullName();
        }

        std::string icon = "magic\\fire.nif"; 
        std::string command = "";

        std::string formIdHex = std::format("{:08X}", form->GetFormID());

        bool isPower = false;
        if (form->Is(RE::FormType::Spell)) {
            auto spell = form->As<RE::SpellItem>();
            auto t = spell->GetSpellType();
            if (t == RE::MagicSystem::SpellType::kPower || 
                t == RE::MagicSystem::SpellType::kLesserPower || 
                t == RE::MagicSystem::SpellType::kVoicePower) {
                isPower = true;
            }
        }

        if (form->Is(RE::FormType::Shout) || isPower) {
            command = dragonboard::game::actions::MakeCastPower(form->GetFormID());
            icon = "magic\\ward.nif";
        } else if (form->Is(RE::FormType::Spell)) {
            command = dragonboard::game::actions::MakeCastPower(form->GetFormID());
            icon = "magic\\fire.nif"; 
        } else {
            command = dragonboard::game::actions::MakeCastPower(form->GetFormID());
        }

        ModAction newAction;
        newAction.label = formName;
        newAction.iconPath = icon;
        newAction.command = command;

        _actions.push_back(newAction);
        // RE::DebugNotification((std::string("DragonBoardVR: Recorded cast action -> ") + formName).c_str());
        logger::trace("DragonBoardVR: ModActionManager recorded cast action for {} ({})", formName, formIdHex);

        _isListening = false;
        
        saveActions(); 

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ModActionManager::ProcessEvent(const RE::TESEquipEvent* event, [[maybe_unused]] RE::BSTEventSource<RE::TESEquipEvent>* eventSource)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (!_isListening) return RE::BSEventNotifyControl::kContinue;
        if (!event) return RE::BSEventNotifyControl::kContinue;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || event->actor.get() != player) return RE::BSEventNotifyControl::kContinue;

        if (!event->equipped) return RE::BSEventNotifyControl::kContinue;

        auto* form = RE::TESForm::LookupByID(event->baseObject);
        if (!form) return RE::BSEventNotifyControl::kContinue;

        std::string formName = "Unknown Item";
        if (auto* fullName = form->As<RE::TESFullName>()) {
            formName = fullName->GetFullName();
        }
        if (formName.empty()) formName = "Recorded Item";

        std::string icon = "DragonBoardVR\\IconPlane.nif"; 
        std::string command = "";

        std::string formIdHex = std::format("{:08X}", form->GetFormID());

        bool isPower = false;
        if (form->Is(RE::FormType::Spell)) {
            auto spell = form->As<RE::SpellItem>();
            auto t = spell->GetSpellType();
            if (t == RE::MagicSystem::SpellType::kPower || 
                t == RE::MagicSystem::SpellType::kLesserPower || 
                t == RE::MagicSystem::SpellType::kVoicePower) {
                isPower = true;
            }
        }

        if (form->Is(RE::FormType::Shout) || isPower) {
            command = dragonboard::game::actions::MakeCastPower(form->GetFormID());
            icon = "magic\\ward.nif";
        } else if (form->Is(RE::FormType::Spell)) {
            command = dragonboard::game::actions::MakeCastPower(form->GetFormID());
            icon = "magic\\fire.nif"; 
        } else {
            command = dragonboard::game::actions::MakeEquipItem(form->GetFormID());
            if (form->IsWeapon()) icon = "weapons\\iron\\ironlongsword.nif";
            else if (form->IsArmor()) icon = "armor\\iron\\ironarmor.nif";
            else if (form->Is(RE::FormType::AlchemyItem)) icon = "clutter\\potions\\potionhealth.nif";
            else if (form->Is(RE::FormType::Scroll)) icon = "clutter\\books\\scroll01.nif";
        }

        ModAction newAction;
        newAction.label = formName;
        newAction.iconPath = icon;
        newAction.command = command;

        _actions.push_back(newAction);
        // RE::DebugNotification((std::string("DragonBoardVR: Recorded action -> ") + formName).c_str());
        logger::trace("DragonBoardVR: ModActionManager recorded action for {} ({})", formName, formIdHex);

        _isListening = false;
        
        saveActions(); // Uses the internal save that doesn't re-lock

        return RE::BSEventNotifyControl::kContinue;
    }

    void ModActionManager::loadActions()
    {
        // Must be called with lock held or handled inside initialization
    }

    void ModActionManager::saveActions()
    {
        // Warning: This internal saveActions expects _mutex to be locked by the caller!
        std::vector<ModAction> tempCopy = _actions;  // copy string data while safely locked
        const auto saveGeneration = ++s_saveGeneration;

        // Dispatch heavy file I/O to a background thread to prevent stutters.
        // Use generation + mutex so stale snapshots do not overwrite newer saves.
        std::thread([tempCopy = std::move(tempCopy), saveGeneration]() {
            std::lock_guard<std::mutex> ioLock(s_saveIOMutex);

            const auto latestGeneration = s_saveGeneration.load();
            if (saveGeneration != latestGeneration) {
                logger::trace("DragonBoardVR: Skipping stale mod-action save (gen {} < {}).", saveGeneration, latestGeneration);
                return;
            }

            std::filesystem::path iniPath = "Data/SKSE/Plugins/DragonBoardVR_Mods.ini";
            std::ofstream file(iniPath, std::ios::trunc);
            if (!file.is_open()) {
                logger::error("DragonBoardVR: Failed to open DragonBoardVR_Mods.ini for writing.");
                return;
            }

            file << "[ModSlots]\n";
            file << "; Format: ActionNumber = Label|IconPath|Command\n\n";

            for (size_t i = 0; i < tempCopy.size(); ++i) {
                file << "Action" << i << " = " 
                     << tempCopy[i].label << "|" 
                     << tempCopy[i].iconPath << "|" 
                     << tempCopy[i].command << "\n";
            }
            
            logger::trace("DragonBoardVR: Saved {} mod actions to INI in background.", tempCopy.size());
        }).detach();
    }

    std::vector<ModAction> ModActionManager::getActions() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _actions;
    }

    void ModActionManager::addAction(const ModAction& action)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _actions.push_back(action);
        saveActions();
    }

    void ModActionManager::removeAction(size_t index)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (index < _actions.size()) {
            _actions.erase(_actions.begin() + index);
            saveActions();
        }
    }
}
