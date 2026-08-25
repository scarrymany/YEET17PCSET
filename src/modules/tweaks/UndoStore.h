#pragma once

#include "modules/tweaks/Tweak.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace yeet17::tweaks {

class UndoStore {
public:
    void Clear(std::string_view tweakId);
    void PutRegistry(std::string_view tweakId, Hive hive, std::string key, std::string name,
                     std::optional<nlohmann::json> previousOrNulloptIfMissing);
    void PutService(std::string_view tweakId, std::string service, StartType previous, bool wasRunning);
    void PutTask(std::string_view tweakId, std::string path, bool wasEnabled);
    [[nodiscard]] bool CanUndo(std::string_view tweakId) const;

    // %LOCALAPPDATA%/YEET17PCSET/undo.json on Windows;
    // /tmp/yeet17-undo.json (or LOCALAPPDATA) on Linux.
    bool Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;
    [[nodiscard]] const nlohmann::json& Raw() const;

    static std::filesystem::path DefaultPath();

private:
    nlohmann::json& TweaksObj();
    nlohmann::json& OpsArray(std::string_view tweakId);

    nlohmann::json raw_{{"version", 1}, {"tweaks", nlohmann::json::object()}};
};

} // namespace yeet17::tweaks
