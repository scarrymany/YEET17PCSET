#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace yeet17::updates {

// Application self-update from GitHub Releases.
//
// The release asset is a zip of the whole install layout (exe, catalog/,
// presets/, resources/, src/ xbf tree, bootstrap DLL): the app cannot run
// from a bare exe, so updating means unpacking the zip over the install dir.
// LaunchUpdater writes a detached PowerShell script that waits for this
// process to exit, unpacks the zip and restarts the app.
//
// All HTTP calls are blocking (WinHTTP) - run them on a worker thread.

struct ReleaseInfo {
    std::string tag;      // "v1.2.0" as published
    std::string version;  // "1.2.0" (tag without the leading 'v')
    std::string assetUrl; // browser_download_url of the update zip
    std::string assetName;
    std::uint64_t assetSize = 0;
};

class SelfUpdate {
public:
    // Plain github.com redirects, not api.github.com: the REST API allows only
    // 60 anonymous requests per hour per IP, which silently breaks the update
    // check for users behind shared/carrier-grade NAT addresses.
    static constexpr std::wstring_view kReleaseHost = L"github.com";
    static constexpr std::wstring_view kLatestReleasePath =
        L"/scarrymany/YEET17PCSET/releases/latest";
    static constexpr std::wstring_view kTagPathMarker = L"/releases/tag/";
    static constexpr std::string_view kAssetFileName = "YEET17PCSET-win-x64.zip";

    static SelfUpdate& Instance();

    // Latest release, or nullopt when the installed version is up to date.
    [[nodiscard]] std::expected<std::optional<ReleaseInfo>, std::string>
    CheckForUpdate(std::string_view currentVersion) const;

    // Downloads the release zip into %TEMP%; returns the zip path.
    [[nodiscard]] std::expected<std::filesystem::path, std::string> DownloadAsset(
        const ReleaseInfo& release,
        const std::function<void(std::uint64_t received, std::uint64_t total)>& progress) const;

    // Writes and starts the detached updater. The caller must exit the app
    // right after this returns success.
    [[nodiscard]] std::expected<void, std::string>
    LaunchUpdater(const std::filesystem::path& zipPath) const;

    // True when `remote` (x.y.z) is strictly newer than `local`.
    [[nodiscard]] static bool IsNewer(std::string_view remote, std::string_view local);

private:
    SelfUpdate() = default;
    SelfUpdate(const SelfUpdate&) = delete;
    SelfUpdate& operator=(const SelfUpdate&) = delete;
};

} // namespace yeet17::updates
