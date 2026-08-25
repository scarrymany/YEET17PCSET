#pragma once

#include "persistence/Preset.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yeet17::persistence {

class ConfigStore {
public:
    static ConfigStore& Instance();

    [[nodiscard]] std::filesystem::path ConfigPath() const;
    [[nodiscard]] std::filesystem::path BundledPresetsDirectory() const;
    [[nodiscard]] std::filesystem::path UserPresetsDirectory() const;
    // Bundled next to the exe (POST_BUILD copy). Kept for existing callers.
    [[nodiscard]] std::filesystem::path PresetsDirectory() const { return BundledPresetsDirectory(); }

    static bool Validate(const nlohmann::json& json);
    static std::optional<Preset> Parse(const nlohmann::json& json);

    bool Save(const Preset& preset) const;
    std::optional<Preset> Load() const;

    bool Export(const std::filesystem::path& path, const Preset& preset) const;
    std::optional<Preset> Import(const std::filesystem::path& path) const;

    std::optional<Preset> LoadPreset(std::string_view id) const;
    std::vector<Preset> LoadBundledPresets() const;
    std::vector<Preset> ListUserPresets() const;
    bool SaveUserPreset(std::string_view id, const Preset& preset) const;

private:
    ConfigStore() = default;

    bool WriteJson(const std::filesystem::path& path, const nlohmann::json& json) const;
    std::optional<nlohmann::json> ReadJson(const std::filesystem::path& path) const;
    std::optional<Preset> LoadPresetFrom(const std::filesystem::path& dir, std::string_view id) const;
};

} // namespace yeet17::persistence
