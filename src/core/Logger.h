#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace spdlog {
class logger;
}

namespace yeet17::core {

// App data lives under %LOCALAPPDATA%/YEET17PCSET so an unpackaged exe does
// not write next to Program Files (which is not writable even when elevated
// if the user later copies the binary there).
std::filesystem::path AppDataDirectory();

class Logger {
public:
    static Logger& Instance();

    void Initialize();
    void Info(std::string_view message);
    void Warn(std::string_view message);
    void Error(std::string_view message);
    void Debug(std::string_view message);

    [[nodiscard]] std::filesystem::path LogDirectory() const;
    [[nodiscard]] std::shared_ptr<spdlog::logger> Handle() const { return logger_; }

private:
    Logger() = default;
    std::shared_ptr<spdlog::logger> logger_;
    std::filesystem::path logDir_;
};

} // namespace yeet17::core
