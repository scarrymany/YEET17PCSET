#include "pch.h"
#include "modules/config/Features.h"
#include "core/Logger.h"
#include "core/Localization.h"

#ifdef _WIN32
#    include <windows.h>
#endif

namespace yeet17::config {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

[[nodiscard]] std::string AsciiLower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        out.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch));
    }
    return out;
}

// Enabled / Enable Pending → true. Disabled / Disable Pending → false.
[[nodiscard]] bool StateMeansEnabled(std::string_view raw) {
    const auto state = AsciiLower(Trim(raw));
    return state == "enabled" || state == "enable pending" || state == "enabled (pending)";
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
    if (raw.size() >= 8 && raw[1] == '\0' && raw[3] == '\0') {
        const auto* wide = reinterpret_cast<const wchar_t*>(raw.data());
        const auto count = raw.size() / sizeof(wchar_t);
        return yeet17::core::WideToUtf8(std::wstring_view{wide, count});
    }
    return raw;
}

// Capture stdout/stderr of a child process. Returns false if the process
// never started. exitCode is only valid when the function returns true.
bool RunCaptured(const std::wstring& exe, std::wstring command, std::string& output, DWORD& exitCode) {
    output.clear();
    exitCode = 1;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
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
        exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(outWrite);

    if (!started) {
        CloseHandle(outRead);
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
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    output = NormalizeCapturedText(std::move(raw));
    return true;
}

void ParseDismList(std::string_view text, std::unordered_map<std::string, bool>& enabledById) {
    std::string currentName;
    std::string line;

    const auto handleLine = [&](std::string_view rawLine) {
        const auto view = Trim(rawLine);
        if (view.empty()) {
            return;
        }
        const auto colon = view.find(':');
        if (colon == std::string_view::npos) {
            return;
        }
        const auto key = Trim(view.substr(0, colon));
        const auto value = Trim(view.substr(colon + 1));
        if (key == "Feature Name") {
            currentName = std::string{value};
            return;
        }
        if (key == "State" && !currentName.empty()) {
            enabledById[currentName] = StateMeansEnabled(value);
            currentName.clear();
        }
    };

    for (char ch : text) {
        if (ch == '\n') {
            handleLine(line);
            line.clear();
        } else if (ch != '\r') {
            line.push_back(ch);
        }
    }
    if (!line.empty()) {
        handleLine(line);
    }
}

#endif

} // namespace

Features::Features() {
    features_ = {
        {"NetFx3", ".NET Framework 3.5",
         "Нужен старым приложениям. Может скачать компоненты с Windows Update."},
        {"NetFx4-AdvSrvs", ".NET Framework 4 Advanced Services",
         "Расширенные службы .NET 4 (часто уже включены)."},
        {"Microsoft-Hyper-V-All", "Hyper-V",
         "Гипервизор. Нет на Windows Home. Обычно нужна перезагрузка."},
        {"Microsoft-Windows-Subsystem-Linux", "WSL",
         "Подсистема Windows для Linux."},
        {"VirtualMachinePlatform", "Платформа виртуальной машины",
         "Нужна для WSL2."},
        {"Containers-DisposableClientVM", "Windows Sandbox",
         "Песочница. Pro/Enterprise, нужен Hyper-V."},
        {"ServicesForNFS-ClientOnly", "Клиент NFS",
         "Подключение к NFS-ресурсам."},
        {"MediaPlayback", "Воспроизведение медиа",
         "Компонент воспроизведения Windows Media."},
        {"WindowsMediaPlayer", "Windows Media Player",
         "Классический проигрыватель."},
    };
}

std::vector<Feature> Features::All() const {
    return features_;
}

bool Features::Refresh(std::string& error) {
    error.clear();
#ifdef _WIN32
    const auto dism = System32File(L"dism.exe");
    std::wstring command = L"\"" + dism + L"\" /online /get-features /format:list /english";
    std::string output;
    DWORD exitCode = 1;
    yeet17::core::Logger::Instance().Info("DISM: запрос списка компонентов");
    if (!RunCaptured(dism, std::move(command), output, exitCode)) {
        error = "Не удалось запустить dism.exe (код " + std::to_string(GetLastError()) + ")";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    if (exitCode != 0) {
        error = "dism /get-features завершился с кодом " + std::to_string(static_cast<int>(exitCode));
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

    std::unordered_map<std::string, bool> enabledById;
    ParseDismList(output, enabledById);
    for (auto& feature : features_) {
        if (const auto it = enabledById.find(feature.id); it != enabledById.end()) {
            feature.available = true;
            feature.enabled = it->second;
        } else {
            feature.available = false;
            feature.enabled = false;
        }
    }
    yeet17::core::Logger::Instance().Info("DISM: состояния компонентов обновлены");
    return true;
#else
    error = "DISM доступен только в Windows.";
    yeet17::core::Logger::Instance().Warn(error);
    return false;
#endif
}

bool Features::Set(std::string_view id, bool on, std::string& error) {
    error.clear();
    Feature* target = nullptr;
    for (auto& feature : features_) {
        if (feature.id == id) {
            target = &feature;
            break;
        }
    }
    if (!target) {
        error = "Неизвестный компонент: " + std::string{id};
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

#ifdef _WIN32
    const auto dism = System32File(L"dism.exe");
    std::wstring command = L"\"" + dism + L"\" /online ";
    if (on) {
        command += L"/enable-feature /featurename:";
        command += yeet17::core::Utf8ToWide(target->id);
        command += L" /all /norestart";
    } else {
        command += L"/disable-feature /featurename:";
        command += yeet17::core::Utf8ToWide(target->id);
        command += L" /norestart";
    }

    yeet17::core::Logger::Instance().Info(
        std::string{on ? "Включение" : "Отключение"} + " компонента " + target->id);

    std::string output;
    DWORD exitCode = 1;
    if (!RunCaptured(dism, std::move(command), output, exitCode)) {
        error = "Не удалось запустить dism.exe (код " + std::to_string(GetLastError()) + ")";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    if (exitCode != 0) {
        error = "DISM вернул код " + std::to_string(static_cast<int>(exitCode));
        if (!output.empty()) {
            error += ". " + output.substr(0, 400);
        }
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    target->enabled = on;
    yeet17::core::Logger::Instance().Info("Компонент " + target->id + " изменён");
    return true;
#else
    (void)on;
    error = "DISM доступен только в Windows.";
    yeet17::core::Logger::Instance().Warn(error);
    return false;
#endif
}

} // namespace yeet17::config
