#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace yeet17::core {

enum class ThemeMode { System, Dark, Light };

struct AppSettings {
    ThemeMode theme = ThemeMode::System;
    bool createRestorePoint = true;
    bool confirmDangerous = true;
    std::string lastPreset = "standard";
    std::string updatePolicy = "full";

    [[nodiscard]] nlohmann::json ToJson() const;
    static AppSettings FromJson(const nlohmann::json& json);
};

// Persisted next to the logger: %LOCALAPPDATA%/YEET17PCSET/settings.json
class Settings {
public:
    static Settings& Instance();

    void Load();
    void Save() const;

    [[nodiscard]] AppSettings& Current() { return current_; }
    [[nodiscard]] const AppSettings& Current() const { return current_; }
    [[nodiscard]] std::filesystem::path Path() const;

private:
    Settings() = default;
    AppSettings current_{};
};

std::string ToString(ThemeMode mode);
std::optional<ThemeMode> ThemeModeFromString(std::string_view value);

} // namespace yeet17::core
