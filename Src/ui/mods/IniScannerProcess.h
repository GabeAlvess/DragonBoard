#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace dragonboard::ui::mods
{
    class IniScannerProcess
    {
    public:
        enum class State : std::uint8_t
        {
            kIdle,
            kRunning,
            kSucceeded,
            kFailed
        };

        struct Completion
        {
            State state = State::kIdle;
            unsigned long exitCode = 0;
            std::string message;
        };

        IniScannerProcess() = default;
        ~IniScannerProcess();
        IniScannerProcess(const IniScannerProcess&) = delete;
        IniScannerProcess& operator=(const IniScannerProcess&) = delete;

        bool Start(
            const std::filesystem::path& executable,
            const std::filesystem::path& output,
            std::string& error);
        [[nodiscard]] State GetState() const { return _state.load(std::memory_order_acquire); }
        std::optional<Completion> ConsumeCompletion();

    private:
        void Run(
            std::stop_token stopToken,
            std::filesystem::path executable,
            std::filesystem::path output);

        std::atomic<State> _state{ State::kIdle };
        std::jthread _worker;
        std::mutex _completionMutex;
        std::optional<Completion> _completion;
    };
}
