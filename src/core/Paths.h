#pragma once

#include <filesystem>

namespace yeet17::core {

// Path helpers for the unpackaged layout.
// App data: %LOCALAPPDATA%/YEET17PCSET
// Sidecar dirs (catalog/presets/resources) live next to YEET17PCSET.exe
// (CMake POST_BUILD copies them there). User-saved presets are Persist's
// zone — do not invent ConfigPath here.
struct Paths {
    static std::filesystem::path AppDataDir();
    static std::filesystem::path LogsDir();
    static std::filesystem::path SettingsPath();
    static std::filesystem::path ExeDir();
    static std::filesystem::path CatalogDir();
    static std::filesystem::path PresetsDir();
    static std::filesystem::path ResourcesDir();
};

} // namespace yeet17::core
