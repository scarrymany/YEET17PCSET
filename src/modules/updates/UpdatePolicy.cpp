#include "pch.h"
#include "modules/updates/UpdatePolicy.h"

#include "core/Logger.h"
#include "core/Settings.h"
#include "core/Utf8.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <map>
#include <mutex>
#include <utility>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <winsvc.h>
#    include <ole2.h>
#    include <oleauto.h>
#    include <taskschd.h>
#    ifdef _MSC_VER
#        pragma comment(lib, "advapi32.lib")
#        pragma comment(lib, "ole32.lib")
#        pragma comment(lib, "oleaut32.lib")
#        pragma comment(lib, "taskschd.lib")
#    endif
#endif

namespace yeet17::updates {
namespace {

// ---------------------------------------------------------------------------
// Documented HKLM values this module owns.
// We delete individual values, never whole keys — other software may live there.
// ---------------------------------------------------------------------------
//
// HKLM\SOFTWARE\Policies\Microsoft\Windows\Device Metadata
//   PreventDeviceMetadataFromNetwork (DWORD) — SecurityOnly=1; blocks WU
//   metadata fetch for drivers. Reset deletes it.
//
// HKLM\SOFTWARE\Policies\Microsoft\Windows\DriverSearching
//   DontPromptForWindowsUpdate (DWORD) — SecurityOnly=1
//   DontSearchWindowsUpdate     (DWORD) — SecurityOnly=1
//   DriverUpdateWizardWuSearchEnabled (DWORD) — SecurityOnly=0
//   Why: stop driver offering through Windows Update (WinUtil security).
//
// HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate
//   ExcludeWUDriversInQualityUpdate (DWORD) — SecurityOnly=1
//   DeferFeatureUpdates             (DWORD) — SecurityOnly=1
//   DeferFeatureUpdatesPeriodInDays (DWORD) — SecurityOnly=365
//   DeferQualityUpdates             (DWORD) — SecurityOnly=1
//   DeferQualityUpdatesPeriodInDays (DWORD) — SecurityOnly=4
//   Why: keep quality/security close, park feature updates for a year.
//   Detect uses DeferFeatureUpdates==1 && period>=180 → SecurityOnly.
//
// HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU
//   AUOptions (DWORD) — SecurityOnly=4 (auto download + scheduled install).
//   NoAutoRebootWithLoggedOnUsers (DWORD) — SecurityOnly=1 (AUOptions=4 only).
//   AUPowerManagement (DWORD) — SecurityOnly=0
//   IncludeRecommendedUpdates (DWORD) — SecurityOnly=0 ("recommended off")
//   NoAutoUpdate — DELETE if present (we never disable WU via this value).
//
// HKLM\SOFTWARE\Microsoft\WindowsUpdate\UX\Settings
//   PauseUpdates              (DWORD 1) — official pause flag
//   PauseUpdatesStartTime     (REG_SZ ISO-8601 UTC)
//   PauseUpdatesExpiryTime    (REG_SZ ISO-8601 UTC) — Detect: future → Pause
//   PauseFeatureUpdatesStartTime / EndTime (REG_SZ) — Settings app pair
//   PauseQualityUpdatesStartTime / EndTime (REG_SZ)
//   FlightSettingsMaxPauseDays (DWORD days) — Home/Pro honor the length
//   BranchReadinessLevel, DeferFeatureUpdatesPeriodInDays,
//   DeferQualityUpdatesPeriodInDays — legacy UX copies; DELETE on apply
//
// HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\DeliveryOptimization\Config
//   DODownloadMode — DELETE on reset only (do not invent a new mode).
//
// HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\Explorer
//   SettingsPageVisibility — DELETE only if the value is exactly
//   "hide:windowsupdate". We never write a hide: value.
//
// Services (Win32 SCM; Linux records under "Service\<name>" / "Start"):
//   BITS     → SERVICE_DEMAND_START (Manual)
//   wuauserv → SERVICE_DEMAND_START (Manual)
//   UsoSvc   → SERVICE_AUTO_START   (Automatic) and StartService
//   We never disable WaaSMedic / UsoSvc / wuauserv / BITS.
//
// Scheduled tasks (Win32, best-effort, reset/default only):
//   \Microsoft\Windows\InstallService\*
//   \Microsoft\Windows\UpdateOrchestrator\*
//   \Microsoft\Windows\UpdateAssistant\*
//   \Microsoft\Windows\WaaSMedic\*
//   \Microsoft\Windows\WindowsUpdate\*
//   \Microsoft\WindowsUpdate\*

constexpr std::string_view kDeviceMetadata =
    "SOFTWARE\\Policies\\Microsoft\\Windows\\Device Metadata";
constexpr std::string_view kDriverSearching =
    "SOFTWARE\\Policies\\Microsoft\\Windows\\DriverSearching";
constexpr std::string_view kWuPolicy =
    "SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate";
constexpr std::string_view kAuPolicy =
    "SOFTWARE\\Policies\\Microsoft\\Windows\\WindowsUpdate\\AU";
constexpr std::string_view kUxSettings =
    "SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings";
constexpr std::string_view kDoConfig =
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DeliveryOptimization\\Config";
constexpr std::string_view kExplorer =
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
constexpr std::string_view kServicePrefix = "Service\\";

#ifndef SERVICE_AUTO_START
constexpr std::uint32_t SERVICE_AUTO_START = 2;
#endif
#ifndef SERVICE_DEMAND_START
constexpr std::uint32_t SERVICE_DEMAND_START = 3;
#endif

struct ValueRef {
    std::string_view path;
    std::string_view name;
};

constexpr ValueRef kManagedValues[] = {
    {kDeviceMetadata, "PreventDeviceMetadataFromNetwork"},
    {kDriverSearching, "DontPromptForWindowsUpdate"},
    {kDriverSearching, "DontSearchWindowsUpdate"},
    {kDriverSearching, "DriverUpdateWizardWuSearchEnabled"},
    {kWuPolicy, "ExcludeWUDriversInQualityUpdate"},
    {kWuPolicy, "DeferFeatureUpdates"},
    {kWuPolicy, "DeferFeatureUpdatesPeriodInDays"},
    {kWuPolicy, "DeferQualityUpdates"},
    {kWuPolicy, "DeferQualityUpdatesPeriodInDays"},
    {kAuPolicy, "AUOptions"},
    {kAuPolicy, "NoAutoRebootWithLoggedOnUsers"},
    {kAuPolicy, "AUPowerManagement"},
    {kAuPolicy, "NoAutoUpdate"},
    {kAuPolicy, "IncludeRecommendedUpdates"},
    {kUxSettings, "PauseUpdates"},
    {kUxSettings, "PauseUpdatesStartTime"},
    {kUxSettings, "PauseUpdatesExpiryTime"},
    {kUxSettings, "PauseFeatureUpdatesStartTime"},
    {kUxSettings, "PauseQualityUpdatesStartTime"},
    {kUxSettings, "PauseFeatureUpdatesEndTime"},
    {kUxSettings, "PauseQualityUpdatesEndTime"},
    {kUxSettings, "FlightSettingsMaxPauseDays"},
    {kUxSettings, "BranchReadinessLevel"},
    {kUxSettings, "DeferFeatureUpdatesPeriodInDays"},
    {kUxSettings, "DeferQualityUpdatesPeriodInDays"},
    {kDoConfig, "DODownloadMode"},
    {kExplorer, "SettingsPageVisibility"},
};

constexpr std::string_view kServiceNames[] = {"BITS", "wuauserv", "UsoSvc"};

#ifdef _WIN32
constexpr const wchar_t* kTaskFolders[] = {
    L"\\Microsoft\\Windows\\InstallService",
    L"\\Microsoft\\Windows\\UpdateOrchestrator",
    L"\\Microsoft\\Windows\\UpdateAssistant",
    L"\\Microsoft\\Windows\\WaaSMedic",
    L"\\Microsoft\\Windows\\WindowsUpdate",
    L"\\Microsoft\\WindowsUpdate",
};
#endif

std::mutex g_mutex;

#ifndef _WIN32
// In-memory HKLM stand-in so Read/Apply/Undo/Reset are testable on Linux.
// We never pretend a Win32 call succeeded.
struct MemValue {
    enum class Kind { Missing, Dword, Sz };
    Kind kind = Kind::Missing;
    std::uint32_t dword = 0;
    std::string sz;
};
std::map<std::pair<std::string, std::string>, MemValue> g_memory;
#endif

enum class ValKind { Missing, Dword, Sz };

struct Captured {
    std::string path;
    std::string name;
    ValKind kind = ValKind::Missing;
    std::uint32_t dword = 0;
    std::string sz;
};

[[nodiscard]] std::string WriteFail(std::string_view name) {
    return std::string{"Не удалось записать параметр «"} + std::string{name} + "».";
}

[[nodiscard]] std::string DeleteFail(std::string_view name) {
    return std::string{"Не удалось удалить параметр «"} + std::string{name} + "».";
}

[[nodiscard]] std::string RestoreFail(std::string_view name) {
    return std::string{"Не удалось восстановить параметр «"} + std::string{name} + "».";
}

[[nodiscard]] std::string ServiceFail(std::string_view name) {
    return std::string{"Не удалось настроить службу «"} + std::string{name} + "».";
}

[[nodiscard]] int ClampPauseDays(int days) {
    return std::clamp(days, 1, 35);
}

[[nodiscard]] std::filesystem::path UndoPath() {
    return yeet17::core::AppDataDirectory() / "updates-undo.json";
}

[[nodiscard]] std::string FormatUtcIso8601(std::chrono::system_clock::time_point tp) {
    const auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &t) != 0) {
        return {};
    }
#else
    if (!gmtime_r(&t, &utc)) {
        return {};
    }
#endif
    char buf[40]{};
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
        return {};
    }
    return buf;
}

