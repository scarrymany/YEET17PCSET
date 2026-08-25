#include "pch.h"
#include "app/Elevation.h"
#include "core/Logger.h"
#include "core/Strings.h"

#ifdef _WIN32
#    include <windows.h>
#    include <shellapi.h>
#endif

namespace yeet17::app {

bool Elevation::IsRunAsAdmin() {
#ifdef _WIN32
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
#else
    return false;
#endif
}

bool Elevation::EnsureAdministrator() {
    if (IsRunAsAdmin()) {
        return true;
    }
#ifdef _WIN32
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        ::yeet17::core::Logger::Instance().Error("Не удалось получить путь exe для повышения прав");
        return false;
    }
    const HINSTANCE rc = ShellExecuteW(nullptr, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(rc);
    if (code > 32) {
        relaunchRequested_ = true;
        ::yeet17::core::Logger::Instance().Info("Запрошено повышение прав (UAC)");
        return false;
    }
    ::yeet17::core::Logger::Instance().Error("Пользователь отклонил UAC или запуск не удался");
    return false;
#else
    ::yeet17::core::Logger::Instance().Warn("Проверка прав администратора доступна только в Windows");
    return false;
#endif
}

} // namespace ::yeet17::app
