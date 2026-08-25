#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace yeet17::persistence {

inline constexpr int kSchemaVersion = 1;
inline constexpr std::string_view kAppId = "YEET17PCSET";

struct CustomPackage {
    std::string id;
    std::string name;
};

struct UpdatesBlock {
    std::string mode = "default"; // full | security | pause | default
    int pauseDays = 0;
};

// One document = live config.json = export = user/builtin preset.
struct Preset {
    int schemaVersion = kSchemaVersion;
    std::string app{kAppId};
    std::string name;
    std::string createdAt;
    std::string theme = "system"; // system | dark | light
    std::vector<std::string> packages;
    std::map<std::string, bool> tweaks;
    std::map<std::string, bool> features;
    UpdatesBlock updates;
    std::vector<CustomPackage> customPackages;
    nlohmann::json extra = nlohmann::json::object(); // unknown keys, never dropped

    static Preset FromJson(const nlohmann::json& json);
    [[nodiscard]] nlohmann::json ToJson() const;

    [[nodiscard]] std::vector<std::string> EnabledTweakIds() const;
};

} // namespace yeet17::persistence
