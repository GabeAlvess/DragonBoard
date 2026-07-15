#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <RE/T/TESEquipEvent.h>
#include <RE/T/TESSpellCastEvent.h>
#include <RE/B/BSTEvent.h>

namespace vrui {

    struct ModAction {
        std::string label;
        std::string iconPath;
        std::string command;
    };

    class ModActionManager : public RE::BSTEventSink<RE::TESEquipEvent>,
                             public RE::BSTEventSink<RE::TESSpellCastEvent>
    {
    public:
        static ModActionManager& get() {
            static ModActionManager instance;
            return instance;
        }

        void initialize();
        void loadActions();
        void saveActions();

        std::vector<ModAction> getActions() const;
        void addAction(const ModAction& action);
        void removeAction(size_t index);

        // Activates the interactive listening mode
        void startListening();

        // Called each frame from VRMenuManager or VRFrameUpdater
        void update(float deltaTime);

        bool isListening() const { return _isListening; }

    protected:
        RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event, RE::BSTEventSource<RE::TESEquipEvent>* eventSource) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* event, RE::BSTEventSource<RE::TESSpellCastEvent>* eventSource) override;

    private:
        ModActionManager() = default;
        ~ModActionManager() = default;

        std::vector<ModAction> _actions;
        mutable std::mutex _mutex;

        bool _isListening = false;
        float _listenTimer = 0.0f;
    };

}
