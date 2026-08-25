#include "pch.h"
#include "ui/ThemeService.h"
#include "core/Logger.h"
#include "core/Strings.h"

#ifdef _WIN32
#    include <winrt/Microsoft.UI.Xaml.h>
#    include <winrt/Microsoft.UI.Xaml.Media.h>
#    include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#endif

namespace yeet17::ui {

ThemeService& ThemeService::Instance() {
    static ThemeService instance;
    return instance;
}

const char* ThemeService::RussianLabel(yeet17::core::ThemeMode mode) {
    switch (mode) {
    case yeet17::core::ThemeMode::Dark:  return yeet17::core::Strings::ThemeDark.data();
    case yeet17::core::ThemeMode::Light: return yeet17::core::Strings::ThemeLight.data();
    default:                             return yeet17::core::Strings::ThemeSystem.data();
    }
}

yeet17::core::ThemeMode ThemeService::Current() const {
    return yeet17::core::Settings::Instance().Current().theme;
}

void ThemeService::SetMode(yeet17::core::ThemeMode mode) {
    auto& settings = yeet17::core::Settings::Instance();
    settings.Current().theme = mode;
    settings.Save();
    Apply(mode);
#ifdef _WIN32
    ApplyBackdrop();
#endif
}

void ThemeService::Apply(yeet17::core::ThemeMode mode) {
    auto& settings = yeet17::core::Settings::Instance();
    if (settings.Current().theme != mode) {
        settings.Current().theme = mode;
        settings.Save();
    }
#ifdef _WIN32
    ApplyElementTheme();
#endif
    yeet17::core::Logger::Instance().Info(std::string{"Тема: "} + RussianLabel(mode));
}

void ThemeService::Cycle() {
    switch (Current()) {
    case yeet17::core::ThemeMode::System: SetMode(yeet17::core::ThemeMode::Dark); break;
    case yeet17::core::ThemeMode::Dark:   SetMode(yeet17::core::ThemeMode::Light); break;
    case yeet17::core::ThemeMode::Light:  SetMode(yeet17::core::ThemeMode::System); break;
    }
}

#ifndef _WIN32
void ThemeService::Apply() {}
#endif

#ifdef _WIN32

void ThemeService::Apply(winrt::Microsoft::UI::Xaml::Window const& window) {
    window_ = window;
    ApplyBackdrop();
    ApplyElementTheme();
}

bool ThemeService::IsWindows11() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
        auto rtl = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtl) {
            OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (rtl(&vi) == 0) {
                return vi.dwBuildNumber >= 22000;
            }
        }
    }
    return false;
}

void ThemeService::ApplyBackdrop() {
    auto window = window_.get();
    if (!window) return;

    using winrt::Microsoft::UI::Xaml::Media::DesktopAcrylicBackdrop;
    using winrt::Microsoft::UI::Xaml::Media::MicaBackdrop;
    using winrt::Microsoft::UI::Composition::SystemBackdrops::MicaKind;

    try {
        if (IsWindows11()) {
            MicaBackdrop mica;
            mica.Kind(MicaKind::Base);
            window.SystemBackdrop(mica);
            yeet17::core::Logger::Instance().Info("Фон: Mica");
            return;
        }
    } catch (...) {
    }
    try {
        window.SystemBackdrop(DesktopAcrylicBackdrop());
        yeet17::core::Logger::Instance().Info("Фон: Acrylic (запасной вариант)");
    } catch (...) {
        yeet17::core::Logger::Instance().Warn("Системный фон недоступен");
    }
}

void ThemeService::ApplyElementTheme() {
    auto window = window_.get();
    if (!window) return;
    auto content = window.Content().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
    if (!content) return;

    using winrt::Microsoft::UI::Xaml::ElementTheme;
    switch (Current()) {
    case yeet17::core::ThemeMode::Dark:  content.RequestedTheme(ElementTheme::Dark); break;
    case yeet17::core::ThemeMode::Light: content.RequestedTheme(ElementTheme::Light); break;
    default:                             content.RequestedTheme(ElementTheme::Default); break;
    }
}

#endif // _WIN32

} // namespace yeet17::ui
