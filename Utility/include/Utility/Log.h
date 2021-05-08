// Copyright (C) 2020-2021 Michael Kazakov. Subject to GNU General Public License version 3.
#pragma once

#include <Habanero/SpdlogFacade.h>
#include <string>

namespace nc::utility {

struct Log : base::SpdlogFacade<Log> {
    Log() = delete;
    
    static const std::string &Name() noexcept;
    static spdlog::logger &Get() noexcept;
    static void Set(std::shared_ptr<spdlog::logger> _logger) noexcept;
};

} // namespace nc::utility
