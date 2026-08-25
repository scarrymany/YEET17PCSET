#include "pch.h"
#include "modules/config/LegacyPanels.h"
#include "core/Logger.h"
#include "core/Localization.h"

#ifdef _WIN32
#    include <windows.h>
#    include <shellapi.h>
#endif

namespace yeet17::config {
namespace {

[[nodiscard]] const wchar_t* TargetFor(std::string_view id) {
    if (id == "control") return L"control.exe";
    if (id == "mmsys") return L"mmsys.cpl";
    if (id == "ncpa") return L"ncpa.cpl";
    if (id == "appwiz") return L"appwiz.cpl";
    if (id == "sysdm") return L"sysdm.cpl";
    if (id == "powercfg") return L"powercfg.cpl";
    if (id == "services") return L"services.msc";
    if (id == "firewall") return L"firewall.cpl";
    return nullptr;
}

} // namespace

bool LegacyPanels::Open(std::string_view id, std::string& error) {
    error.clear();
    const auto* target = TargetFor(id);
    if (!target) {
        error = "Неизвестная оснастка: " + std::string{id};
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }

#ifdef _WIN32
    const HINSTANCE rc = ShellExecuteW(nullptr, L"open", target, nullptr, nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(rc);
    if (code <= 32) {
        error = "Не удалось открыть оснастку (код " + std::to_string(code) + ")";
        yeet17::core::Logger::Instance().Error(error);
        return false;
    }
    yeet17::core::Logger::Instance().Info("Открыта оснастка: " + std::string{id});
    return true;
#else
    error = "Оснастки доступны только в Windows.";
    yeet17::core::Logger::Instance().Warn(error);
    return false;
#endif
}

} // namespace yeet17::config
