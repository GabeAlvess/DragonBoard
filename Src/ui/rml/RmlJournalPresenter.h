#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace dragonboard::ui::rml
{
    class DragonBoardRmlUi;

    class RmlJournalPresenter
    {
    public:
        struct Objective
        {
            std::uint16_t objectiveID = 0;
            std::uint32_t instanceID = 0;
            std::string text;
            std::string state;
            bool completed = false;
            bool failed = false;
            bool hasTargets = false;
        };

        struct Quest
        {
            std::uint32_t formID = 0;
            std::uint32_t instanceID = 0;
            std::string title;
            std::string summary;
            std::string type;
            bool active = false;
            bool completed = false;
            bool failed = false;
            std::vector<Objective> objectives;
        };

        struct Stat
        {
            std::string label;
            std::string value;
        };

        struct PlayerInfo
        {
            std::string name;
            std::uint16_t level = 1;
        };

        struct Selection
        {
            std::uint32_t formID = 0;
            std::uint32_t instanceID = 0;
        };

        [[nodiscard]] Selection GetSelection(bool preserveSelection) const;
        void ReplaceSnapshot(
            std::vector<Quest> quests,
            std::size_t selectedIndex,
            PlayerInfo player,
            std::vector<Stat> characterStats,
            std::vector<Stat> skills,
            std::vector<Stat> generalStats,
            std::uint64_t stateSignature);
        [[nodiscard]] bool SelectQuest(
            std::uint32_t formID,
            std::uint32_t instanceID);
        [[nodiscard]] std::optional<bool> TrackingState(
            std::uint32_t formID,
            std::uint32_t instanceID) const;
        [[nodiscard]] bool HasObjective(
            std::uint32_t formID,
            std::uint32_t instanceID,
            std::uint32_t objectiveInstanceID,
            std::uint16_t objectiveID) const;
        void SetTracked(
            std::uint32_t formID,
            std::uint32_t instanceID,
            bool tracked);
        void Sync(DragonBoardRmlUi& rmlUi) const;

    private:
        [[nodiscard]] std::vector<Quest>::iterator FindQuestLocked(
            std::uint32_t formID,
            std::uint32_t instanceID);
        [[nodiscard]] std::vector<Quest>::const_iterator FindQuestLocked(
            std::uint32_t formID,
            std::uint32_t instanceID) const;

        mutable std::mutex _mutex;
        std::vector<Quest> _quests;
        std::vector<Stat> _characterStats;
        std::vector<Stat> _skills;
        std::vector<Stat> _generalStats;
        std::size_t _selectedIndex = 0;
        Selection _selection;
        PlayerInfo _player;
        std::uint64_t _stateSignature = 0;
    };
}