[[nodiscard]] std::optional<std::chrono::system_clock::time_point>
ParseUtcIso8601(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::string tmp{text};
    if (!tmp.empty() && (tmp.back() == 'Z' || tmp.back() == 'z')) {
        tmp.pop_back();
    }
    // Drop fractional seconds if a caller stored them.
    if (const auto dot = tmp.find('.'); dot != std::string::npos) {
        tmp.resize(dot);
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(tmp.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 6 &&
        std::sscanf(tmp.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 6) {
        return std::nullopt;
    }
    const std::chrono::year_month_day ymd{
        std::chrono::year{y},
        std::chrono::month{static_cast<unsigned>(mo)},
        std::chrono::day{static_cast<unsigned>(d)}};
    if (!ymd.ok()) {
        return std::nullopt;
    }
    const auto abs = std::chrono::sys_days{ymd} + std::chrono::hours{h} +
                     std::chrono::minutes{mi} + std::chrono::seconds{s};
    return std::chrono::system_clock::time_point{abs.time_since_epoch()};
}

#ifdef _WIN32
class RegKey {
public:
    HKEY h = nullptr;
    ~RegKey() {
        if (h) {
            RegCloseKey(h);
        }
    }
    RegKey() = default;
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

class ScHandle {
public:
    SC_HANDLE h = nullptr;
    ~ScHandle() {
        if (h) {
            CloseServiceHandle(h);
        }
    }
    ScHandle() = default;
    ScHandle(const ScHandle&) = delete;
    ScHandle& operator=(const ScHandle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};
#endif

[[nodiscard]] Captured QueryValue(std::string_view path, std::string_view name) {
    Captured out;
    out.path = std::string{path};
    out.name = std::string{name};

#ifdef _WIN32
    const auto wpath = yeet17::core::Utf8ToWide(path);
    const auto wname = yeet17::core::Utf8ToWide(name);
    RegKey key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wpath.c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, &key.h) != ERROR_SUCCESS) {
        return out;
    }
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key.h, wname.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        return out;
    }
    if (type == REG_DWORD && size == sizeof(DWORD)) {
        DWORD dword = 0;
        if (RegQueryValueExW(key.h, wname.c_str(), nullptr, &type,
                             reinterpret_cast<LPBYTE>(&dword), &size) == ERROR_SUCCESS) {
            out.kind = ValKind::Dword;
            out.dword = dword;
        }
        return out;
    }
    if ((type == REG_SZ || type == REG_EXPAND_SZ) && size > 0) {
        std::wstring buf(size / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(key.h, wname.c_str(), nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf.data()), &size) == ERROR_SUCCESS) {
            if (!buf.empty() && buf.back() == L'\0') {
                buf.pop_back();
            }
            // size is in bytes and may include a trailing NUL that we already popped.
            while (!buf.empty() && buf.back() == L'\0') {
                buf.pop_back();
            }
            out.kind = ValKind::Sz;
            out.sz = yeet17::core::WideToUtf8(buf);
        }
    }
    return out;
#else
    const auto it = g_memory.find({std::string{path}, std::string{name}});
    if (it == g_memory.end() || it->second.kind == MemValue::Kind::Missing) {
        return out;
    }
    if (it->second.kind == MemValue::Kind::Dword) {
        out.kind = ValKind::Dword;
        out.dword = it->second.dword;
    } else {
        out.kind = ValKind::Sz;
        out.sz = it->second.sz;
    }
    return out;
#endif
}

