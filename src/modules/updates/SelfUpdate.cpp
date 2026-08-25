#include "pch.h"
#include "modules/updates/SelfUpdate.h"
#include "core/Logger.h"
#include "core/Paths.h"
#include "core/Utf8.h"

#include <array>
#include <charconv>
#include <fstream>

#ifdef _WIN32
#    include <winhttp.h>
#endif

namespace yeet17::updates {

namespace {

constexpr std::wstring_view kUserAgent = L"YEET17PCSET-Updater";
constexpr DWORD kHttpTimeoutMs = 15000;
constexpr std::uint64_t kMaxAssetSize = 512ull * 1024 * 1024;

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

std::optional<Version> ParseVersion(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
        text.remove_prefix(1);
    }
    Version out;
    std::array<int*, 3> parts{&out.major, &out.minor, &out.patch};
    size_t begin = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const size_t end = text.find('.', begin);
        const auto piece = text.substr(begin, end == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : end - begin);
        if (piece.empty()) return std::nullopt;
        const auto res = std::from_chars(piece.data(), piece.data() + piece.size(), *parts[i]);
        if (res.ec != std::errc{}) return std::nullopt;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return out;
}

#ifdef _WIN32

struct HInternetGuard {
    HINTERNET handle = nullptr;
    ~HInternetGuard() {
        if (handle) ::WinHttpCloseHandle(handle);
    }
    explicit operator bool() const { return handle != nullptr; }
};

struct UrlParts {
    std::wstring host;
    std::wstring path;
};

std::optional<UrlParts> CrackUrl(const std::wstring& url) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!::WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        return std::nullopt;
    }
    UrlParts out;
    out.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    out.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength > 0) {
        out.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    return out;
}

// One HTTPS GET. Body goes either to `memory` or to `file` (exactly one),
// with optional progress reporting against Content-Length.
std::expected<void, std::string> HttpGet(
    const std::wstring& host, const std::wstring& path,
    std::string* memory, std::ofstream* file,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    HInternetGuard session{::WinHttpOpen(kUserAgent.data(),
                                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) {
        return std::unexpected("Не удалось открыть сетевую сессию WinHTTP");
    }
    ::WinHttpSetTimeouts(session.handle, kHttpTimeoutMs, kHttpTimeoutMs,
                         kHttpTimeoutMs, kHttpTimeoutMs);

    HInternetGuard connection{::WinHttpConnect(session.handle, host.c_str(),
                                               INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection) {
        return std::unexpected("Не удалось подключиться к " + core::WideToUtf8(host));
    }

    HInternetGuard request{::WinHttpOpenRequest(
        connection.handle, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request) {
        return std::unexpected("Не удалось сформировать HTTPS-запрос");
    }

    const std::wstring headers = L"Accept: application/vnd.github+json\r\n"
                                 L"X-GitHub-Api-Version: 2022-11-28";
    if (!::WinHttpSendRequest(request.handle, headers.c_str(),
                              static_cast<DWORD>(headers.size()),
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !::WinHttpReceiveResponse(request.handle, nullptr)) {
        return std::unexpected("Сетевая ошибка при обращении к " + core::WideToUtf8(host));
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    ::WinHttpQueryHeaders(request.handle,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                          WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        return std::unexpected("Сервер вернул статус " + std::to_string(status));
    }

    std::uint64_t total = 0;
    {
        wchar_t lengthText[32]{};
        DWORD lengthSize = sizeof(lengthText);
        if (::WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CONTENT_LENGTH,
                                  WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
                                  &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
            total = ::wcstoull(lengthText, nullptr, 10);
        }
    }

    std::uint64_t received = 0;
    std::array<char, 64 * 1024> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!::WinHttpReadData(request.handle, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &read)) {
            return std::unexpected("Ошибка чтения ответа сервера");
        }
        if (read == 0) break;
        received += read;
        if (received > kMaxAssetSize) {
            return std::unexpected("Ответ сервера превышает допустимый размер");
        }
        if (memory) {
            memory->append(buffer.data(), read);
        }
        if (file) {
            file->write(buffer.data(), static_cast<std::streamsize>(read));
            if (!*file) {
                return std::unexpected("Не удалось записать файл обновления на диск");
            }
        }
        if (progress) {
            progress(received, total);
        }
    }
    return {};
}

#endif // _WIN32

} // namespace

SelfUpdate& SelfUpdate::Instance() {
    static SelfUpdate instance;
    return instance;
}

bool SelfUpdate::IsNewer(std::string_view remote, std::string_view local) {
    const auto r = ParseVersion(remote);
    const auto l = ParseVersion(local);
    if (!r || !l) return false;
    const auto asTuple = [](const Version& v) {
        return std::tuple{v.major, v.minor, v.patch};
    };
    return asTuple(*r) > asTuple(*l);
}

#ifdef _WIN32

std::expected<std::optional<ReleaseInfo>, std::string>
SelfUpdate::CheckForUpdate(std::string_view currentVersion) const {
    std::string body;
    auto fetched = HttpGet(std::wstring{kApiHost}, std::wstring{kLatestReleasePath},
                           &body, nullptr, nullptr);
    if (!fetched) {
        return std::unexpected(fetched.error());
    }

    nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.contains("tag_name")) {
        return std::unexpected("Некорректный ответ GitHub Releases API");
    }

    ReleaseInfo info;
    info.tag = json.value("tag_name", "");
    info.version = info.tag;
    if (!info.version.empty() && (info.version.front() == 'v' || info.version.front() == 'V')) {
        info.version.erase(0, 1);
    }
    if (!IsNewer(info.version, currentVersion)) {
        return std::optional<ReleaseInfo>{};
    }

    for (const auto& asset : json.value("assets", nlohmann::json::array())) {
        const auto name = asset.value("name", "");
        if (name.ends_with(kAssetSuffix)) {
            info.assetName = name;
            info.assetUrl = asset.value("browser_download_url", "");
            info.assetSize = asset.value("size", 0ull);
            break;
        }
    }
    if (info.assetUrl.empty()) {
        return std::unexpected("В релизе " + info.tag + " нет zip-архива обновления");
    }
    return std::optional<ReleaseInfo>{std::move(info)};
}

std::expected<std::filesystem::path, std::string> SelfUpdate::DownloadAsset(
    const ReleaseInfo& release,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress) const {
    const auto url = core::Utf8ToWide(release.assetUrl);
    const auto parts = CrackUrl(url);
    if (!parts) {
        return std::unexpected("Некорректный адрес файла обновления");
    }

    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        return std::unexpected("Не удалось получить временную папку");
    }
    const auto zipPath = dir / ("YEET17PCSET-update-" + release.version + ".zip");

    {
        std::ofstream file(zipPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return std::unexpected("Не удалось создать файл обновления во временной папке");
        }
        auto downloaded = HttpGet(parts->host, parts->path, nullptr, &file, progress);
        if (!downloaded) {
            file.close();
            std::filesystem::remove(zipPath, ec);
            return std::unexpected(downloaded.error());
        }
    }

    if (release.assetSize > 0) {
        const auto actual = std::filesystem::file_size(zipPath, ec);
        if (ec || actual != release.assetSize) {
            std::filesystem::remove(zipPath, ec);
            return std::unexpected("Файл обновления скачан не полностью");
        }
    }
    return zipPath;
}

