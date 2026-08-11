#include "pch.h"

#include "ui/rml/RmlJournalPresenter.h"

#include "ui/rml/DragonBoardRmlUi.h"

#include <algorithm>
#include <utility>

namespace dragonboard::ui::rml
{
    RmlJournalPresenter::Selection RmlJournalPresenter::GetSelection(
        bool preserveSelection) const
    {
        std::scoped_lock lock(_mutex);
        return preserveSelection ? _selection : Selection{};
    }

    void RmlJournalPresenter::ReplaceSnapshot(
        std::vector<Quest> quests,
        std::size_t selectedIndex,
        PlayerInfo player,
        std::vector<Stat> characterStats,
        std::vector<Stat> skills,
        std::vector<Stat> generalStats,
        std::uint64_t stateSignature)
    {
        std::scoped_lock lock(_mutex);
        _quests = std::move(quests);
        _selectedIndex = _quests.empty() ? 0 :
            std::min(selectedIndex, _quests.size() - 1);
        _selection = _quests.empty() ? Selection{} : Selection{
            _quests[_selectedIndex].formID,
            _quests[_selectedIndex].instanceID
        };
        _player = std::move(player);
        _characterStats = std::move(characterStats);
        _skills = std::move(skills);
        _generalStats = std::move(generalStats);
        _stateSignature = stateSignature;
    }

    bool RmlJournalPresenter::SelectQuest(
        std::uint32_t formID,
        std::uint32_t instanceID)
    {
        std::scoped_lock lock(_mutex);
        const auto selected = FindQuestLocked(formID, instanceID);
        if (selected == _quests.end()) return false;
        _selectedIndex = static_cast<std::size_t>(
            std::distance(_quests.begin(), selected));
        _selection = { selected->formID, selected->instanceID };
        return true;
    }

    std::optional<bool> RmlJournalPresenter::TrackingState(
        std::uint32_t formID,
        std::uint32_t instanceID) const
    {
        std::scoped_lock lock(_mutex);
        const auto selected = FindQuestLocked(formID, instanceID);
        if (selected == _quests.end()) return std::nullopt;
        return selected->active;
    }

    bool RmlJournalPresenter::HasObjective(
        std::uint32_t formID,
        std::uint32_t instanceID,
        std::uint32_t objectiveInstanceID,
        std::uint16_t objectiveID) const
    {
        std::scoped_lock lock(_mutex);
        const auto selected = FindQuestLocked(formID, instanceID);
        if (selected == _quests.end()) return false;
        return std::ranges::any_of(
            selected->objectives,
            [&](const Objective& objective) {
                return objective.objectiveID == objectiveID &&
                    objective.instanceID == objectiveInstanceID;
            });
    }

    void RmlJournalPresenter::SetTracked(
        std::uint32_t formID,
        std::uint32_t instanceID,
        bool tracked)
    {
        std::scoped_lock lock(_mutex);
        const auto selected = FindQuestLocked(formID, instanceID);
        if (selected != _quests.end()) selected->active = tracked;
    }

    void RmlJournalPresenter::Sync(DragonBoardRmlUi& rmlUi) const
    {
        DragonBoardRmlUi::JournalInfo info;
        {
            std::scoped_lock lock(_mutex);
            info.playerName = _player.name;
            info.playerLevel = _player.level;
            info.selectedIndex = _selectedIndex;
            info.quests.reserve(_quests.size());
            for (const auto& entry : _quests) {
                DragonBoardRmlUi::JournalQuestInfo quest;
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
                    DragonBoardRmlUi::JournalObjectiveInfo objective;
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
                const std::vector<Stat>& source,
                std::vector<DragonBoardRmlUi::JournalStatInfo>& target) {
                target.reserve(source.size());
                for (const auto& entry : source) {
                    target.push_back({ entry.label, entry.value, entry.secondaryValue });
                }
            };
            copyStats(_characterStats, info.characterStats);
            copyStats(_skills, info.skills);
            copyStats(_generalStats, info.generalStats);
        }
        rmlUi.SetJournal(info);
    }

    std::vector<RmlJournalPresenter::Quest>::iterator
    RmlJournalPresenter::FindQuestLocked(
        std::uint32_t formID,
        std::uint32_t instanceID)
    {
        return std::find_if(
            _quests.begin(), _quests.end(),
            [&](const Quest& quest) {
                return quest.formID == formID && quest.instanceID == instanceID;
            });
    }

    std::vector<RmlJournalPresenter::Quest>::const_iterator
    RmlJournalPresenter::FindQuestLocked(
        std::uint32_t formID,
        std::uint32_t instanceID) const
    {
        return std::find_if(
            _quests.cbegin(), _quests.cend(),
            [&](const Quest& quest) {
                return quest.formID == formID && quest.instanceID == instanceID;
            });
    }
}
