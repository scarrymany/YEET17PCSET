#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace yeet17::updates {

// Windows Update policy for YEET17PCSET.
//
// We only write documented AU / UX pause keys. We never stop wuauserv or BITS
// (that is WinUtil "disable", not pause) and we never hide the Settings page.
//
// Preset / config JSON (the persist module nests this under "updates"):
//   {
//     "mode": "full" | "security" | "pause" | "default",
//     "pauseDays": 7
//   }
// State::ToJson / FromJson read and write that object. FromJson also accepts
// a wrapper { "updates": { ... } } so a full config file can be passed in.

enum class Mode {
    Full,          // receive quality + feature updates, no deferral
    SecurityOnly,  // recommended-off + driver exclude + WinUtil deferrals
    Pause,         // official UX pause (PauseUpdates + PauseUpdatesExpiryTime)
    Default        // delete managed values, restore healthy service startup
};

struct State {
    Mode mode = Mode::Full;
    int pauseDays = 7; // clamped to 1..35 on Apply

    [[nodiscard]] nlohmann::json ToJson() const;
    [[nodiscard]] static State FromJson(const nlohmann::json& json);
};

class UpdatePolicy {
public:
    static UpdatePolicy& Instance();

    // Detect from HKLM (or the in-memory map on non-Windows).
    [[nodiscard]] State Read() const;

    // Snapshot every value we are about to change, then mutate.
    // On success: settings.updatePolicy is set to ToId(next.mode) and saved.
    // On failure: `error` is a Russian string for the UI log; we do not claim success.
    bool Apply(State next, std::string& error);
    bool Undo(std::string& error);
    bool ResetToDefault(std::string& error);

    [[nodiscard]] static std::string_view ToId(Mode mode); // full|security|pause|default
    [[nodiscard]] static std::optional<Mode> FromId(std::string_view id);

private:
    UpdatePolicy() = default;
    UpdatePolicy(const UpdatePolicy&) = delete;
    UpdatePolicy& operator=(const UpdatePolicy&) = delete;
};

} // namespace yeet17::updates
