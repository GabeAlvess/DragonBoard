#include "core/DeferredTaskQueue.h"

#include <cstddef>
#include <utility>

namespace dragonboard::core
{
    void DeferredTaskQueue::Clear()
    {
        _tasks.clear();
    }

    void DeferredTaskQueue::Schedule(float delaySeconds, std::function<void()> callback)
    {
        if (!callback) {
            return;
        }

        Task task;
        task.remainingDelay = delaySeconds > 0.0f ? delaySeconds : 0.0f;
        task.callback = std::move(callback);
        _tasks.push_back(std::move(task));
    }

    void DeferredTaskQueue::Update(float deltaTime)
    {
        for (std::size_t index = 0; index < _tasks.size();) {
            auto& task = _tasks[index];
            task.remainingDelay -= deltaTime;
            if (task.remainingDelay > 0.0f) {
                ++index;
                continue;
            }

            auto callback = std::move(task.callback);
            _tasks.erase(_tasks.begin() + static_cast<std::ptrdiff_t>(index));
            if (callback) {
                callback();
            }
        }
    }
}
