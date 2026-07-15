#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <format>
#include <iostream>
#include <string_view>
#include <utility>

namespace dragonboard::tools::preview_log
{
    template <class... Args>
    void Write(const char* level, std::string_view pattern, Args&&... args)
    {
        try {
            std::cerr << '[' << level << "] "
                      << std::vformat(pattern, std::make_format_args(args...)) << '\n';
        } catch (...) {
            std::cerr << '[' << level << "] " << pattern << '\n';
        }
    }

    template <class... Args>
    void info(std::string_view pattern, Args&&... args)
    {
        Write("info", pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void warn(std::string_view pattern, Args&&... args)
    {
        Write("warn", pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void error(std::string_view pattern, Args&&... args)
    {
        Write("error", pattern, std::forward<Args>(args)...);
    }

    template <class... Args>
    void debug(std::string_view pattern, Args&&... args)
    {
        Write("debug", pattern, std::forward<Args>(args)...);
    }
}

namespace logger = dragonboard::tools::preview_log;