[[nodiscard]] std::optional<std::uint32_t> QueryDword(std::string_view path, std::string_view name) {
    const auto v = QueryValue(path, name);
    if (v.kind != ValKind::Dword) {
        return std::nullopt;
    }
    return v.dword;
}

[[nodiscard]] std::optional<std::string> QuerySz(std::string_view path, std::string_view name) {
    const auto v = QueryValue(path, name);
    if (v.kind != ValKind::Sz) {
        return std::nullopt;
    }
    return v.sz;
}

bool SetDword(std::string_view path, std::string_view name, std::uint32_t value, std::string& error) {
#ifdef _WIN32
    const auto wpath = yeet17::core::Utf8ToWide(path);
    const auto wname = yeet17::core::Utf8ToWide(name);
    RegKey key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, wpath.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key.h,
                        nullptr) != ERROR_SUCCESS) {
        error = WriteFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    const DWORD dword = value;
    if (RegSetValueExW(key.h, wname.c_str(), 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dword),
                       sizeof(dword)) != ERROR_SUCCESS) {
        error = WriteFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
#else
    g_memory[{std::string{path}, std::string{name}}] =
        MemValue{MemValue::Kind::Dword, value, {}};
#endif
    yeet17::core::Logger::Instance().Info(std::string{"Записан DWORD «"} + std::string{name} + "»");
    return true;
}

