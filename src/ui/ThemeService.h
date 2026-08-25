#pragma once

#include "core/Settings.h"

#ifdef _WIN32
#    include <winrt/Microsoft.UI.Xaml.h>
#endif

namespace yeet17::ui {

// Theme + backdrop. Persistence is Settings::Instance() only — no second file,
// no second ThemeMode. Reuse yeet17::core::ThemeMode.
class ThemeService {
public:
    static ThemeService& Instance();

#ifdef _WIN32
    // Mica on Windows 11 (build >= 22000), DesktopAcrylic on Windows 10.
    void Apply(winrt::Microsoft::UI::Xaml::Window const& window);
#else
    void Apply();
#endif

    // App.xaml.cpp calls this before the first window exists.
    void Apply(yeet17::core::ThemeMode mode);

    // Writes Settings::Current().theme, Settings::Save(), then Apply().
    void SetMode(yeet17::core::ThemeMode mode);

    // Reads Settings — never a shadow copy.
    [[nodiscard]] yeet17::core::ThemeMode Current() const;

    void Cycle();

    [[nodiscard]] static const char* RussianLabel(yeet17::core::ThemeMode mode);

private:
    ThemeService() = default;

#ifdef _WIN32
    void ApplyElementTheme();
    void ApplyBackdrop();
    [[nodiscard]] static bool IsWindows11();

    winrt::weak_ref<winrt::Microsoft::UI::Xaml::Window> window_;
#endif
};

} // namespace yeet17::ui
