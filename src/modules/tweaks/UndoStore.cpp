#include "pch.h"
#include "modules/tweaks/UndoStore.h"
#include "core/Logger.h"

#include <cstdlib>

namespace yeet17::tweaks {
namespace {

const char* HiveStr(Hive hive) {
    return hive == Hive::Hkcu ? "HKCU" : "HKLM";
}

const char* StartStr(StartType t) {
    switch (t) {
    case StartType::Disabled: return "disabled";
    case StartType::Manual:   return "manual";
    case StartType::Auto:     return "auto";
    }
    return "manual";
}

} // namespace

std::filesystem::path UndoStore::DefaultPath() {
#ifdef _WIN32
    return yeet17::core::AppDataDirectory() / "undo.json";
#else
    if (const char* env = std::getenv("LOCALAPPDATA"); env && *env) {
        return std::filesystem::path{env} / "YEET17PCSET" / "undo.json";
    }
    return std::filesystem::path{"/tmp/yeet17-undo.json"};
#endif
}

nlohmann::json& UndoStore::TweaksObj() {
    if (!raw_.contains("tweaks") || !raw_["tweaks"].is_object()) {
        raw_["tweaks"] = nlohmann::json::object();
    }
    return raw_["tweaks"];
}

nlohmann::json& UndoStore::OpsArray(std::string_view tweakId) {
    auto& slot = TweaksObj()[std::string{tweakId}];
    if (!slot.contains("ops") || !slot["ops"].is_array()) {
        slot["ops"] = nlohmann::json::array();
    }
    return slot["ops"];
}

void UndoStore::Clear(std::string_view tweakId) {
    TweaksObj().erase(std::string{tweakId});
}

void UndoStore::PutRegistry(std::string_view tweakId, Hive hive, std::string key, std::string name,
                            std::optional<nlohmann::json> previousOrNulloptIfMissing) {
    nlohmann::json e{
        {"kind", "registry"},
        {"hive", HiveStr(hive)},
        {"key", std::move(key)},
        {"name", std::move(name)},
        {"previous", nullptr},
    };
    if (previousOrNulloptIfMissing) {
        e["previous"] = *previousOrNulloptIfMissing;
    }
    OpsArray(tweakId).push_back(std::move(e));
}

void UndoStore::PutService(std::string_view tweakId, std::string service, StartType previous,
                           bool wasRunning) {
    OpsArray(tweakId).push_back({
        {"kind", "service"},
        {"name", std::move(service)},
        {"previous", StartStr(previous)},
        {"wasRunning", wasRunning},
    });
}

void UndoStore::PutTask(std::string_view tweakId, std::string path, bool wasEnabled) {
    OpsArray(tweakId).push_back({
        {"kind", "task"},
        {"path", std::move(path)},
        {"wasEnabled", wasEnabled},
    });
}

bool UndoStore::CanUndo(std::string_view tweakId) const {
    if (!raw_.contains("tweaks") || !raw_["tweaks"].is_object()) {
        return false;
    }
    const auto& tweaks = raw_["tweaks"];
    const auto it = tweaks.find(std::string{tweakId});
    if (it == tweaks.end() || !it->contains("ops") || !(*it)["ops"].is_array()) {
        return false;
    }
    return !(*it)["ops"].empty();
}

bool UndoStore::Load(const std::filesystem::path& path) {
    raw_ = {{"version", 1}, {"tweaks", nlohmann::json::object()}};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return true;
    }
    try {
        std::ifstream in{path};
        if (!in) {
            return false;
        }
        nlohmann::json parsed;
        in >> parsed;
        if (!parsed.is_object()) {
            return false;
        }
        raw_ = std::move(parsed);
        if (!raw_.contains("tweaks") || !raw_["tweaks"].is_object()) {
            raw_["tweaks"] = nlohmann::json::object();
        }
        raw_["version"] = raw_.value("version", 1);
        return true;
    } catch (const std::exception& ex) {
        spdlog::warn("UndoStore::Load: {}", ex.what());
        raw_ = {{"version", 1}, {"tweaks", nlohmann::json::object()}};
        return false;
    }
}

bool UndoStore::Save(const std::filesystem::path& path) const {
    try {
        std::error_code ec;
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        std::ofstream out{path};
        if (!out) {
            return false;
        }
        out << raw_.dump(2);
        out << '\n';
        return static_cast<bool>(out);
    } catch (const std::exception& ex) {
        spdlog::warn("UndoStore::Save: {}", ex.what());
        return false;
    }
}

const nlohmann::json& UndoStore::Raw() const {
    return raw_;
}

} // namespace yeet17::tweaks
