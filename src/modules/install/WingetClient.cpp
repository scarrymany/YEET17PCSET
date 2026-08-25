#include "pch.h"
#include "modules/install/WingetClient.h"
#include "core/Localization.h"
#include "core/Logger.h"
#include "core/Strings.h"

#include <cctype>

#ifdef _WIN32
#    include <windows.h>
#endif

namespace yeet17::install {
namespace {

constexpr std::string_view kBusyError = "Уже выполняется операция winget";
constexpr std::string_view kWindowsOnly = "winget доступен только в Windows";
constexpr std::string_view kEmptyId = "Не указан идентификатор пакета";
constexpr std::string_view kEmptyQuery = "Пустой поисковый запрос";
constexpr std::string_view kChocoMissing = "choco.exe не найден (Chocolatey не установлен)";
constexpr std::string_view kStartFailed = "Не удалось запустить процесс";

std::string NormalizeSource(std::string_view source) {
    std::string out{source};
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (out == "choco" || out == "chocolatey") {
        return "choco";
    }
    if (out.empty() || out == "winget") {
        return "winget";
    }
    return {};
}

std::string_view ActionTitle(PackageAction action) {
    switch (action) {
    case PackageAction::Install:
        return "установка";
    case PackageAction::Upgrade:
        return "обновление";
    case PackageAction::Uninstall:
        return "удаление";
    }
    return "операция";
}

int ParsePercent(std::string_view line) {
    int last = -1;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '%') {
            continue;
        }
        std::size_t end = i;
        while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
            --end;
        }
        std::size_t start = end;
        while (start > 0 && std::isdigit(static_cast<unsigned char>(line[start - 1]))) {
            --start;
        }
        if (start == end) {
            continue;
        }
        int value = 0;
        for (std::size_t k = start; k < end; ++k) {
            value = value * 10 + (line[k] - '0');
        }
        if (value >= 0 && value <= 100) {
            last = value;
        }
    }
    return last;
}

std::string TrimCopy(std::string_view in) {
    std::size_t a = 0;
    std::size_t b = in.size();
    while (a < b && (in[a] == ' ' || in[a] == '\t' || in[a] == '\r')) {
        ++a;
    }
    while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\r')) {
        --b;
    }
    return std::string{in.substr(a, b - a)};
}

bool LooksLikeRule(std::string_view line) {
    std::size_t dashes = 0;
    std::size_t other = 0;
    for (char ch : line) {
        if (ch == '-' || ch == '=' || ch == static_cast<char>(0xC4)) {
            ++dashes;
        } else if (ch != ' ' && ch != '\t' && ch != '\r') {
            ++other;
        }
    }
    return dashes >= 8 && other == 0;
}

bool LooksLikeHeader(std::string_view line) {
    return line.find("Name") != std::string_view::npos && line.find("Id") != std::string_view::npos;
}

struct Column {
    std::string name;
    std::size_t start = 0;
};

std::vector<Column> ParseHeaderColumns(std::string_view header) {
    std::vector<Column> cols;
    std::size_t i = 0;
    while (i < header.size()) {
        while (i < header.size() && (header[i] == ' ' || header[i] == '\t')) {
            ++i;
        }
        if (i >= header.size()) {
            break;
        }
        const std::size_t start = i;
        while (i < header.size() && header[i] != ' ' && header[i] != '\t') {
            ++i;
        }
        cols.push_back(Column{std::string{header.substr(start, i - start)}, start});
    }
    return cols;
}

std::string Cell(const std::string& line, const std::vector<Column>& cols, std::size_t index) {
    if (index >= cols.size()) {
        return {};
    }
    const std::size_t start = cols[index].start;
    const std::size_t end = (index + 1 < cols.size()) ? cols[index + 1].start : line.size();
    if (start >= line.size()) {
        return {};
    }
    return TrimCopy(std::string_view{line}.substr(start, end > start ? end - start : 0));
}

std::size_t FindColumn(const std::vector<Column>& cols, std::string_view name) {
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (cols[i].name == name) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

std::vector<std::string> SplitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        if (current.back() == '\r') {
            current.pop_back();
        }
        lines.push_back(std::move(current));
    }
    return lines;
}