bool SetSz(std::string_view path, std::string_view name, std::string_view value, std::string& error) {
#ifdef _WIN32
    const auto wpath = yeet17::core::Utf8ToWide(path);
    const auto wname = yeet17::core::Utf8ToWide(name);
    const auto wvalue = yeet17::core::Utf8ToWide(value);
    RegKey key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, wpath.c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key.h,
                        nullptr) != ERROR_SUCCESS) {
        error = WriteFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((wvalue.size() + 1) * sizeof(wchar_t));
    if (RegSetValueExW(key.h, wname.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(wvalue.c_str()),
                       bytes) != ERROR_SUCCESS) {
        error = WriteFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
#else
    g_memory[{std::string{path}, std::string{name}}] =
        MemValue{MemValue::Kind::Sz, 0, std::string{value}};
#endif
    yeet17::core::Logger::Instance().Info(std::string{"Записан REG_SZ «"} + std::string{name} + "»");
    return true;
}

bool DeleteVal(std::string_view path, std::string_view name, std::string& error) {
#ifdef _WIN32
    const auto wpath = yeet17::core::Utf8ToWide(path);
    const auto wname = yeet17::core::Utf8ToWide(name);
    RegKey key;
    const LONG open = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wpath.c_str(), 0,
                                    KEY_SET_VALUE | KEY_WOW64_64KEY, &key.h);
    if (open == ERROR_FILE_NOT_FOUND || open == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    if (open != ERROR_SUCCESS) {
        error = DeleteFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    const LONG st = RegDeleteValueW(key.h, wname.c_str());
    if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
        error = DeleteFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
#else
    g_memory.erase({std::string{path}, std::string{name}});
#endif
    yeet17::core::Logger::Instance().Info(std::string{"Удалён параметр «"} + std::string{name} + "»");
    return true;
}

[[nodiscard]] Captured QueryServiceStart(std::string_view name) {
    const std::string path = std::string{kServicePrefix} + std::string{name};
    Captured out;
    out.path = path;
    out.name = "Start";

#ifdef _WIN32
    const auto wname = yeet17::core::Utf8ToWide(name);
    ScHandle scm;
    scm.h = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        return out;
    }
    ScHandle svc;
    svc.h = OpenServiceW(scm.h, wname.c_str(), SERVICE_QUERY_CONFIG);
    if (!svc) {
        return out;
    }
    DWORD bytes = 0;
    QueryServiceConfigW(svc.h, nullptr, 0, &bytes);
    if (bytes == 0) {
        return out;
    }
    std::vector<BYTE> buf(bytes);
    if (!QueryServiceConfigW(svc.h, reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data()),
                             bytes, &bytes)) {
        return out;
    }
    const auto* cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data());
    out.kind = ValKind::Dword;
    out.dword = cfg->dwStartType;
    return out;
#else
    return QueryValue(path, "Start");
#endif
}

