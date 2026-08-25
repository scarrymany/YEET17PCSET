#pragma once

namespace yeet17::app {

class Elevation {
public:
    [[nodiscard]] static bool IsRunAsAdmin();

    // If not elevated, relaunches YEET17PCSET.exe via ShellExecuteW "runas"
    // (UAC). Returns true when the current process is already admin.
    // Returns false if the user cancelled UAC or relaunch failed.
    static bool EnsureAdministrator();

    // Used by App after a successful relaunch to exit the unelevated instance.
    [[nodiscard]] static bool RelaunchRequested() { return relaunchRequested_; }

private:
    static inline bool relaunchRequested_ = false;
};

} // namespace yeet17::app