std::expected<void, std::string>
SelfUpdate::LaunchUpdater(const std::filesystem::path& zipPath) const {
    const auto installDir = core::Paths::ExeDir();
    const auto exePath = installDir / "YEET17PCSET.exe";

    std::error_code ec;
    const auto scriptPath =
        std::filesystem::temp_directory_path(ec) / "YEET17PCSET-apply-update.ps1";
    if (ec) {
        return std::unexpected("Не удалось получить временную папку");
    }

    // PowerShell literal single-quoted strings: only ' needs escaping (doubled).
    const auto psQuote = [](const std::wstring& value) {
        std::wstring quoted = L"'";
        for (const wchar_t ch : value) {
            quoted += ch;
            if (ch == L'\'') quoted += ch;
        }
        quoted += L"'";
        return quoted;
    };

    std::wstring script;
    script += L"param()\n";
    script += L"Wait-Process -Id " + std::to_wstring(::GetCurrentProcessId()) +
              L" -ErrorAction SilentlyContinue\n";
    script += L"Start-Sleep -Milliseconds 400\n";
    script += L"Expand-Archive -LiteralPath " + psQuote(zipPath.wstring()) +
              L" -DestinationPath " + psQuote(installDir.wstring()) + L" -Force\n";
    script += L"Remove-Item -LiteralPath " + psQuote(zipPath.wstring()) +
              L" -Force -ErrorAction SilentlyContinue\n";
    script += L"Start-Process -FilePath " + psQuote(exePath.wstring()) + L"\n";
    script += L"Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force "
              L"-ErrorAction SilentlyContinue\n";

    {
        std::ofstream file(scriptPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return std::unexpected("Не удалось записать скрипт обновления");
        }
        // UTF-8 BOM so PowerShell 5.1 reads non-ASCII paths correctly.
        file.write("\xEF\xBB\xBF", 3);
        const auto utf8 = core::WideToUtf8(script);
        file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }

    std::wstring params = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" +
                          scriptPath.wstring() + L"\"";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = L"powershell.exe";
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    if (!::ShellExecuteExW(&info)) {
        return std::unexpected("Не удалось запустить установщик обновления");
    }
    core::Logger::Instance().Info("Запущен установщик обновления, приложение завершается");
    return {};
}

#else

std::expected<std::optional<ReleaseInfo>, std::string>
SelfUpdate::CheckForUpdate(std::string_view) const {
    return std::unexpected("Самообновление доступно только в Windows");
}

std::expected<std::filesystem::path, std::string> SelfUpdate::DownloadAsset(
    const ReleaseInfo&,
    const std::function<void(std::uint64_t, std::uint64_t)>&) const {
    return std::unexpected("Самообновление доступно только в Windows");
}

std::expected<void, std::string> SelfUpdate::LaunchUpdater(const std::filesystem::path&) const {
    return std::unexpected("Самообновление доступно только в Windows");
}

#endif

} // namespace yeet17::updates