#ifdef _WIN32
std::wstring QuoteWinArg(std::wstring_view arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    const bool need = arg.find_first_of(L" \t\"") != std::wstring_view::npos;
    if (!need) {
        return std::wstring{arg};
    }
    std::wstring out;
    out.push_back(L'"');
    int slashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        if (ch == L'"') {
            out.append(static_cast<std::size_t>(slashes * 2 + 1), L'\\');
            out.push_back(L'"');
            slashes = 0;
            continue;
        }
        if (slashes > 0) {
            out.append(static_cast<std::size_t>(slashes), L'\\');
            slashes = 0;
        }
        out.push_back(ch);
    }
    if (slashes > 0) {
        out.append(static_cast<std::size_t>(slashes * 2), L'\\');
    }
    out.push_back(L'"');
    return out;
}

std::filesystem::path LocateOnPath(const wchar_t* exeName) {
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, exeName, nullptr, MAX_PATH, found, nullptr) > 0) {
        return std::filesystem::path{found};
    }
    return {};
}

std::filesystem::path LocateWinget() {
    if (auto fromPath = LocateOnPath(L"winget.exe"); !fromPath.empty()) {
        return fromPath;
    }
    wchar_t local[32768]{};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (n > 0 && n < 32768) {
        auto candidate = std::filesystem::path{local} / L"Microsoft" / L"WindowsApps" / L"winget.exe";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path LocateChoco() {
    return LocateOnPath(L"choco.exe");
}
#endif

void Emit(const ProgressCallback& cb, ProgressEvent event) {
    if (cb) {
        cb(event);
    }
}

} // namespace

WingetClient::WingetClient() {
#ifdef _WIN32
    wingetExe_ = LocateWinget();
    chocoExe_ = LocateChoco();
#endif
}

WingetClient::~WingetClient() {
    Cancel();
    std::lock_guard lock{mutex_};
}

bool WingetClient::Available() const {
#ifdef _WIN32
    std::error_code ec;
    if (!wingetExe_.empty() && std::filesystem::exists(wingetExe_, ec)) {
        return true;
    }
    return !LocateWinget().empty();
#else
    return false;
#endif
}

std::filesystem::path WingetClient::Executable() const {
#ifdef _WIN32
    std::error_code ec;
    if (!wingetExe_.empty() && std::filesystem::exists(wingetExe_, ec)) {
        return wingetExe_;
    }
    return LocateWinget();
#else
    return {};
#endif
}

bool WingetClient::ChocolateyAvailable() const {
#ifdef _WIN32
    std::error_code ec;
    if (!chocoExe_.empty() && std::filesystem::exists(chocoExe_, ec)) {
        return true;
    }
    return !LocateChoco().empty();
#else
    return false;
#endif
}

bool WingetClient::Busy() const {
    return busy_.load(std::memory_order_acquire);
}

void WingetClient::Cancel() {
#ifdef _WIN32
    std::lock_guard lock{childMutex_};
    if (childProcess_) {
        TerminateProcess(static_cast<HANDLE>(childProcess_), 1);
        yeet17::core::Logger::Instance().Warn("Операция прервана");
    }
#endif
}

#ifdef _WIN32
void WingetClient::AttachChild(void* process) {
    std::lock_guard lock{childMutex_};
    childProcess_ = process;
}

void WingetClient::DetachChild() {
    std::lock_guard lock{childMutex_};
    childProcess_ = nullptr;
}

WingetClient::ProcessOutcome WingetClient::Launch(const std::filesystem::path& exe,
                                                  const std::vector<std::wstring>& args,
                                                  std::string_view packageId,
                                                  PackageAction action,
                                                  const ProgressCallback& onProgress) {
    ProcessOutcome result;

    std::wstring command = QuoteWinArg(exe.wstring());
    for (const auto& arg : args) {
        command += L' ';
        command += QuoteWinArg(arg);
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
        yeet17::core::Logger::Instance().Error("Не удалось создать каналы для процесса");
        return result;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    si.hStdInput = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = command;
    const BOOL ok = CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                                   &si, &pi);
    CloseHandle(outWrite);

    if (!ok) {
        CloseHandle(outRead);
        yeet17::core::Logger::Instance().Error(std::string{kStartFailed});
        return result;
    }

    result.started = true;
    AttachChild(pi.hProcess);

    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    int percent = -1;
    while (ReadFile(outRead, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        pending.append(buffer, buffer + read);
        result.output.append(buffer, buffer + read);
        std::size_t pos = 0;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (const int parsed = ParsePercent(line); parsed >= 0) {
                percent = parsed;
            }
            yeet17::core::Logger::Instance().Debug(line);
            Emit(onProgress, ProgressEvent{
                .packageId = std::string{packageId},
                .action = action,
                .percent = percent,
                .line = line,
            });
        }
    }
    if (!pending.empty()) {
        if (pending.back() == '\r') {
            pending.pop_back();
        }
        if (const int parsed = ParsePercent(pending); parsed >= 0) {
            percent = parsed;
        }
        yeet17::core::Logger::Instance().Debug(pending);
        Emit(onProgress, ProgressEvent{
            .packageId = std::string{packageId},
            .action = action,
            .percent = percent,
            .line = pending,
        });
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exitCode = static_cast<int>(code);

    DetachChild();
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(outRead);
    return result;
}
#endif

std::expected<void, std::string> WingetClient::RunAction(PackageAction action, std::string_view id,
                                                         std::string_view source,
                                                         ProgressCallback onProgress, bool takeLock) {
    std::unique_lock<std::mutex> lock{mutex_, std::defer_lock};
    if (takeLock) {
        if (!lock.try_lock() || busy_.load(std::memory_order_acquire)) {
            return std::unexpected(std::string{kBusyError});
        }
        busy_.store(true, std::memory_order_release);
    }

    struct BusyGuard {
        std::atomic<bool>* flag = nullptr;
        ~BusyGuard() {
            if (flag) {
                flag->store(false, std::memory_order_release);
            }
        }
    } busyGuard{takeLock ? &busy_ : nullptr};

    if (id.empty()) {
        return std::unexpected(std::string{kEmptyId});
    }

#ifndef _WIN32
    (void)action;
    (void)source;
    (void)onProgress;
    return std::unexpected(std::string{kWindowsOnly});
#else
    const auto src = NormalizeSource(source);
    if (src.empty()) {
        return std::unexpected(std::string{"Неизвестный источник пакета"});
    }

    std::filesystem::path exe;
    std::vector<std::wstring> args;
    const auto wideId = yeet17::core::Utf8ToWide(id);

    if (src == "choco") {
        exe = ChocolateyAvailable() ? (chocoExe_.empty() ? LocateChoco() : chocoExe_) : std::filesystem::path{};
        if (exe.empty()) {
            yeet17::core::Logger::Instance().Error(std::string{kChocoMissing});
            return std::unexpected(std::string{kChocoMissing});
        }
        switch (action) {
        case PackageAction::Install:
            args = {L"install", L"-y", wideId};
            break;
        case PackageAction::Upgrade:
            args = {L"upgrade", L"-y", wideId};
            break;
        case PackageAction::Uninstall:
            args = {L"uninstall", L"-y", wideId};
            break;
        }
    } else {
        exe = Executable();
        if (exe.empty()) {
            yeet17::core::Logger::Instance().Error(std::string{yeet17::core::Strings::WingetMissing});
            return std::unexpected(std::string{yeet17::core::Strings::WingetMissing});
        }
        switch (action) {
        case PackageAction::Install:
            args = {L"install", L"--id", wideId, L"--exact", L"--accept-package-agreements",
                    L"--accept-source-agreements", L"--disable-interactivity", L"--source", L"winget"};
            break;
        case PackageAction::Upgrade:
            args = {L"upgrade", L"--id", wideId, L"--exact", L"--accept-package-agreements",
                    L"--accept-source-agreements", L"--disable-interactivity"};
            break;
        case PackageAction::Uninstall:
            args = {L"uninstall", L"--id", wideId, L"--exact", L"--disable-interactivity"};
            break;
        }
    }

    yeet17::core::Logger::Instance().Info(
        std::string{"Запуск "} + (src == "choco" ? "Chocolatey" : "winget") + ": "
        + std::string{ActionTitle(action)} + " " + std::string{id});

    Emit(onProgress, ProgressEvent{
        .packageId = std::string{id},
        .action = action,
        .percent = 0,
        .line = std::string{ActionTitle(action)} + " " + std::string{id},
    });

    const auto outcome = Launch(exe, args, id, action, onProgress);
    if (!outcome.started) {
        Emit(onProgress, ProgressEvent{
            .packageId = std::string{id},
            .action = action,
            .percent = -1,
            .line = std::string{kStartFailed},
            .finished = true,
            .success = false,
            .exitCode = -1,
        });
        return std::unexpected(std::string{kStartFailed});
    }

    const bool ok = outcome.exitCode == 0;
    if (ok) {
        yeet17::core::Logger::Instance().Info(
            std::string{"Готово: "} + std::string{ActionTitle(action)} + " " + std::string{id});
    } else {
        yeet17::core::Logger::Instance().Error(
            std::string{"Ошибка "} + std::string{ActionTitle(action)} + " " + std::string{id}
            + " (код " + std::to_string(outcome.exitCode) + ")");
    }

    Emit(onProgress, ProgressEvent{
        .packageId = std::string{id},
        .action = action,
        .percent = ok ? 100 : -1,
        .line = ok ? std::string{"Готово"} : (std::string{"Ошибка (код "} + std::to_string(outcome.exitCode) + ")"),
        .finished = true,
        .success = ok,
        .exitCode = outcome.exitCode,
    });

    if (!ok) {
        return std::unexpected(std::string{"Ошибка "} + std::string{ActionTitle(action)} + " "
                               + std::string{id} + " (код " + std::to_string(outcome.exitCode) + ")");
    }
    return {};
#endif
}

std::expected<void, std::string> WingetClient::Install(std::string_view id, ProgressCallback onProgress) {
    return RunAction(PackageAction::Install, id, "winget", std::move(onProgress), true);
}

std::expected<void, std::string> WingetClient::Upgrade(std::string_view id, ProgressCallback onProgress) {
    return RunAction(PackageAction::Upgrade, id, "winget", std::move(onProgress), true);
}

std::expected<void, std::string> WingetClient::Uninstall(std::string_view id, ProgressCallback onProgress) {
    return RunAction(PackageAction::Uninstall, id, "winget", std::move(onProgress), true);
}

std::expected<void, std::string> WingetClient::Install(std::string_view id, std::string_view source,
                                                       ProgressCallback onProgress) {
    return RunAction(PackageAction::Install, id, source, std::move(onProgress), true);
}

std::expected<void, std::string> WingetClient::Upgrade(std::string_view id, std::string_view source,
                                                       ProgressCallback onProgress) {
    return RunAction(PackageAction::Upgrade, id, source, std::move(onProgress), true);
}

std::expected<void, std::string> WingetClient::Uninstall(std::string_view id, std::string_view source,
                                                         ProgressCallback onProgress) {
    return RunAction(PackageAction::Uninstall, id, source, std::move(onProgress), true);
}

std::expected<void, std::string> WingetClient::RunBulk(
    const std::vector<std::pair<PackageAction, std::string>>& jobs, ProgressCallback onProgress) {
    std::vector<PackageJob> typed;
    typed.reserve(jobs.size());
    for (const auto& [action, id] : jobs) {
        typed.push_back(PackageJob{.action = action, .id = id, .source = "winget"});
    }
    return RunBulk(typed, std::move(onProgress));
}

std::expected<void, std::string> WingetClient::RunBulk(const std::vector<PackageJob>& jobs,
                                                       ProgressCallback onProgress) {
    std::unique_lock lock{mutex_, std::try_to_lock};
    if (!lock.owns_lock() || busy_.load(std::memory_order_acquire)) {
        return std::unexpected(std::string{kBusyError});
    }
    busy_.store(true, std::memory_order_release);
    struct BusyGuard {
        std::atomic<bool>& flag;
        ~BusyGuard() { flag.store(false, std::memory_order_release); }
    } busyGuard{busy_};

    int failed = 0;
    for (const auto& job : jobs) {
        auto outcome = RunAction(job.action, job.id, job.source, onProgress, false);
        if (!outcome) {
            ++failed;
        }
    }
    if (failed > 0) {
        const auto message = "Не удалось обработать " + std::to_string(failed) + " из "
                             + std::to_string(jobs.size()) + " пакетов";
        yeet17::core::Logger::Instance().Error(message);
        return std::unexpected(message);
    }
    yeet17::core::Logger::Instance().Info("Пакетная операция завершена");
    return {};
}

std::expected<std::vector<InstalledPackage>, std::string> WingetClient::ListInstalled() {
    std::unique_lock lock{mutex_, std::try_to_lock};
    if (!lock.owns_lock() || busy_.load(std::memory_order_acquire)) {
        return std::unexpected(std::string{kBusyError});
    }
    busy_.store(true, std::memory_order_release);
    struct BusyGuard {
        std::atomic<bool>& flag;
        ~BusyGuard() { flag.store(false, std::memory_order_release); }
    } busyGuard{busy_};

#ifndef _WIN32
    return std::unexpected(std::string{kWindowsOnly});
#else
    const auto exe = Executable();
    if (exe.empty()) {
        yeet17::core::Logger::Instance().Error(std::string{yeet17::core::Strings::WingetMissing});
        return std::unexpected(std::string{yeet17::core::Strings::WingetMissing});
    }

    yeet17::core::Logger::Instance().Info("Запрос списка установленных пакетов");
    const auto outcome = Launch(exe, {L"list", L"--disable-interactivity"}, {}, PackageAction::Install, {});
    if (!outcome.started) {
        return std::unexpected(std::string{kStartFailed});
    }
    if (outcome.exitCode != 0) {
        const auto message = "Не удалось получить список установленных (код "
                             + std::to_string(outcome.exitCode) + ")";
        yeet17::core::Logger::Instance().Error(message);
        return std::unexpected(message);
    }

    std::vector<InstalledPackage> installed;
    const auto lines = SplitLines(outcome.output);
    std::vector<Column> cols;
    bool seenHeader = false;
    for (const auto& line : lines) {
        if (TrimCopy(line).empty()) {
            continue;
        }
        if (!seenHeader) {
            if (LooksLikeHeader(line)) {
                cols = ParseHeaderColumns(line);
                seenHeader = true;
            }
            continue;
        }
        if (LooksLikeRule(line)) {
            continue;
        }
        if (cols.empty()) {
            continue;
        }
        const auto idIdx = FindColumn(cols, "Id");
        const auto nameIdx = FindColumn(cols, "Name");
        const auto verIdx = FindColumn(cols, "Version");
        const auto availIdx = FindColumn(cols, "Available");
        InstalledPackage row;
        if (idIdx != static_cast<std::size_t>(-1)) {
            row.id = Cell(line, cols, idIdx);
        }
        if (nameIdx != static_cast<std::size_t>(-1)) {
            row.name = Cell(line, cols, nameIdx);
        }
        if (verIdx != static_cast<std::size_t>(-1)) {
            row.version = Cell(line, cols, verIdx);
        }
        if (availIdx != static_cast<std::size_t>(-1)) {
            row.available = Cell(line, cols, availIdx);
        }
        if (row.id.empty()) {
            continue;
        }
        installed.push_back(std::move(row));
    }

    if (!seenHeader) {
        yeet17::core::Logger::Instance().Warn("Не удалось разобрать таблицу установленных пакетов");
        return std::vector<InstalledPackage>{};
    }

    yeet17::core::Logger::Instance().Info(
        "Установлено пакетов (по winget): " + std::to_string(installed.size()));
    return installed;
#endif
}

std::expected<std::vector<Package>, std::string> WingetClient::SearchRemote(std::string_view query) {
    if (query.empty()) {
        return std::unexpected(std::string{kEmptyQuery});
    }

    std::unique_lock lock{mutex_, std::try_to_lock};
    if (!lock.owns_lock() || busy_.load(std::memory_order_acquire)) {
        return std::unexpected(std::string{kBusyError});
    }
    busy_.store(true, std::memory_order_release);
    struct BusyGuard {
        std::atomic<bool>& flag;
        ~BusyGuard() { flag.store(false, std::memory_order_release); }
    } busyGuard{busy_};

#ifndef _WIN32
    return std::unexpected(std::string{kWindowsOnly});
#else
    const auto exe = Executable();
    if (exe.empty()) {
        yeet17::core::Logger::Instance().Error(std::string{yeet17::core::Strings::WingetMissing});
        return std::unexpected(std::string{yeet17::core::Strings::WingetMissing});
    }

    yeet17::core::Logger::Instance().Info("Поиск в источнике winget: " + std::string{query});
    const auto outcome = Launch(exe,
                                {L"search", yeet17::core::Utf8ToWide(query), L"--disable-interactivity",
                                 L"--source", L"winget"},
                                {}, PackageAction::Install, {});
    if (!outcome.started) {
        return std::unexpected(std::string{kStartFailed});
    }
    if (outcome.exitCode != 0) {
        const auto message = "Поиск winget завершился с кодом " + std::to_string(outcome.exitCode);
        yeet17::core::Logger::Instance().Error(message);
        return std::unexpected(message);
    }

    std::vector<Package> found;
    const auto lines = SplitLines(outcome.output);
    std::vector<Column> cols;
    bool seenHeader = false;
    for (const auto& line : lines) {
        if (TrimCopy(line).empty()) {
            continue;
        }
        if (!seenHeader) {
            if (LooksLikeHeader(line)) {
                cols = ParseHeaderColumns(line);
                seenHeader = true;
            }
            continue;
        }
        if (LooksLikeRule(line)) {
            continue;
        }
        const auto idIdx = FindColumn(cols, "Id");
        const auto nameIdx = FindColumn(cols, "Name");
        Package package;
        if (idIdx != static_cast<std::size_t>(-1)) {
            package.id = Cell(line, cols, idIdx);
        }
        if (nameIdx != static_cast<std::size_t>(-1)) {
            package.name = Cell(line, cols, nameIdx);
        }
        package.source = "winget";
        package.custom = false;
        if (package.id.empty()) {
            continue;
        }
        if (package.name.empty()) {
            package.name = package.id;
        }
        found.push_back(std::move(package));
    }

    if (!seenHeader) {
        yeet17::core::Logger::Instance().Warn("Не удалось разобрать таблицу поиска winget");
        return std::vector<Package>{};
    }

    yeet17::core::Logger::Instance().Info("Найдено пакетов: " + std::to_string(found.size()));
    return found;
#endif
}


namespace {
bool IsWingetIdChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '_';
}
} // namespace

std::vector<std::string> WingetClient::ParseListedIds(std::string_view stdoutText,
                                                      const std::vector<Package>& catalog) {
    std::vector<std::string> ids;
    if (stdoutText.empty() || catalog.empty()) {
        return ids;
    }
    for (const auto& package : catalog) {
        const auto& key = package.id;
        if (key.empty()) {
            continue;
        }
        std::size_t pos = 0;
        while ((pos = stdoutText.find(key, pos)) != std::string_view::npos) {
            const bool leftOk = pos == 0 || !IsWingetIdChar(stdoutText[pos - 1]);
            const auto endPos = pos + key.size();
            const bool rightOk = endPos == stdoutText.size() || !IsWingetIdChar(stdoutText[endPos]);
            if (leftOk && rightOk) {
                ids.push_back(package.id);
                break;
            }
            pos = endPos;
        }
    }
    return ids;
}

} // namespace yeet17::install