bool ConfigureService(std::string_view name, std::uint32_t startType, bool startNow,
                      std::string& error) {
    const std::string path = std::string{kServicePrefix} + std::string{name};

#ifdef _WIN32
    const auto wname = yeet17::core::Utf8ToWide(name);
    ScHandle scm;
    scm.h = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        error = ServiceFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    ScHandle svc;
    svc.h = OpenServiceW(scm.h, wname.c_str(),
                         SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) {
        error = ServiceFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    if (!ChangeServiceConfigW(svc.h, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
                              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr)) {
        error = ServiceFail(name);
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    if (startNow) {
        if (!StartServiceW(svc.h, 0, nullptr)) {
            const DWORD err = GetLastError();
            if (err != ERROR_SERVICE_ALREADY_RUNNING) {
                yeet17::core::Logger::Instance().Warn(
                    std::string{"Служба «"} + std::string{name} +
                    "» настроена, но не запущена.");
            }
        }
    }
#else
    g_memory[{path, "Start"}] = MemValue{MemValue::Kind::Dword, startType, {}};
    if (startNow) {
        g_memory[{path, "Started"}] = MemValue{MemValue::Kind::Dword, 1, {}};
    }
#endif
    yeet17::core::Logger::Instance().Info(std::string{"Служба «"} + std::string{name} +
                                          "» — тип запуска обновлён.");
    return true;
}

bool RestoreServiceStart(const Captured& cap, std::string& error) {
    if (cap.path.rfind(kServicePrefix, 0) != 0) {
        return true;
    }
    const std::string name = cap.path.substr(kServicePrefix.size());
    if (cap.kind == ValKind::Missing) {
        // We never saw a previous start type; leave whatever Apply wrote.
        return true;
    }
    if (cap.kind != ValKind::Dword) {
        return true;
    }
    // Restore startup type only — do not stop a running WU service.
    return ConfigureService(name, cap.dword, false, error);
}

bool RestoreValue(const Captured& cap, std::string& error) {
    if (cap.path.rfind(kServicePrefix, 0) == 0) {
        return RestoreServiceStart(cap, error);
    }
    if (cap.kind == ValKind::Missing) {
        return DeleteVal(cap.path, cap.name, error);
    }
    if (cap.kind == ValKind::Dword) {
        return SetDword(cap.path, cap.name, cap.dword, error);
    }
    return SetSz(cap.path, cap.name, cap.sz, error);
}

[[nodiscard]] nlohmann::json CapturedToJson(const Captured& cap) {
    nlohmann::json j{
        {"path", cap.path},
        {"name", cap.name},
    };
    if (cap.kind == ValKind::Missing) {
        j["missing"] = true;
    } else if (cap.kind == ValKind::Dword) {
        j["type"] = "dword";
        j["data"] = cap.dword;
    } else {
        j["type"] = "sz";
        j["data"] = cap.sz;
    }
    return j;
}

[[nodiscard]] std::optional<Captured> CapturedFromJson(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("path") || !j.contains("name")) {
        return std::nullopt;
    }
    if (!j["path"].is_string() || !j["name"].is_string()) {
        return std::nullopt;
    }
    Captured cap;
    cap.path = j["path"].get<std::string>();
    cap.name = j["name"].get<std::string>();
    if (j.value("missing", false)) {
        cap.kind = ValKind::Missing;
        return cap;
    }
    const auto type = j.value("type", std::string{});
    if (type == "dword") {
        if (!j.contains("data") || !j["data"].is_number_integer()) {
            return std::nullopt;
        }
        const auto n = j["data"].get<std::int64_t>();
        if (n < 0 || n > 0xFFFFFFFFLL) {
            return std::nullopt;
        }
        cap.kind = ValKind::Dword;
        cap.dword = static_cast<std::uint32_t>(n);
        return cap;
    }
    if (type == "sz") {
        if (!j.contains("data") || !j["data"].is_string()) {
            return std::nullopt;
        }
        cap.kind = ValKind::Sz;
        cap.sz = j["data"].get<std::string>();
        return cap;
    }
    return std::nullopt;
}

bool WriteSnapshot(const std::vector<Captured>& values, std::string_view previousPolicy,
                   int previousPauseDays, std::string& error) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& v : values) {
        arr.push_back(CapturedToJson(v));
    }
    const nlohmann::json root{
        {"version", 1},
        {"previousPolicy", std::string{previousPolicy}},
        {"previousPauseDays", previousPauseDays},
        {"values", std::move(arr)},
    };

    const auto dir = yeet17::core::AppDataDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = "Не удалось создать каталог для снимка отмены.";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

    const auto path = UndoPath();
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out{tmp};
        if (!out) {
            error = "Не удалось сохранить снимок отмены.";
            yeet17::core::Logger::Instance().Error(error);
            return false;
        }
        out << root.dump(2);
        if (!out) {
            error = "Не удалось сохранить снимок отмены.";
            yeet17::core::Logger::Instance().Error(error);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "Не удалось сохранить снимок отмены.";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    yeet17::core::Logger::Instance().Info("Создан снимок политики обновлений для отмены.");
    return true;
}

bool TakeFreshSnapshot(std::string& error) {
    std::vector<Captured> values;
    values.reserve(std::size(kManagedValues) + std::size(kServiceNames));
    for (const auto& ref : kManagedValues) {
        values.push_back(QueryValue(ref.path, ref.name));
    }
    for (const auto& svc : kServiceNames) {
        values.push_back(QueryServiceStart(svc));
    }

    const auto& settings = yeet17::core::Settings::Instance().Current();
    int prevDays = 7;
    if (const auto maxDays = QueryDword(kUxSettings, "FlightSettingsMaxPauseDays")) {
        prevDays = ClampPauseDays(static_cast<int>(*maxDays));
    }
    return WriteSnapshot(values, settings.updatePolicy, prevDays, error);
}

bool PersistMode(Mode mode) {
    yeet17::core::Settings::Instance().Current().updatePolicy = std::string{UpdatePolicy::ToId(mode)};
    yeet17::core::Settings::Instance().Save();
    return true;
}

bool EnsureHealthyServices(std::string& error) {
    // Manual BITS / wuauserv keeps WU available without forcing a boot-start
    // service. Automatic UsoSvc is what the orchestrator expects.
    if (!ConfigureService("BITS", SERVICE_DEMAND_START, false, error)) {
        return false;
    }
    if (!ConfigureService("wuauserv", SERVICE_DEMAND_START, false, error)) {
        return false;
    }
    if (!ConfigureService("UsoSvc", SERVICE_AUTO_START, true, error)) {
        return false;
    }
    return true;
}

