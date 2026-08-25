#include "pch.h"
#include "core/Settings.h"
#include "core/Logger.h"

namespace yeet17::core {

std::string ToString(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Dark:  return "dark";
    case ThemeMode::Light: return "light";
    default:               return "system";
    }
}

std::optional<ThemeMode> ThemeModeFromString(std::string_view value) {
    if (value == "dark")   return ThemeMode::Dark;
    if (value == "light")  return ThemeMode::Light;
    if (value == "system") return ThemeMode::System;
    return std::nullopt;
}

nlohmann::json AppSettings::ToJson() const {
    return nlohmann::json{
        {"theme", ToString(theme)},
        {"createRestorePoint", createRestorePoint},
        {"confirmDangerous", confirmDangerous},
        {"lastPreset", lastPreset},
        {"updatePolicy", updatePolicy},
    };
}

AppSettings AppSettings::FromJson(const nlohmann::json& json) {
    AppSettings s;
    if (json.contains("theme")) {
        if (auto parsed = ThemeModeFromString(json["theme"].get<std::string>())) {
            s.theme = *parsed;
        }
    }
    s.createRestorePoint = json.value("createRestorePoint", true);
    s.confirmDangerous   = json.value("confirmDangerous", true);
    s.lastPreset         = json.value("lastPreset", "standard");
    s.updatePolicy       = json.value("updatePolicy", "full");
    return s;
}

Settings& Settings::Instance() {
    static Settings instance;
    return instance;
}

std::filesystem::path Settings::Path() const {
    return AppDataDirectory() / "settings.json";
}

void Settings::Load() {
    const auto path = Path();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        current_ = {};
        return;
    }
    try {
        std::ifstream in{path};
        nlohmann::json json;
        in >> json;
        current_ = AppSettings::FromJson(json);
        Logger::Instance().Info("Настройки загружены");
    } catch (const std::exception& ex) {
        Logger::Instance().Warn(std::string{"Не удалось прочитать настройки: "} + ex.what());
        current_ = {};
    }
}

void Settings::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(AppDataDirectory(), ec);
    std::ofstream out{Path()};
    out << Current().ToJson().dump(2);
    Logger::Instance().Info("Настройки сохранены");
}

} // namespace yeet17::core
