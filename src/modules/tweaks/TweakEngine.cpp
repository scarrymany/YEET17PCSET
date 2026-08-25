#include "pch.h"
#include "modules/tweaks/TweakEngine.h"
#include "modules/tweaks/RestorePoint.h"

#include <cstdint>
#include <cstdlib>
#include <type_traits>

#ifdef _WIN32
#    include <objbase.h>
#    include <oleauto.h>
#    include <taskschd.h>
#    include <winsvc.h>
#    ifdef _MSC_VER
#        pragma comment(lib, "taskschd.lib")
#        pragma comment(lib, "ole32.lib")
#        pragma comment(lib, "oleaut32.lib")
#    endif
#endif

namespace yeet17::tweaks {
namespace {

constexpr const wchar_t* kRestorePointDesc = L"YEET17PCSET: перед пакетом твиков";

Hive ParseHive(std::string_view s) {
    return (s == "HKCU" || s == "hkcu") ? Hive::Hkcu : Hive::Hklm;
}

RegType ParseRegType(std::string_view s) {
    return (s == "sz" || s == "SZ" || s == "string") ? RegType::Sz : RegType::Dword;
}

StartType ParseStartType(std::string_view s) {
    if (s == "disabled") return StartType::Disabled;
    if (s == "manual") return StartType::Manual;
    return StartType::Auto;
}

Tier ParseTier(std::string_view s) {
    return (s == "advanced") ? Tier::Advanced : Tier::Essential;
}

std::string RegMapKey(Hive hive, std::string_view key, std::string_view name) {
    std::string out = (hive == Hive::Hkcu) ? "HKCU|" : "HKLM|";
    out.append(key);
    out.push_back('|');
    out.append(name);
    return out;
}

std::optional<std::uint32_t> AsDword(const nlohmann::json& j) {
    if (j.is_number_unsigned()) return static_cast<std::uint32_t>(j.get<std::uint64_t>());
    if (j.is_number_integer()) return static_cast<std::uint32_t>(j.get<std::int64_t>());
    if (j.is_number_float()) return static_cast<std::uint32_t>(j.get<double>());
    if (j.is_string()) {
        try {
            return static_cast<std::uint32_t>(std::stoul(j.get<std::string>()));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool ValuesEqual(RegType type, const nlohmann::json& a, const nlohmann::json& b) {
    if (type == RegType::Dword) {
        const auto da = AsDword(a);
        const auto db = AsDword(b);
        return da.has_value() && db.has_value() && *da == *db;
    }
    const std::string sa = a.is_string() ? a.get<std::string>() : (a.is_null() ? std::string{} : a.dump());
    const std::string sb = b.is_string() ? b.get<std::string>() : (b.is_null() ? std::string{} : b.dump());
    return sa == sb;
}

std::expected<Op, std::string> ParseOp(const nlohmann::json& j) {
    // Accept both engine schema (type=registry|service|task) and catalog
    // schema (kind=registry.set|service.stop|service.disable|task.set).
    auto type = j.value("type", std::string{});
    if (type.empty()) {
        type = j.value("kind", std::string{});
    }
    if (type == "registry" || type == "registry.set") {
        RegistryOp op;
        op.hive = ParseHive(j.value("hive", "HKLM"));
        op.key = j.value("key", std::string{});
        op.name = j.value("name", std::string{});
        op.valueType = ParseRegType(j.value("valueType", "dword"));
        if (j.contains("apply")) {
            op.apply = j["apply"];
        } else if (j.contains("value")) {
            op.apply = j["value"];
        }
        op.createKey = j.value("createKey", true);
        return Op{std::move(op)};
    }
    if (type == "service" || type == "service.stop" || type == "service.disable" ||
        type == "service.set") {
        ServiceOp op;
        op.name = j.value("name", j.value("service", std::string{}));
        if (type == "service.stop" || type == "service.disable") {
            op.applyStartType = StartType::Disabled;
            op.stopOnApply = true;
        } else {
            op.applyStartType = ParseStartType(j.value("applyStartType", "disabled"));
            op.stopOnApply = j.value("stopOnApply", true);
        }
        return Op{std::move(op)};
    }
    if (type == "task" || type == "task.set" || type == "task.disable") {
        TaskOp op;
        op.path = j.value("path", std::string{});
        op.apply = j.value("apply", j.value("value", std::string{"disabled"}));
        if (type == "task.disable") {
            op.apply = "disabled";
        }
        return Op{std::move(op)};
    }
    return std::unexpected<std::string>("Неизвестный тип операции: " + type);
}

std::expected<Tweak, std::string> ParseTweak(const nlohmann::json& j) {
    Tweak t;
    t.id = j.value("id", std::string{});
    if (t.id.empty()) {
        return std::unexpected<std::string>("Твик без поля id");
    }
    t.title = j.value("title", t.id);
    t.description = j.value("description", std::string{});
    t.category = j.value("category", j.value("group", std::string{"ui"}));
    t.tier = ParseTier(j.value("tier", j.value("group", "essential")));
    if (j.contains("os") && j["os"].is_array()) {
        t.os = j["os"].get<std::vector<std::string>>();
    } else {
        t.os = {"win10", "win11"};
    }
    t.requiresConfirm = j.value("requiresConfirm", false);
    t.reversible = j.value("reversible", true);
    t.rebootRecommended = j.value("rebootRecommended", false);
    t.explorerRestart = j.value("explorerRestart", false);
    if (j.contains("presets") && j["presets"].is_array()) {
        t.presets = j["presets"].get<std::vector<std::string>>();
    }
    if (!t.reversible && !t.requiresConfirm) {
        return std::unexpected<std::string>(
            "Твик " + t.id + " должен быть обратимым либо иметь requiresConfirm");
    }
    const nlohmann::json* ops = nullptr;
    if (j.contains("ops") && j["ops"].is_array()) {
        ops = &j["ops"];
    } else if (j.contains("operations") && j["operations"].is_array()) {
        ops = &j["operations"];
    }
    if (ops) {
        for (const auto& item : *ops) {
            auto op = ParseOp(item);
            if (!op) {
                return std::unexpected(op.error());
            }
            t.ops.push_back(std::move(*op));
        }
    }
    return t;
}

#ifdef _WIN32

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    const auto z = s.find(L'\0');
    if (z != std::wstring_view::npos) {
        s = s.substr(0, z);
    }
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

HKEY RootKey(Hive hive) {
    return hive == Hive::Hkcu ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

std::string WinErr(std::string_view prefix, unsigned long code) {
    return std::string{prefix} + " (ошибка " + std::to_string(code) + ")";
}

constexpr REGSAM kRegSam64 = KEY_WOW64_64KEY;

struct ScmHandle {
    SC_HANDLE h{};
    explicit ScmHandle(DWORD access) { h = OpenSCManagerW(nullptr, nullptr, access); }
    ~ScmHandle() {
        if (h) CloseServiceHandle(h);
    }
    ScmHandle(const ScmHandle&) = delete;
    ScmHandle& operator=(const ScmHandle&) = delete;
};

struct SvcHandle {
    SC_HANDLE h{};
    SvcHandle(SC_HANDLE scm, const std::wstring& name, DWORD access) {
        h = OpenServiceW(scm, name.c_str(), access);
    }
    ~SvcHandle() {
        if (h) CloseServiceHandle(h);
    }
    SvcHandle(const SvcHandle&) = delete;
    SvcHandle& operator=(const SvcHandle&) = delete;
};

struct ComScope {
    bool ok{false};
    bool owned{false};
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            ok = true;
            owned = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            ok = true;
        }
    }
    ~ComScope() {
        if (owned) CoUninitialize();
    }
};

struct TaskRefs {
    ITaskService* svc{nullptr};
    ITaskFolder* folder{nullptr};
    IRegisteredTask* task{nullptr};
    ~TaskRefs() {
        if (task) task->Release();
        if (folder) folder->Release();
        if (svc) svc->Release();
    }
};

std::pair<std::wstring, std::wstring> SplitTaskPath(std::wstring path) {
    if (path.empty()) {
        return {L"\\", L""};
    }
    if (path.front() != L'\\') {
        path.insert(path.begin(), L'\\');
    }
    const auto slash = path.rfind(L'\\');
    if (slash == 0) {
        return {L"\\", path.substr(1)};
    }
    return {path.substr(0, slash), path.substr(slash + 1)};
}

std::expected<void, std::string> OpenRegisteredTask(TaskRefs& refs, const std::string& path) {
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                  reinterpret_cast<void**>(&refs.svc));
    if (FAILED(hr) || !refs.svc) {
        return std::unexpected<std::string>(
            "Не удалось создать ITaskService (HRESULT " + std::to_string(static_cast<long>(hr)) + ")");
    }
    VARIANT empty{};
    VariantInit(&empty);
    hr = refs.svc->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) {
        return std::unexpected<std::string>("Не удалось подключиться к планировщику задач");
    }
    const auto [folder, name] = SplitTaskPath(Utf8ToWide(path));
    BSTR bFolder = SysAllocString(folder.c_str());
    hr = refs.svc->GetFolder(bFolder, &refs.folder);
    SysFreeString(bFolder);
    if (FAILED(hr) || !refs.folder) {
        return std::unexpected<std::string>("Не найдена папка задачи: " + path);
    }
    BSTR bName = SysAllocString(name.c_str());
    hr = refs.folder->GetTask(bName, &refs.task);
    SysFreeString(bName);
    if (FAILED(hr) || !refs.task) {
        return std::unexpected<std::string>("Не найдена задача: " + path);
    }
    return {};
}

#endif // _WIN32

} // namespace

TweakEngine::TweakEngine() {
    undoPath_ = UndoStore::DefaultPath();
    if (!undo_.Load(undoPath_)) {
        spdlog::warn("Не удалось прочитать снимок отмены: {}", undoPath_.string());
    }
}

void TweakEngine::PersistUndo() const {
    if (undoPath_.empty()) {
        return;
    }
    if (!undo_.Save(undoPath_)) {
        spdlog::warn("Не удалось сохранить снимок отмены: {}", undoPath_.string());
    }
}

const std::vector<Tweak>& TweakEngine::All() const noexcept {
    return tweaks_;
}

UndoStore& TweakEngine::Undo() noexcept {
    return undo_;
}

const Tweak* TweakEngine::Find(std::string_view id) const {
    for (const auto& t : tweaks_) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

std::vector<const Tweak*> TweakEngine::ByTier(Tier tier) const {
    std::vector<const Tweak*> out;
    for (const auto& t : tweaks_) {
        if (t.tier == tier) {
            out.push_back(&t);
        }
    }
    return out;
}

std::vector<const Tweak*> TweakEngine::ByPreset(std::string_view presetId) const {
    std::vector<const Tweak*> out;
    const std::string id{presetId};
    for (const auto& t : tweaks_) {
        if (std::find(t.presets.begin(), t.presets.end(), id) != t.presets.end()) {
            out.push_back(&t);
        }
    }
    return out;
}

std::expected<void, std::string> TweakEngine::Load(const std::filesystem::path& catalogJson) {
    tweaks_.clear();
    try {
        std::ifstream in{catalogJson};
        if (!in) {
            return std::unexpected<std::string>("Не удалось открыть каталог твиков: " + catalogJson.string());
        }
        nlohmann::json doc;
        in >> doc;
        if (!doc.contains("tweaks") || !doc["tweaks"].is_array()) {
            return std::unexpected<std::string>("Каталог твиков не содержит массив tweaks");
        }
        for (const auto& item : doc["tweaks"]) {
            auto parsed = ParseTweak(item);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            tweaks_.push_back(std::move(*parsed));
        }
        spdlog::info("Загружено твиков: {} из {}", tweaks_.size(), catalogJson.string());
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected<std::string>(std::string{"Не удалось загрузить каталог твиков: "} + ex.what());
    }
}

std::optional<nlohmann::json> TweakEngine::QueryRegistry(Hive hive, const std::string& key,
                                                         const std::string& name) const {
#ifdef _WIN32
    const std::wstring wkey = Utf8ToWide(key);
    const std::wstring wname = Utf8ToWide(name);
    HKEY hk{};
    LONG st = RegOpenKeyExW(RootKey(hive), wkey.c_str(), 0, KEY_QUERY_VALUE | kRegSam64, &hk);
    if (st != ERROR_SUCCESS) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD size = 0;
    st = RegQueryValueExW(hk, wname.c_str(), nullptr, &type, nullptr, &size);
    if (st != ERROR_SUCCESS) {
        RegCloseKey(hk);
        return std::nullopt;
    }
    if (type == REG_DWORD) {
        DWORD value = 0;
        size = sizeof(value);
        st = RegQueryValueExW(hk, wname.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hk);
        if (st != ERROR_SUCCESS) {
            return std::nullopt;
        }
        return nlohmann::json(value);
    }
    std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
    DWORD nbytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
    st = RegQueryValueExW(hk, wname.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &nbytes);
    RegCloseKey(hk);
    if (st != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return nlohmann::json(WideToUtf8(buf.c_str()));
#else
    const auto it = memReg_.find(RegMapKey(hive, key, name));
    if (it == memReg_.end()) {
        return std::nullopt;
    }
    return it->second;
#endif
}

std::expected<void, std::string> TweakEngine::WriteRegistry(Hive hive, const std::string& key,
                                                            const std::string& name, RegType type,
                                                            const nlohmann::json& value, bool createKey) {
#ifdef _WIN32
    const std::wstring wkey = Utf8ToWide(key);
    const std::wstring wname = Utf8ToWide(name);
    HKEY hk{};
    DWORD disp = 0;
    LONG st = ERROR_SUCCESS;
    if (createKey) {
        st = RegCreateKeyExW(RootKey(hive), wkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                             KEY_SET_VALUE | kRegSam64, nullptr, &hk, &disp);
    } else {
        st = RegOpenKeyExW(RootKey(hive), wkey.c_str(), 0, KEY_SET_VALUE | kRegSam64, &hk);
    }
    if (st != ERROR_SUCCESS) {
        return std::unexpected(WinErr("Не удалось открыть ключ реестра " + key, static_cast<unsigned long>(st)));
    }
    if (type == RegType::Dword) {
        const auto dw = AsDword(value);
        if (!dw) {
            RegCloseKey(hk);
            return std::unexpected<std::string>("Некорректное DWORD-значение для " + key + "\\" + name);
        }
        const DWORD v = *dw;
        st = RegSetValueExW(hk, wname.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    } else {
        const std::string s = value.is_string() ? value.get<std::string>() : (value.is_null() ? std::string{} : value.dump());
        const std::wstring ws = Utf8ToWide(s);
        const DWORD bytes = static_cast<DWORD>((ws.size() + 1) * sizeof(wchar_t));
        st = RegSetValueExW(hk, wname.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(ws.c_str()), bytes);
    }
    RegCloseKey(hk);
    if (st != ERROR_SUCCESS) {
        return std::unexpected(WinErr("Не удалось записать реестр: " + key + "\\" + name,
                                      static_cast<unsigned long>(st)));
    }
    return {};
#else
    (void)type;
    (void)createKey;
    memReg_[RegMapKey(hive, key, name)] = value;
    return {};
#endif
}

std::expected<void, std::string> TweakEngine::DeleteRegistry(Hive hive, const std::string& key,
                                                             const std::string& name) {
#ifdef _WIN32
    const std::wstring wkey = Utf8ToWide(key);
    const std::wstring wname = Utf8ToWide(name);
    HKEY hk{};
    LONG st = RegOpenKeyExW(RootKey(hive), wkey.c_str(), 0, KEY_SET_VALUE | kRegSam64, &hk);
    if (st == ERROR_FILE_NOT_FOUND || st == ERROR_PATH_NOT_FOUND) {
        return {};
    }
    if (st != ERROR_SUCCESS) {
        return std::unexpected(WinErr("Не удалось открыть ключ реестра " + key, static_cast<unsigned long>(st)));
    }
    st = RegDeleteValueW(hk, wname.c_str());
    RegCloseKey(hk);
    if (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    return std::unexpected(WinErr("Не удалось удалить значение реестра: " + key + "\\" + name,
                                  static_cast<unsigned long>(st)));
#else
    memReg_.erase(RegMapKey(hive, key, name));
    return {};
#endif
}

std::optional<std::pair<StartType, bool>> TweakEngine::QueryServiceState(const std::string& name) const {
#ifdef _WIN32
    ScmHandle scm{SC_MANAGER_CONNECT};
    if (!scm.h) {
        return std::nullopt;
    }
    SvcHandle svc{scm.h, Utf8ToWide(name), SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS};
    if (!svc.h) {
        return std::nullopt;
    }
    DWORD bytes = 0;
    QueryServiceConfigW(svc.h, nullptr, 0, &bytes);
    if (bytes == 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> buf(bytes);
    auto* cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buf.data());
    if (!QueryServiceConfigW(svc.h, cfg, bytes, &bytes)) {
        return std::nullopt;
    }
    StartType start = StartType::Manual;
    switch (cfg->dwStartType) {
    case SERVICE_DISABLED:
        start = StartType::Disabled;
        break;
    case SERVICE_AUTO_START:
        start = StartType::Auto;
        break;
    default:
        start = StartType::Manual;
        break;
    }
    SERVICE_STATUS ss{};
    bool running = false;
    if (QueryServiceStatus(svc.h, &ss)) {
        running = (ss.dwCurrentState == SERVICE_RUNNING || ss.dwCurrentState == SERVICE_START_PENDING);
    }
    return std::pair{start, running};
#else
    const auto it = memSvc_.find(name);
    if (it == memSvc_.end()) {
        return std::nullopt;
    }
    return it->second;
#endif
}

std::expected<void, std::string> TweakEngine::WriteService(const std::string& name, StartType start, bool stop) {
#ifdef _WIN32
    DWORD winStart = SERVICE_DISABLED;
    if (start == StartType::Manual) {
        winStart = SERVICE_DEMAND_START;
    } else if (start == StartType::Auto) {
        winStart = SERVICE_AUTO_START;
    }
    ScmHandle scm{SC_MANAGER_CONNECT};
    if (!scm.h) {
        return std::unexpected(WinErr("Не удалось открыть диспетчер служб", GetLastError()));
    }
    SvcHandle svc{scm.h, Utf8ToWide(name),
                  SERVICE_CHANGE_CONFIG | SERVICE_STOP | SERVICE_QUERY_STATUS};
    if (!svc.h) {
        return std::unexpected(WinErr("Не удалось открыть службу: " + name, GetLastError()));
    }
    if (!ChangeServiceConfigW(svc.h, SERVICE_NO_CHANGE, winStart, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr)) {
        return std::unexpected(WinErr("Не удалось изменить службу: " + name, GetLastError()));
    }
    if (stop) {
        SERVICE_STATUS ss{};
        if (QueryServiceStatus(svc.h, &ss)
            && ss.dwCurrentState != SERVICE_STOPPED
            && ss.dwCurrentState != SERVICE_STOP_PENDING) {
            if (!ControlService(svc.h, SERVICE_CONTROL_STOP, &ss)) {
                const DWORD err = GetLastError();
                if (err != ERROR_SERVICE_NOT_ACTIVE && err != ERROR_SERVICE_CANNOT_ACCEPT_CTRL) {
                    return std::unexpected(WinErr("Не удалось остановить службу: " + name, err));
                }
            }
        }
    }
    return {};
#else
    auto& slot = memSvc_[name];
    slot.first = start;
    if (stop) {
        slot.second = false;
    }
    return {};
#endif
}

std::expected<void, std::string> TweakEngine::StartServiceIfNeeded(const std::string& name) {
#ifdef _WIN32
    ScmHandle scm{SC_MANAGER_CONNECT};
    if (!scm.h) {
        return std::unexpected(WinErr("Не удалось открыть диспетчер служб", GetLastError()));
    }
    SvcHandle svc{scm.h, Utf8ToWide(name), SERVICE_START | SERVICE_QUERY_STATUS};
    if (!svc.h) {
        return std::unexpected(WinErr("Не удалось открыть службу: " + name, GetLastError()));
    }
    SERVICE_STATUS ss{};
    if (QueryServiceStatus(svc.h, &ss) && ss.dwCurrentState == SERVICE_RUNNING) {
        return {};
    }
    if (!StartServiceW(svc.h, 0, nullptr)) {
        const DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            return {};
        }
        return std::unexpected(WinErr("Не удалось запустить службу: " + name, err));
    }
    return {};
#else
    auto& slot = memSvc_[name];
    slot.second = true;
    return {};
#endif
}

std::optional<bool> TweakEngine::QueryTaskEnabled(const std::string& path) const {
#ifdef _WIN32
    ComScope com;
    if (!com.ok) {
        return std::nullopt;
    }
    TaskRefs refs;
    if (!OpenRegisteredTask(refs, path)) {
        return std::nullopt;
    }
    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (FAILED(refs.task->get_Enabled(&enabled))) {
        return std::nullopt;
    }
    return enabled == VARIANT_TRUE;
#else
    const auto it = memTask_.find(path);
    if (it == memTask_.end()) {
        return std::nullopt;
    }
    return it->second;
#endif
}

std::expected<void, std::string> TweakEngine::WriteTaskEnabled(const std::string& path, bool enabled) {
#ifdef _WIN32
    ComScope com;
    if (!com.ok) {
        return std::unexpected<std::string>("Не удалось инициализировать COM для планировщика задач");
    }
    TaskRefs refs;
    auto opened = OpenRegisteredTask(refs, path);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    const HRESULT hr = refs.task->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
    if (FAILED(hr)) {
        return std::unexpected<std::string>("Не удалось изменить задачу: " + path);
    }
    return {};
#else
    memTask_[path] = enabled;
    return {};
#endif
}

bool TweakEngine::OpMatches(const Op& op) const {
    return std::visit([this](const auto& o) -> bool {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, RegistryOp>) {
            const auto cur = QueryRegistry(o.hive, o.key, o.name);
            return cur.has_value() && ValuesEqual(o.valueType, *cur, o.apply);
        } else if constexpr (std::is_same_v<T, ServiceOp>) {
            const auto cur = QueryServiceState(o.name);
            if (!cur) {
                return false;
            }
            if (cur->first != o.applyStartType) {
                return false;
            }
            if (o.stopOnApply && cur->second) {
                return false;
            }
            return true;
        } else if constexpr (std::is_same_v<T, TaskOp>) {
            const auto en = QueryTaskEnabled(o.path);
            if (!en) {
                return false;
            }
            const bool wantEnabled = (o.apply != "disabled");
            return *en == wantEnabled;
        }
        return false;
    }, op);
}

bool TweakEngine::IsApplied(std::string_view id) const {
    const Tweak* t = Find(id);
    if (!t || t->ops.empty()) {
        return false;
    }
    for (const auto& op : t->ops) {
        if (!OpMatches(op)) {
            return false;
        }
    }
    return true;
}

std::expected<void, std::string> TweakEngine::ApplyOp(std::string_view tweakId, const Op& op) {
    return std::visit([this, tweakId](const auto& o) -> std::expected<void, std::string> {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, RegistryOp>) {
            undo_.PutRegistry(tweakId, o.hive, o.key, o.name, QueryRegistry(o.hive, o.key, o.name));
            return WriteRegistry(o.hive, o.key, o.name, o.valueType, o.apply, o.createKey);
        } else if constexpr (std::is_same_v<T, ServiceOp>) {
            const auto cur = QueryServiceState(o.name);
            const StartType prev = cur ? cur->first : StartType::Auto;
            const bool running = cur ? cur->second : false;
            undo_.PutService(tweakId, o.name, prev, running);
            return WriteService(o.name, o.applyStartType, o.stopOnApply);
        } else if constexpr (std::is_same_v<T, TaskOp>) {
            const auto en = QueryTaskEnabled(o.path);
            undo_.PutTask(tweakId, o.path, en.value_or(true));
            const bool enable = (o.apply != "disabled");
            return WriteTaskEnabled(o.path, enable);
        }
        return std::unexpected<std::string>("Неизвестная операция");
    }, op);
}

std::expected<void, std::string> TweakEngine::UndoEntry(const nlohmann::json& entry) {
    const auto kind = entry.value("kind", std::string{});
    if (kind == "registry") {
        const Hive hive = ParseHive(entry.value("hive", "HKLM"));
        const auto key = entry.value("key", std::string{});
        const auto name = entry.value("name", std::string{});
        if (!entry.contains("previous") || entry["previous"].is_null()) {
            return DeleteRegistry(hive, key, name);
        }
        const RegType type = entry["previous"].is_string() ? RegType::Sz : RegType::Dword;
        return WriteRegistry(hive, key, name, type, entry["previous"], true);
    }
    if (kind == "service") {
        const auto name = entry.value("name", std::string{});
        const auto prev = ParseStartType(entry.value("previous", "manual"));
        const bool wasRunning = entry.value("wasRunning", false);
        auto changed = WriteService(name, prev, /*stop=*/false);
        if (!changed) {
            return changed;
        }
        if (wasRunning) {
            return StartServiceIfNeeded(name);
        }
        return {};
    }
    if (kind == "task") {
        return WriteTaskEnabled(entry.value("path", std::string{}), entry.value("wasEnabled", true));
    }
    return std::unexpected<std::string>("Неизвестная запись снимка");
}

std::expected<void, std::string> TweakEngine::Apply(std::string_view id, const ApplyOptions& opt) {
    try {
        const Tweak* t = Find(id);
        if (!t) {
            return std::unexpected<std::string>("Твик не найден: " + std::string{id});
        }
        if (t->requiresConfirm && !opt.confirmed) {
            return std::unexpected<std::string>("Нужно подтверждение для опасного твика: " + t->title);
        }
        if (opt.createRestorePoint) {
            const auto rp = RestorePoint::Create(kRestorePointDesc);
            if (!rp) {
                spdlog::warn("{}", rp.error());
            }
        }

        undo_.Clear(id);
        for (const auto& op : t->ops) {
            auto result = ApplyOp(id, op);
            PersistUndo();
            if (!result) {
                spdlog::error("Ошибка применения {}: {}", t->id, result.error());
                return result;
            }
        }
        PersistUndo();
        spdlog::info("Твик применён: {}", t->id);
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected<std::string>(std::string{"Ошибка применения твика: "} + ex.what());
    }
}

std::expected<void, std::string> TweakEngine::ApplyMany(std::span<const std::string> ids, const ApplyOptions& opt) {
    try {
        if (ids.empty()) {
            return {};
        }
        for (const auto& id : ids) {
            const Tweak* t = Find(id);
            if (!t) {
                return std::unexpected<std::string>("Твик не найден: " + id);
            }
            if (t->requiresConfirm && !opt.confirmed) {
                return std::unexpected<std::string>("Нужно подтверждение для опасного твика: " + t->title);
            }
        }
        if (opt.createRestorePoint) {
            const auto rp = RestorePoint::Create(kRestorePointDesc);
            if (!rp) {
                spdlog::warn("{}", rp.error());
            }
        }
        ApplyOptions inner = opt;
        inner.createRestorePoint = false;
        inner.confirmed = true;
        for (const auto& id : ids) {
            auto result = Apply(id, inner);
            if (!result) {
                return result;
            }
        }
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected<std::string>(std::string{"Ошибка применения пакета твиков: "} + ex.what());
    }
}

std::expected<void, std::string> TweakEngine::ApplyEnabled(
    const std::unordered_map<std::string, bool>& enabled, const ApplyOptions& opt) {
    std::vector<std::string> ids;
    ids.reserve(enabled.size());
    for (const auto& [id, on] : enabled) {
        if (on) {
            ids.push_back(id);
        }
    }
    return ApplyMany(ids, opt);
}

std::expected<void, std::string> TweakEngine::Undo(std::string_view id) {
    try {
        const std::string sid{id};
        if (!undo_.CanUndo(id)) {
            return std::unexpected<std::string>("Нет снимка для отмены твика: " + sid);
        }
        nlohmann::json ops = nlohmann::json::array();
        const auto& raw = undo_.Raw();
        if (raw.contains("tweaks") && raw["tweaks"].contains(sid) && raw["tweaks"][sid].contains("ops")) {
            ops = raw["tweaks"][sid]["ops"];
        }
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            auto result = UndoEntry(*it);
            if (!result) {
                spdlog::error("Ошибка отмены {}: {}", sid, result.error());
                PersistUndo();
                return result;
            }
        }
        undo_.Clear(id);
        PersistUndo();
        spdlog::info("Твик отменён: {}", sid);
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected<std::string>(std::string{"Ошибка отмены твика: "} + ex.what());
    }
}

} // namespace yeet17::tweaks