void EnableUpdateTasksBestEffort() {
#ifdef _WIN32
    // Best-effort: a protected task must not fail the apply. We log and continue.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool weInit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        // Already initialized on another apartment — Connect still works.
    } else if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && !weInit) {
        yeet17::core::Logger::Instance().Warn(
            "Планировщик заданий недоступен; задачи обновлений не включены.");
        return;
    }

    ITaskService* service = nullptr;
    hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, reinterpret_cast<void**>(&service));
    if (FAILED(hr) || !service) {
        yeet17::core::Logger::Instance().Warn(
            "Не удалось создать службу планировщика заданий.");
        if (weInit) {
            CoUninitialize();
        }
        return;
    }

    VARIANT empty;
    VariantInit(&empty);
    hr = service->Connect(empty, empty, empty, empty);
    VariantClear(&empty);
    if (FAILED(hr)) {
        yeet17::core::Logger::Instance().Warn("Не удалось подключиться к планировщику заданий.");
        service->Release();
        if (weInit) {
            CoUninitialize();
        }
        return;
    }

    for (const wchar_t* folderPath : kTaskFolders) {
        ITaskFolder* folder = nullptr;
        BSTR bpath = SysAllocString(folderPath);
        hr = service->GetFolder(bpath, &folder);
        SysFreeString(bpath);
        if (FAILED(hr) || !folder) {
            yeet17::core::Logger::Instance().Warn("Папка заданий обновлений не найдена.");
            continue;
        }
        IRegisteredTaskCollection* tasks = nullptr;
        if (FAILED(folder->GetTasks(TASK_ENUM_HIDDEN, &tasks)) || !tasks) {
            folder->Release();
            continue;
        }
        LONG count = 0;
        tasks->get_Count(&count);
        for (LONG i = 1; i <= count; ++i) {
            VARIANT index;
            VariantInit(&index);
            index.vt = VT_I4;
            index.lVal = i;
            IRegisteredTask* task = nullptr;
            if (SUCCEEDED(tasks->get_Item(index, &task)) && task) {
                const HRESULT en = task->put_Enabled(VARIANT_TRUE);
                if (FAILED(en)) {
                    yeet17::core::Logger::Instance().Warn(
                        "Не удалось включить задачу обновлений (игнорируется).");
                }
                task->Release();
            }
            VariantClear(&index);
        }
        tasks->Release();
        folder->Release();
    }

    service->Release();
    if (weInit) {
        CoUninitialize();
    }
    yeet17::core::Logger::Instance().Info("Задачи обновлений включены (по возможности).");
#else
    yeet17::core::Logger::Instance().Info(
        "Планировщик заданий недоступен на этой платформе — пропуск.");
#endif
}

bool ClearPauseKeys(std::string& error) {
    return DeleteVal(kUxSettings, "PauseUpdates", error) &&
           DeleteVal(kUxSettings, "PauseUpdatesStartTime", error) &&
           DeleteVal(kUxSettings, "PauseUpdatesExpiryTime", error) &&
           DeleteVal(kUxSettings, "PauseFeatureUpdatesStartTime", error) &&
           DeleteVal(kUxSettings, "PauseQualityUpdatesStartTime", error) &&
           DeleteVal(kUxSettings, "PauseFeatureUpdatesEndTime", error) &&
           DeleteVal(kUxSettings, "PauseQualityUpdatesEndTime", error) &&
           DeleteVal(kUxSettings, "FlightSettingsMaxPauseDays", error);
}

bool DeleteManagedPolicies(std::string& error) {
    // SettingsPageVisibility is special: only remove the exact hide we refuse
    // to author. Any other hide-list belongs to the user / another tool.
    for (const auto& ref : kManagedValues) {
        if (ref.path == kExplorer && ref.name == "SettingsPageVisibility") {
            const auto current = QuerySz(ref.path, ref.name);
            if (!current || *current != "hide:windowsupdate") {
                continue;
            }
        }
        if (!DeleteVal(ref.path, ref.name, error)) {
            return false;
        }
    }
    return true;
}

bool ApplyResetPath(bool enableTasks, std::string& error) {
    if (!DeleteManagedPolicies(error)) {
        return false;
    }
    if (!EnsureHealthyServices(error)) {
        return false;
    }
    if (enableTasks) {
        EnableUpdateTasksBestEffort();
    }
    return true;
}

