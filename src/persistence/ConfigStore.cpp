#include "pch.h"
#include "persistence/ConfigStore.h"
#include "core/Logger.h"

#include <ctime>

#ifdef _WIN32
#    include <windows.h>
#endif

namespace yeet17::persistence {
namespace {

constexpr std::string_view kKnown[] = {
    "schemaVersion", "app", "name", "createdAt", "theme",
    "packages", "tweaks", "features", "updates", "customPackages",
};

std::filesystem::path ExeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::filesystem::path{buf}.parent_path();
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::string NowUtcIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool ValidTheme(std::string_view value) {
    return value == "system" || value == "dark" || value == "light";
}

bool ValidUpdateMode(std::string_view value) {
    return value == "full" || value == "security" || value == "pause" || value == "default";
}

std::map<std::string, bool> MapFromJson(const nlohmann::json& node) {
    std::map<std::string, bool> out;
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.value().is_boolean()) out[it.key()] = it.value().get<bool>();
        }
    } else if (node.is_array()) {
        for (const auto& item : node) {
            if (item.is_string()) out[item.get<std::string>()] = true;
        }
    }
    return out;
}

nlohmann::json MapToJson(const std::map<std::string, bool>& map) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& [id, on] : map) obj[id] = on;
    return obj;
}

} // namespace

Preset Preset::FromJson(const nlohmann::json& json) {
    Preset p;
    if (!json.is_object()) return p;
    p.extra = json;
    for (const auto key : kKnown) p.extra.erase(key);

    p.schemaVersion = json.value("schemaVersion", 0);
    p.app = json.value("app", "");
    p.name = json.value("name", "");
    p.createdAt = json.value("createdAt", "");
    p.theme = json.value("theme", "system");
    if (!ValidTheme(p.theme)) p.theme = "system";
    p.packages = json.value("packages", std::vector<std::string>{});
    if (json.contains("tweaks")) p.tweaks = MapFromJson(json["tweaks"]);
    if (json.contains("features")) p.features = MapFromJson(json["features"]);
    if (json.contains("updates") && json["updates"].is_object()) {
        p.updates.mode = json["updates"].value("mode", "default");
        p.updates.pauseDays = json["updates"].value("pauseDays", 0);
        if (!ValidUpdateMode(p.updates.mode)) p.updates.mode = "default";
        if (p.updates.pauseDays < 0) p.updates.pauseDays = 0;
    }
    if (json.contains("customPackages") && json["customPackages"].is_array()) {
        for (const auto& item : json["customPackages"]) {
            if (!item.is_object()) continue;
            CustomPackage pkg;
            pkg.id = item.value("id", "");
            pkg.name = item.value("name", pkg.id);
            if (!pkg.id.empty()) p.customPackages.push_back(std::move(pkg));
        }
    }
    return p;
}

nlohmann::json Preset::ToJson() const {
    nlohmann::json json = extra.is_object() ? extra : nlohmann::json::object();
    json["schemaVersion"] = schemaVersion;
    json["app"] = app;
    json["name"] = name;
    json["createdAt"] = createdAt;
    json["theme"] = theme;
    json["packages"] = packages;
    json["tweaks"] = MapToJson(tweaks);
    json["features"] = MapToJson(features);
    json["updates"] = nlohmann::json{{"mode", updates.mode}, {"pauseDays", updates.pauseDays}};
    nlohmann::json custom = nlohmann::json::array();
    for (const auto& pkg : customPackages) {
        custom.push_back(nlohmann::json{{"id", pkg.id}, {"name", pkg.name}});
    }
    json["customPackages"] = std::move(custom);
    return json;
}

std::vector<std::string> Preset::EnabledTweakIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, on] : tweaks) {
        if (on) ids.push_back(id);
    }
    return ids;
}

ConfigStore& ConfigStore::Instance() {
    static ConfigStore instance;
    return instance;
}

std::filesystem::path ConfigStore::ConfigPath() const {
    return yeet17::core::AppDataDirectory() / "config.json";
}

std::filesystem::path ConfigStore::BundledPresetsDirectory() const {
    return ExeDir() / "presets";
}

std::filesystem::path ConfigStore::UserPresetsDirectory() const {
    return yeet17::core::AppDataDirectory() / "presets";
}

bool ConfigStore::Validate(const nlohmann::json& json) {
    if (!json.is_object()) return false;
    if (!json.contains("schemaVersion") || !json["schemaVersion"].is_number_integer()) return false;
    if (json["schemaVersion"].get<int>() != kSchemaVersion) return false;
    if (!json.contains("app") || !json["app"].is_string()) return false;
    return json["app"].get<std::string>() == kAppId;
}

std::optional<Preset> ConfigStore::Parse(const nlohmann::json& json) {
    if (!Validate(json)) return std::nullopt;
    return Preset::FromJson(json);
}

