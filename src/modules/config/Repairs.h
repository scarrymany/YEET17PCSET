#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace yeet17::config {

class Repairs {
public:
    using LogFn = std::function<void(std::string_view)>;

    bool ResetNetwork(LogFn log, std::string& error);
    bool ResetWindowsUpdate(LogFn log, std::string& error);
    bool RepairSystemImage(LogFn log, std::string& error);
    bool RepairWinget(LogFn log, std::string& error);
};

} // namespace yeet17::config