bool ApplySecurityOnly(std::string& error) {
    if (!ClearPauseKeys(error)) {
        return false;
    }
    if (!SetDword(kDeviceMetadata, "PreventDeviceMetadataFromNetwork", 1, error)) {
        return false;
    }
    if (!SetDword(kDriverSearching, "DontPromptForWindowsUpdate", 1, error)) {
        return false;
    }
    if (!SetDword(kDriverSearching, "DontSearchWindowsUpdate", 1, error)) {
        return false;
    }
    if (!SetDword(kDriverSearching, "DriverUpdateWizardWuSearchEnabled", 0, error)) {
        return false;
    }
    if (!SetDword(kWuPolicy, "ExcludeWUDriversInQualityUpdate", 1, error)) {
        return false;
    }
    if (!SetDword(kWuPolicy, "DeferFeatureUpdates", 1, error)) {
        return false;
    }
    if (!SetDword(kWuPolicy, "DeferFeatureUpdatesPeriodInDays", 365, error)) {
        return false;
    }
    if (!SetDword(kWuPolicy, "DeferQualityUpdates", 1, error)) {
        return false;
    }
    if (!SetDword(kWuPolicy, "DeferQualityUpdatesPeriodInDays", 4, error)) {
        return false;
    }
    if (!SetDword(kAuPolicy, "AUOptions", 4, error)) {
        return false;
    }
    if (!SetDword(kAuPolicy, "NoAutoRebootWithLoggedOnUsers", 1, error)) {
        return false;
    }
    if (!SetDword(kAuPolicy, "AUPowerManagement", 0, error)) {
        return false;
    }
    if (!SetDword(kAuPolicy, "IncludeRecommendedUpdates", 0, error)) {
        return false;
    }
    if (!DeleteVal(kAuPolicy, "NoAutoUpdate", error)) {
        return false;
    }
    // Legacy UX copies fight Group Policy; delete them so the policy keys win.
    if (!DeleteVal(kUxSettings, "BranchReadinessLevel", error)) {
        return false;
    }
    if (!DeleteVal(kUxSettings, "DeferFeatureUpdatesPeriodInDays", error)) {
        return false;
    }
    if (!DeleteVal(kUxSettings, "DeferQualityUpdatesPeriodInDays", error)) {
        return false;
    }
    return EnsureHealthyServices(error);
}

bool ApplyPause(int days, std::string& error) {
    const auto now = std::chrono::system_clock::now();
    const auto end = now + std::chrono::days{days};
    const auto startIso = FormatUtcIso8601(now);
    const auto endIso = FormatUtcIso8601(end);
    if (startIso.empty() || endIso.empty()) {
        error = "Не удалось сформировать метки времени паузы.";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

    // Official pause only. We do not set NoAutoUpdate and we do not stop services.
    if (!SetDword(kUxSettings, "PauseUpdates", 1, error)) {
        return false;
    }
    if (!SetSz(kUxSettings, "PauseUpdatesStartTime", startIso, error)) {
        return false;
    }
    if (!SetSz(kUxSettings, "PauseUpdatesExpiryTime", endIso, error)) {
        return false;
    }
    if (!SetSz(kUxSettings, "PauseFeatureUpdatesStartTime", startIso, error)) {
        return false;
    }
    if (!SetSz(kUxSettings, "PauseQualityUpdatesStartTime", startIso, error)) {
        return false;
    }
    if (!SetSz(kUxSettings, "PauseFeatureUpdatesEndTime", endIso, error)) {
        return false;
    }
    if (!SetSz(kUxSettings, "PauseQualityUpdatesEndTime", endIso, error)) {
        return false;
    }
    if (!SetDword(kUxSettings, "FlightSettingsMaxPauseDays", static_cast<std::uint32_t>(days),
                  error)) {
        return false;
    }
    return EnsureHealthyServices(error);
}

[[nodiscard]] State DetectLocked() {
    State state;

    if (const auto maxDays = QueryDword(kUxSettings, "FlightSettingsMaxPauseDays")) {
        state.pauseDays = ClampPauseDays(static_cast<int>(*maxDays));
    }

    if (const auto expirySz = QuerySz(kUxSettings, "PauseUpdatesExpiryTime")) {
        if (const auto expiry = ParseUtcIso8601(*expirySz)) {
            if (*expiry > std::chrono::system_clock::now()) {
                state.mode = Mode::Pause;
                const auto remain = std::chrono::duration_cast<std::chrono::hours>(
                    *expiry - std::chrono::system_clock::now());
                const int daysLeft = static_cast<int>((remain.count() + 23) / 24);
                state.pauseDays = ClampPauseDays(std::max(daysLeft, 1));
                return state;
            }
        }
    }

    const auto defer = QueryDword(kWuPolicy, "DeferFeatureUpdates");
    const auto period = QueryDword(kWuPolicy, "DeferFeatureUpdatesPeriodInDays");
    if (defer && *defer == 1 && period && *period >= 180) {
        state.mode = Mode::SecurityOnly;
        return state;
    }

    const auto& id = yeet17::core::Settings::Instance().Current().updatePolicy;
    if (id == "default") {
        state.mode = Mode::Default;
    } else {
        state.mode = Mode::Full;
    }
    return state;
}

} // namespace

nlohmann::json State::ToJson() const {
    return nlohmann::json{
        {"mode", std::string{UpdatePolicy::ToId(mode)}},
        {"pauseDays", pauseDays},
    };
}

