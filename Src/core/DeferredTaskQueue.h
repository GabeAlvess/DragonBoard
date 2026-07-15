#pragma once

#include <functional>
#include <vector>

namespace dragonboard::core
{
    class DeferredTaskQueue
    {
    public:
        void Clear();
        void Schedule(float delaySeconds, std::function<void()> callback);
        void Update(float deltaTime);

    private:
        struct Task
        {
            float remainingDelay{ 0.0f };
            std::function<void()> callback;
        };

        std::vector<Task> _tasks;
    };
}
