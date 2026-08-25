#pragma once

#include "modules/tweaks/Tweak.h"
#include "modules/tweaks/UndoStore.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yeet17::tweaks {

struct ApplyOptions {
    bool confirmed{false};           // must be true if any tweak requiresConfirm
    bool createRestorePoint{true};   // one RP for ApplyMany / preset
};

class TweakEngine {
public:
    TweakEngine();

    std::expected<void, std::string> Load(const std::filesystem::path& catalogJson);
    [[nodiscard]] const std::vector<Tweak>& All() const noexcept;
    [[nodiscard]] const Tweak* Find(std::string_view id) const;
    [[nodiscard]] std::vector<const Tweak*> ByTier(Tier tier) const;
    [[nodiscard]] std::vector<const Tweak*> ByPreset(std::string_view presetId) const;

    [[nodiscard]] bool IsApplied(std::string_view id) const;
    std::expected<void, std::string> Apply(std::string_view id, const ApplyOptions& opt);
    std::expected<void, std::string> ApplyMany(std::span<const std::string> ids, const ApplyOptions& opt);
    // Apply every id whose flag is true (TweaksPage / preset maps).
    std::expected<void, std::string> ApplyEnabled(const std::unordered_map<std::string, bool>& enabled,
                                                 const ApplyOptions& opt);
    std::expected<void, std::string> Undo(std::string_view id);
    [[nodiscard]] UndoStore& Undo() noexcept;

private:
    void PersistUndo() const;
    std::expected<void, std::string> ApplyOp(std::string_view tweakId, const Op& op);
    std::expected<void, std::string> UndoEntry(const nlohmann::json& entry);
    [[nodiscard]] bool OpMatches(const Op& op) const;

    [[nodiscard]] std::optional<nlohmann::json> QueryRegistry(Hive hive, const std::string& key,
                                                              const std::string& name) const;
    std::expected<void, std::string> WriteRegistry(Hive hive, const std::string& key, const std::string& name,
                                                   RegType type, const nlohmann::json& value, bool createKey);
    std::expected<void, std::string> DeleteRegistry(Hive hive, const std::string& key, const std::string& name);

    [[nodiscard]] std::optional<std::pair<StartType, bool>> QueryServiceState(const std::string& name) const;
    std::expected<void, std::string> WriteService(const std::string& name, StartType start, bool stop);
    std::expected<void, std::string> StartServiceIfNeeded(const std::string& name);

    [[nodiscard]] std::optional<bool> QueryTaskEnabled(const std::string& path) const;
    std::expected<void, std::string> WriteTaskEnabled(const std::string& path, bool enabled);

    std::vector<Tweak> tweaks_;
    UndoStore undo_;
    std::filesystem::path undoPath_;
    std::unordered_map<std::string, nlohmann::json> memReg_;
    std::unordered_map<std::string, std::pair<StartType, bool>> memSvc_;
    std::unordered_map<std::string, bool> memTask_;
};

} // namespace yeet17::tweaks