bool ConfigStore::WriteJson(const std::filesystem::path& path, const nlohmann::json& json) const {
    try {
        std::error_code ec;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        const auto tmp = path.native() + std::filesystem::path{".tmp"}.native();
        {
            std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
            if (!out) return false;
            out << json.dump(2);
            if (!out) return false;
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(tmp, path, ec);
            if (ec) {
                std::filesystem::remove(tmp, ec);
                return false;
            }
        }
        return true;
    } catch (const std::exception& ex) {
        yeet17::core::Logger::Instance().Error(std::string{"Запись JSON: "} + ex.what());
        return false;
    }
}

std::optional<nlohmann::json> ConfigStore::ReadJson(const std::filesystem::path& path) const {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::nullopt;
    try {
        std::ifstream in{path, std::ios::binary};
        if (!in) return std::nullopt;
        nlohmann::json json;
        in >> json;
        return json;
    } catch (const std::exception& ex) {
        yeet17::core::Logger::Instance().Error(std::string{"Чтение JSON: "} + ex.what());
        return std::nullopt;
    }
}

bool ConfigStore::Save(const Preset& preset) const {
    Preset copy = preset;
    if (copy.app.empty()) copy.app = std::string{kAppId};
    copy.schemaVersion = kSchemaVersion;
    if (copy.createdAt.empty()) copy.createdAt = NowUtcIso8601();
    if (!WriteJson(ConfigPath(), copy.ToJson())) {
        yeet17::core::Logger::Instance().Error("Не удалось сохранить конфиг");
        return false;
    }
    yeet17::core::Logger::Instance().Info("Конфиг сохранён");
    return true;
}

std::optional<Preset> ConfigStore::Load() const {
    const auto json = ReadJson(ConfigPath());
    if (!json) return std::nullopt;
    auto parsed = Parse(*json);
    if (!parsed) {
        yeet17::core::Logger::Instance().Error("Конфиг не прошёл проверку schemaVersion/app");
        return std::nullopt;
    }
    yeet17::core::Logger::Instance().Info("Конфиг загружен");
    return parsed;
}

bool ConfigStore::Export(const std::filesystem::path& path, const Preset& preset) const {
    Preset copy = preset;
    if (copy.app.empty()) copy.app = std::string{kAppId};
    copy.schemaVersion = kSchemaVersion;
    if (copy.createdAt.empty()) copy.createdAt = NowUtcIso8601();
    if (!WriteJson(path, copy.ToJson())) {
        yeet17::core::Logger::Instance().Error("Экспорт пресета не удался");
        return false;
    }
    yeet17::core::Logger::Instance().Info("Пресет экспортирован");
    return true;
}

std::optional<Preset> ConfigStore::Import(const std::filesystem::path& path) const {
    const auto json = ReadJson(path);
    if (!json) return std::nullopt;
    auto parsed = Parse(*json);
    if (!parsed) {
        yeet17::core::Logger::Instance().Error("Импорт: файл не YEET17PCSET schema v1");
        return std::nullopt;
    }
    yeet17::core::Logger::Instance().Info("Пресет импортирован");
    return parsed;
}

std::optional<Preset> ConfigStore::LoadPresetFrom(const std::filesystem::path& dir,
                                                 std::string_view id) const {
    const auto path = dir / (std::string{id} + ".json");
    const auto json = ReadJson(path);
    if (!json) return std::nullopt;
    return Parse(*json);
}

std::optional<Preset> ConfigStore::LoadPreset(std::string_view id) const {
    if (auto bundled = LoadPresetFrom(BundledPresetsDirectory(), id)) return bundled;
    return LoadPresetFrom(UserPresetsDirectory(), id);
}

std::vector<Preset> ConfigStore::LoadBundledPresets() const {
    std::vector<Preset> out;
    for (const char* id : {"minimal", "standard", "maximal"}) {
        if (auto p = LoadPresetFrom(BundledPresetsDirectory(), id)) {
            if (p->name.empty()) p->name = id;
            out.push_back(*p);
        }
    }
    return out;
}

std::vector<Preset> ConfigStore::ListUserPresets() const {
    std::vector<Preset> out;
    std::error_code ec;
    const auto dir = UserPresetsDirectory();
    if (!std::filesystem::exists(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        const auto json = ReadJson(entry.path());
        if (!json) continue;
        if (auto parsed = Parse(*json)) {
            if (parsed->name.empty()) parsed->name = entry.path().stem().string();
            out.push_back(*parsed);
        }
    }
    return out;
}

bool ConfigStore::SaveUserPreset(std::string_view id, const Preset& preset) const {
    if (id.empty()) return false;
    Preset copy = preset;
    if (copy.app.empty()) copy.app = std::string{kAppId};
    copy.schemaVersion = kSchemaVersion;
    if (copy.createdAt.empty()) copy.createdAt = NowUtcIso8601();
    if (copy.name.empty()) copy.name = std::string{id};
    const auto path = UserPresetsDirectory() / (std::string{id} + ".json");
    if (!WriteJson(path, copy.ToJson())) {
        yeet17::core::Logger::Instance().Error("Не удалось сохранить пользовательский пресет");
        return false;
    }
    yeet17::core::Logger::Instance().Info("Пользовательский пресет сохранён");
    return true;
}

} // namespace yeet17::persistence
