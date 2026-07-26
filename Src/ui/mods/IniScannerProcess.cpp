#include "ui/mods/IniScannerProcess.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <vector>

namespace dragonboard::ui::mods
{
    namespace
    {
        constexpr auto kScannerTimeout = std::chrono::seconds(120);

        std::wstring QuoteArgument(const std::filesystem::path& path)
        {
            std::wstring result = L"\"";
            std::size_t backslashes = 0;
            for (const wchar_t character : path.wstring()) {
                if (character == L'\\') {
                    ++backslashes;
                } else {
                    if (character == L'"') {
                        result.append(backslashes * 2 + 1, L'\\');
                    } else {
                        result.append(backslashes, L'\\');
                    }
                    backslashes = 0;
                    result.push_back(character);
                }
            }
            result.append(backslashes * 2, L'\\');
            result.push_back(L'"');
            return result;
        }

        std::string ReadPipe(HANDLE pipe)
        {
            std::string output;
            std::array<char, 4096> buffer{};
            DWORD available = 0;
            while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available) {
                DWORD read = 0;
                if (!ReadFile(
                        pipe,
                        buffer.data(),
                        static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available)),
                        &read,
                        nullptr) ||
                    read == 0) {
                    break;
                }
                output.append(buffer.data(), read);
            }
            while (!output.empty() &&
                   (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) {
                output.pop_back();
            }
            return output;
        }
    }

    IniScannerProcess::~IniScannerProcess()
    {
        if (_worker.joinable()) {
            _worker.request_stop();
            _worker.join();
        }
    }

    bool IniScannerProcess::Start(
        const std::filesystem::path& executable,
        const std::filesystem::path& output,
        std::string& error)
    {
        if (_state.load(std::memory_order_acquire) == State::kRunning) {
            error = "An INI scan is already running.";
            return false;
        }
        if (!std::filesystem::is_regular_file(executable)) {
            error = "INI scanner executable was not found: " + executable.string();
            return false;
        }
        if (_worker.joinable()) _worker.join();
        {
            std::scoped_lock lock(_completionMutex);
            _completion.reset();
        }
        _state.store(State::kRunning, std::memory_order_release);
        _worker = std::jthread(
            [this, executable, output](std::stop_token token) {
                Run(token, executable, output);
            });
        error.clear();
        return true;
    }

    std::optional<IniScannerProcess::Completion> IniScannerProcess::ConsumeCompletion()
    {
        std::scoped_lock lock(_completionMutex);
        auto result = std::move(_completion);
        _completion.reset();
        if (result) _state.store(State::kIdle, std::memory_order_release);
        return result;
    }

    void IniScannerProcess::Run(
        std::stop_token stopToken,
        std::filesystem::path executable,
        std::filesystem::path output)
    {
        Completion completion;
        completion.state = State::kFailed;

        SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
            completion.message = std::format(
                "Could not create scanner output pipe (Win32 {}).", GetLastError());
        } else {
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startup.wShowWindow = SW_HIDE;
            startup.hStdOutput = writePipe;
            startup.hStdError = writePipe;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

            PROCESS_INFORMATION process{};
            auto command = QuoteArgument(executable) + L" scan --output " +
                QuoteArgument(output);
            std::vector<wchar_t> mutableCommand(command.begin(), command.end());
            mutableCommand.push_back(L'\0');
            const auto workingDirectory = executable.parent_path();
            const BOOL created = CreateProcessW(
                executable.c_str(),
                mutableCommand.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                workingDirectory.c_str(),
                &startup,
                &process);
            CloseHandle(writePipe);
            writePipe = nullptr;

            if (!created) {
                completion.message = std::format(
                    "Could not start INI scanner (Win32 {}).", GetLastError());
            } else {
                const auto started = std::chrono::steady_clock::now();
                DWORD waitResult = WAIT_TIMEOUT;
                while (!stopToken.stop_requested() &&
                       std::chrono::steady_clock::now() - started < kScannerTimeout) {
                    waitResult = WaitForSingleObject(process.hProcess, 100);
                    if (waitResult != WAIT_TIMEOUT) break;
                }
                if (waitResult == WAIT_TIMEOUT) {
                    TerminateProcess(process.hProcess, 2);
                    WaitForSingleObject(process.hProcess, 5000);
                    completion.message = stopToken.stop_requested() ?
                        "INI scan was cancelled." :
                        "INI scanner timed out after 120 seconds.";
                } else if (waitResult == WAIT_OBJECT_0) {
                    DWORD exitCode = 1;
                    GetExitCodeProcess(process.hProcess, &exitCode);
                    completion.exitCode = exitCode;
                    completion.message = ReadPipe(readPipe);
                    if (exitCode == 0) {
                        completion.state = State::kSucceeded;
                        if (completion.message.empty()) {
                            completion.message = "INI catalog refreshed.";
                        }
                    } else if (completion.message.empty()) {
                        completion.message =
                            std::format("INI scanner exited with code {}.", exitCode);
                    }
                } else {
                    completion.message = std::format(
                        "Could not wait for INI scanner (Win32 {}).", GetLastError());
                }
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
        }

        if (writePipe) CloseHandle(writePipe);
        if (readPipe) CloseHandle(readPipe);
        {
            std::scoped_lock lock(_completionMutex);
            _completion = completion;
        }
        _state.store(completion.state, std::memory_order_release);
    }
}