State State::FromJson(const nlohmann::json& json) {
    State s;
    const nlohmann::json* obj = &json;
    if (json.is_object() && json.contains("updates") && json["updates"].is_object()) {
        obj = &json["updates"];
    }
    if (obj->contains("mode") && (*obj)["mode"].is_string()) {
        if (const auto parsed = UpdatePolicy::FromId((*obj)["mode"].get<std::string>())) {
            s.mode = *parsed;
        }
    }
    if (obj->contains("pauseDays") && (*obj)["pauseDays"].is_number_integer()) {
        s.pauseDays = (*obj)["pauseDays"].get<int>();
    }
    return s;
}

UpdatePolicy& UpdatePolicy::Instance() {
    static UpdatePolicy instance;
    return instance;
}

std::string_view UpdatePolicy::ToId(Mode mode) {
    switch (mode) {
    case Mode::SecurityOnly:
        return "security";
    case Mode::Pause:
        return "pause";
    case Mode::Default:
        return "default";
    case Mode::Full:
    default:
        return "full";
    }
}

std::optional<Mode> UpdatePolicy::FromId(std::string_view id) {
    if (id == "full") {
        return Mode::Full;
    }
    if (id == "security") {
        return Mode::SecurityOnly;
    }
    if (id == "pause" || id == "paused") {
        return Mode::Pause;
    }
    if (id == "default") {
        return Mode::Default;
    }
    return std::nullopt;
}

State UpdatePolicy::Read() const {
    std::lock_guard lock{g_mutex};
    return DetectLocked();
}

bool UpdatePolicy::Apply(State next, std::string& error) {
    std::lock_guard lock{g_mutex};
    error.clear();
    next.pauseDays = ClampPauseDays(next.pauseDays);

    if (!TakeFreshSnapshot(error)) {
        return false;
    }

    bool ok = false;
    switch (next.mode) {
    case Mode::Full:
        ok = ApplyResetPath(/*enableTasks=*/false, error);
        break;
    case Mode::SecurityOnly:
        ok = ApplySecurityOnly(error);
        break;
    case Mode::Pause:
        ok = ApplyPause(next.pauseDays, error);
        break;
    case Mode::Default:
        ok = ApplyResetPath(/*enableTasks=*/true, error);
        break;
    }

    if (!ok) {
        if (error.empty()) {
            error = "Не удалось применить политику обновлений.";
        }
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

    PersistMode(next.mode);
    switch (next.mode) {
    case Mode::SecurityOnly:
        yeet17::core::Logger::Instance().Info("Политика обновлений: только безопасность.");
        break;
    case Mode::Pause:
        yeet17::core::Logger::Instance().Info(
            std::string{"Обновления приостановлены на "} + std::to_string(next.pauseDays) +
            " дн.");
        break;
    case Mode::Default:
        yeet17::core::Logger::Instance().Info(
            "Политика обновлений сброшена к значениям по умолчанию.");
        break;
    case Mode::Full:
        yeet17::core::Logger::Instance().Info("Политика обновлений: полные обновления.");
        break;
    }
    return true;
}

bool UpdatePolicy::ResetToDefault(std::string& error) {
    State next;
    next.mode = Mode::Default;
    next.pauseDays = 7;
    return Apply(next, error);
}

bool UpdatePolicy::Undo(std::string& error) {
    std::lock_guard lock{g_mutex};
    error.clear();

    const auto path = UndoPath();
    std::error_code existsEc;
    if (!std::filesystem::exists(path, existsEc)) {
        error = "Нет снимка для отмены.";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

    nlohmann::json root;
    try {
        std::ifstream in{path};
        if (!in) {
            error = "Снимок отмены повреждён и не может быть применён.";
            yeet17::core::Logger::Instance().Error(error);
            return false;
        }
        in >> root;
    } catch (const std::exception&) {
        error = "Снимок отмены повреждён и не может быть применён.";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

    if (!root.is_object() || root.value("version", 0) != 1 || !root.contains("values") ||
        !root["values"].is_array()) {
        error = "Снимок отмены повреждён и не может быть применён.";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

    for (const auto& item : root["values"]) {
        const auto cap = CapturedFromJson(item);
        if (!cap) {
            error = "Снимок отмены повреждён и не может быть применён.";
            yeet17::core::Logger::Instance().Error(error);
            return false;
        }
        if (!RestoreValue(*cap, error)) {
            if (error.empty()) {
                error = RestoreFail(cap->name);
            }
            return false;
        }
    }

    if (root.contains("previousPolicy") && root["previousPolicy"].is_string()) {
        yeet17::core::Settings::Instance().Current().updatePolicy =
            root["previousPolicy"].get<std::string>();
        yeet17::core::Settings::Instance().Save();
    }

    std::error_code rmEc;
    std::filesystem::remove(path, rmEc);
    yeet17::core::Logger::Instance().Info("Политика обновлений отменена по снимку.");
    return true;
}

} // namespace yeet17::updates
