#pragma once

#include "modules/install/Package.h"

#include <atomic>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yeet17::install {

enum class PackageAction { Install, Upgrade, Uninstall };

struct ProgressEvent {
    std::string packageId;
    PackageAction action;
    int percent = -1;          // 0-100 or -1 unknown
    std::string line;          // latest stdout/stderr line (UTF-8)
    bool finished = false;
    bool success = false;
    int exitCode = -1;
};

using ProgressCallback = std::function<void(const ProgressEvent&)>;

struct InstalledPackage {
    std::string id;
    std::string name;
    std::string version;
    std::string available;     // empty if up to date / unknown
};

struct PackageJob {
    PackageAction action{};
    std::string id;
    std::string source{"winget"};
};

// ---------------------------------------------------------------------------
// TODO(com): Microsoft.Management.Deployment (WinRT / COM)
// Следующий слой — in-proc клиент Package Manager. Сейчас НЕ реализован:
// на хосте нет проекции C++/WinRT для этого API. Запасной путь остаётся CLI.
// Нельзя вызывать COM и нельзя сообщать об успехе без кода выхода == 0.
// ---------------------------------------------------------------------------
class WingetComClient; // заглушка следующего слоя, без методов

class WingetClient {
public:
    WingetClient();
    ~WingetClient();

    WingetClient(const WingetClient&) = delete;
    WingetClient& operator=(const WingetClient&) = delete;

    [[nodiscard]] bool Available() const;          // winget.exe found
    [[nodiscard]] std::filesystem::path Executable() const;
    [[nodiscard]] bool ChocolateyAvailable() const;

    // Single package. Success ONLY if exit code == 0.
    std::expected<void, std::string> Install(std::string_view id, ProgressCallback onProgress = {});
    std::expected<void, std::string> Upgrade(std::string_view id, ProgressCallback onProgress = {});
    std::expected<void, std::string> Uninstall(std::string_view id, ProgressCallback onProgress = {});

    // Overloads that honour Package.source ("winget" | "choco").
    std::expected<void, std::string> Install(std::string_view id, std::string_view source,
                                             ProgressCallback onProgress = {});
    std::expected<void, std::string> Upgrade(std::string_view id, std::string_view source,
                                             ProgressCallback onProgress = {});
    std::expected<void, std::string> Uninstall(std::string_view id, std::string_view source,
                                               ProgressCallback onProgress = {});

    // Sequential bulk. Continues after a failure; returns error if ANY failed.
    // Reports per-package ProgressEvent. Do not parallelize winget (it locks sources).
    std::expected<void, std::string> RunBulk(
        const std::vector<std::pair<PackageAction, std::string>>& jobs,
        ProgressCallback onProgress = {});
    std::expected<void, std::string> RunBulk(
        const std::vector<PackageJob>& jobs,
        ProgressCallback onProgress = {});

    std::expected<std::vector<InstalledPackage>, std::string> ListInstalled();
    std::expected<std::vector<Package>, std::string> SearchRemote(std::string_view query);

    // Maps `winget list` stdout onto catalog ids only. Unknown rows are ignored.
    static std::vector<std::string> ParseListedIds(std::string_view stdoutText,
                                                   const std::vector<Package>& catalog);

    void Cancel(); // kill current child
    [[nodiscard]] bool Busy() const;

private:
    std::expected<void, std::string> RunAction(PackageAction action, std::string_view id,
                                               std::string_view source, ProgressCallback onProgress,
                                               bool takeLock);

#ifdef _WIN32
    struct ProcessOutcome {
        bool started = false;
        int exitCode = -1;
        std::string output;
    };

    ProcessOutcome Launch(const std::filesystem::path& exe, const std::vector<std::wstring>& args,
                          std::string_view packageId, PackageAction action,
                          const ProgressCallback& onProgress);
    void AttachChild(void* process);
    void DetachChild();
#endif

    std::filesystem::path wingetExe_;
    std::filesystem::path chocoExe_;
    std::mutex mutex_;
    std::atomic<bool> busy_{false};
#ifdef _WIN32
    void* childProcess_{nullptr};
    std::mutex childMutex_;
#endif
};

} // namespace yeet17::install
