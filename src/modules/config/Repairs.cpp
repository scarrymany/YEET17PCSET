#include "pch.h"
#include "modules/config/Repairs.h"
#include "core/Logger.h"
#include "core/Localization.h"

#ifdef _WIN32
#    include <windows.h>
#endif

namespace yeet17::config {
namespace {

void Emit(const Repairs::LogFn& log, std::string_view message) {
    yeet17::core::Logger::Instance().Info(std::string{message});
    if (log) {
        log(message);
    }
}

#ifdef _WIN32

[[nodiscard]] std::wstring System32File(const wchar_t* name) {
    wchar_t systemDir[MAX_PATH]{};
    const auto n = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return name;
    }
    return (std::filesystem::path{systemDir} / name).wstring();
}

[[nodiscard]] std::string NormalizeCapturedText(std::string raw) {
    if (raw.size() >= 2) {
        const auto b0 = static_cast<unsigned char>(raw[0]);
        const auto b1 = static_cast<unsigned char>(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) {
            const auto* wide = reinterpret_cast<const wchar_t*>(raw.data() + 2);
            const auto count = (raw.size() - 2) / sizeof(wchar_t);
            return yeet17::core::WideToUtf8(std::wstring_view{wide, count});
        }
    }
    return raw;
}

// Run one command via cmd.exe /c. Streams output lines to log.
// netStopOk: treat "net stop" exit 2 (service not started) as success.
bool RunStep(std::wstring commandLine, const Repairs::LogFn& log, std::string& error, bool netStopOk) {
    const auto cmd = System32File(L"cmd.exe");
    std::wstring full = L"\"" + cmd + L"\" /c " + commandLine;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
        error = "Не удалось создать канал вывода";
        return false;
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(
        cmd.c_str(), full.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(outWrite);

    if (!started) {
        CloseHandle(outRead);
        error = "Не удалось запустить команду (код " + std::to_string(GetLastError()) + ")";
        Emit(log, error);
        return false;
    }

    std::string raw;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(outRead, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        raw.append(buffer, buffer + read);
    }
    CloseHandle(outRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    const auto text = NormalizeCapturedText(std::move(raw));
    std::string line;
    for (char ch : text) {
        if (ch == '\n') {
            if (const auto trimmed = std::string_view{line}; !trimmed.empty()) {
                Emit(log, trimmed);
            }
            line.clear();
        } else if (ch != '\r') {
            line.push_back(ch);
        }
    }
    if (!line.empty()) {
        Emit(log, line);
    }

    const bool ok = exitCode == 0 || (netStopOk && exitCode == 2);
    if (!ok) {
        error = "Команда завершилась с кодом " + std::to_string(static_cast<int>(exitCode));
        Emit(log, error);
        return false;
    }
    return true;
}

bool RunSteps(const std::vector<std::pair<std::string, std::wstring>>& steps,
              const Repairs::LogFn& log,
              std::string& error) {
    error.clear();
    for (const auto& [title, command] : steps) {
        Emit(log, title);
        const bool netStopOk = command.rfind(L"net stop ", 0) == 0;
        if (!RunStep(command, log, error, netStopOk)) {
            return false;
        }
    }
    return true;
}

#endif

} // namespace

bool Repairs::ResetNetwork(LogFn log, std::string& error) {
#ifdef _WIN32
    return RunSteps(
        {
            {"Сброс Winsock", L"netsh winsock reset"},
            {"Сброс IP", L"netsh int ip reset"},
        },
        log, error);
#else
    (void)log;
    error = "Сброс сети доступен только в Windows.";
    yeet17::core::Logger::Instance().Warn(error);
    return false;
#endif
}

bool Repairs::ResetWindowsUpdate(LogFn log, std::string& error) {
#ifdef _WIN32
    return RunSteps(
        {
            {"Остановка wuauserv", L"net stop wuauserv"},
            {"Остановка BITS", L"net stop bits"},
            {"Остановка CryptSvc", L"net stop cryptSvc"},
            {"Остановка msiserver", L"net stop msiserver"},
            {"Удаление старого SoftwareDistribution",
             L"cmd.exe /c if exist %SystemRoot%\\SoftwareDistribution.old rmdir /s /q %SystemRoot%\\SoftwareDistribution.old"},
            {"Переименование SoftwareDistribution",
             L"cmd.exe /c if exist %SystemRoot%\\SoftwareDistribution ren %SystemRoot%\\SoftwareDistribution SoftwareDistribution.old"},
            {"Удаление старого catroot2",
             L"cmd.exe /c if exist %SystemRoot%\\System32\\catroot2.old rmdir /s /q %SystemRoot%\\System32\\catroot2.old"},
            {"Переименование catroot2",
             L"cmd.exe /c if exist %SystemRoot%\\System32\\catroot2 ren %SystemRoot%\\System32\\catroot2 catroot2.old"},
            {"Запуск wuauserv", L"net start wuauserv"},
            {"Запуск BITS", L"net start bits"},
            {"Запуск CryptSvc", L"net start cryptSvc"},
            {"Запуск msiserver", L"net start msiserver"},
        },
        log, error);
#else
    (void)log;
    error = "Сброс Windows Update доступен только в Windows.";
    yeet17::core::Logger::Instance().Warn(error);
    return false;
#endif
}

bool Repairs::RepairSystemImage(LogFn log, std::string& error) {
#ifdef _WIN32
    if (!RunSteps({{"DISM RestoreHealth", L"DISM.exe /Online /Cleanup-Image /RestoreHealth"}}, log, error)) {
        return false;
    }
    return RunSteps({{"SFC /scannow", L"sfc.exe /scannow"}}, log, error);
#else
    (void)log;
    error = "SFC + DISM доступны только в Windows.";
    yeet17::core::Logger::Instance().Warn(error);
    return false;
#endif
}

bool Repairs::RepairWinget(LogFn log, std::string& error) {
#ifdef _WIN32
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"winget.exe", nullptr, MAX_PATH, found, nullptr) == 0) {
        error = "winget.exe не найден. Установите «Установщик приложений» из Microsoft Store.";
        Emit(log, error);
        return false;
    }
    const std::wstring winget{found};
    return RunSteps(
        {
            {"Сброс источников Winget", L"\"" + winget + L"\" source reset --force"},
            {"Обновление источников Winget", L"\"" + winget + L"\" source update"},
        },
        log, error);
#else
    (void)log;
    error = "Восстановление Winget доступно только в Windows.";
    yeet17::core::Logger::Instance().Warn(error);
    return false;
#endif
}

} // namespace yeet17::config
