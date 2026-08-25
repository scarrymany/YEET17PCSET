#include "core/Paths.h"
#include "core/Logger.h"

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

#include <cstdlib>
#include <system_error>
#include <vector>

namespace yeet17::core {

std::filesystem::path Paths::AppDataDir() {
    return AppDataDirectory();
}

std::filesystem::path Paths::LogsDir() {
    auto dir = AppDataDirectory() / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path Paths::SettingsPath() {
    return AppDataDirectory() / "settings.json";
}

std::filesystem::path Paths::ExeDir() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    DWORD written = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (written == 0) {
        return std::filesystem::current_path();
    }
    if (written >= buf.size()) {
        buf.resize(32768, L'\0');
        written = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (written == 0) {
            return std::filesystem::current_path();
        }
    }
    buf.resize(written);
    return std::filesystem::path{buf}.parent_path();
#else
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return std::filesystem::current_path();
    }
    return exe.parent_path();
#endif
}

std::filesystem::path Paths::CatalogDir() {
    return ExeDir() / "catalog";
}

std::filesystem::path Paths::PresetsDir() {
    // Bundled presets shipped next to the exe. Persist owns
    // %LOCALAPPDATA%/YEET17PCSET/presets for user Save As.
    return ExeDir() / "presets";
}

std::filesystem::path Paths::ResourcesDir() {
    return ExeDir() / "resources";
}

} // namespace yeet17::core
