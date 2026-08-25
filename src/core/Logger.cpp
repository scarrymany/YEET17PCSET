#include "pch.h"
#include "core/Logger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef _WIN32
#    include <shlobj.h>
#endif

namespace yeet17::core {
namespace {

std::filesystem::path ResolveLocalAppData() {
#ifdef _WIN32
    // SHGetKnownFolderPath is the supported way to resolve LOCALAPPDATA
    // under elevation (GetEnvironmentVariable can point at the admin profile).
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        std::filesystem::path path{raw};
        CoTaskMemFree(raw);
        return path;
    }
#endif
    if (const char* env = std::getenv("LOCALAPPDATA"); env && *env) {
        return std::filesystem::path{env};
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path{home} / ".local" / "share";
    }
    return std::filesystem::temp_directory_path();
}

} // namespace

std::filesystem::path AppDataDirectory() {
    return ResolveLocalAppData() / "YEET17PCSET";
}

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Initialize() {
    if (logger_) {
        return;
    }

    logDir_ = AppDataDirectory() / "logs";
    std::error_code ec;
    std::filesystem::create_directories(logDir_, ec);

    const auto file = (logDir_ / "yeet17.log").string();
    auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(file, 5 * 1024 * 1024, 3);
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    logger_ = std::make_shared<spdlog::logger>("yeet17", spdlog::sinks_init_list{rotating, console});
    logger_->set_level(spdlog::level::debug);
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    logger_->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger_);
    Info("Журнал инициализирован");
}

void Logger::Info(std::string_view message) {
    if (logger_) logger_->info("{}", message);
}

void Logger::Warn(std::string_view message) {
    if (logger_) logger_->warn("{}", message);
}

void Logger::Error(std::string_view message) {
    if (logger_) logger_->error("{}", message);
}

void Logger::Debug(std::string_view message) {
    if (logger_) logger_->debug("{}", message);
}

std::filesystem::path Logger::LogDirectory() const {
    return logDir_;
}

} // namespace yeet17::core
