#include "pch.h"
#include "app/App.xaml.h"
#include "app/MainWindow.xaml.h"
#include "app/Elevation.h"
#include "core/Logger.h"
#include "core/Localization.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "ui/ThemeService.h"

#ifdef _WIN32
#    include <winrt/Microsoft.UI.Xaml.h>
#    include <MddBootstrap.h>
#    include <WindowsAppSDK-VersionInfo.h>
#endif

using namespace yeet17;

namespace winrt::yeet17::implementation {

App::App() {
    ::yeet17::core::Logger::Instance().Initialize();
    ::yeet17::core::Settings::Instance().Load();
    ::yeet17::core::Localization::Instance().Initialize();

#ifdef _WIN32
    InitializeComponent();

    // Theme before the first window so Mica/Acrylic pick the right variant.
    ::yeet17::ui::ThemeService::Instance().Apply(::yeet17::core::Settings::Instance().Current().theme);

    const bool admin = ::yeet17::app::Elevation::EnsureAdministrator();
    if (!admin && ::yeet17::app::Elevation::RelaunchRequested()) {
        // Unelevated instance exits; the elevated one continues.
    } else if (!admin) {
        ::yeet17::core::Logger::Instance().Warn(
            std::string{::yeet17::core::Strings::NeedAdmin});
    }
#else
    ::yeet17::core::Logger::Instance().Warn("WinUI 3 UI недоступен вне Windows");
#endif
}

#ifdef _WIN32
void App::OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    window_ = winrt::make<MainWindow>();
    window_.Activate();
}
#else
int App::RunHeadlessForReview() {
    return 0;
}
#endif

} // namespace winrt::yeet17::implementation

#ifdef _WIN32
namespace {
// Unpackaged WinUI 3 must bootstrap the Windows App SDK's framework package before touching
// any of its types - including Microsoft::UI::Xaml::Application::Start itself, which is why
// this has to run here, not inside App::App() (Application::Start already needs the runtime by
// the time it invokes App's constructor as its launch callback).
bool BootstrapWindowsAppSdk() {
    PACKAGE_VERSION minVersion{};
    minVersion.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;
    const HRESULT hr = ::MddBootstrapInitialize2(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        minVersion,
        MddBootstrapInitializeOptions_None);
    if (FAILED(hr)) {
        wchar_t message[256]{};
        swprintf_s(message,
                    L"Не удалось инициализировать Windows App SDK (0x%08X).\n"
                    L"Убедитесь, что установлен Windows App Runtime.",
                    static_cast<unsigned int>(hr));
        MessageBoxW(nullptr, message, L"YEET17PCSET", MB_ICONERROR | MB_OK);
        return false;
    }
    return true;
}
} // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    if (!BootstrapWindowsAppSdk()) {
        return 1;
    }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    ::winrt::Microsoft::UI::Xaml::Application::Start(
        [](auto&&) { winrt::make<::winrt::yeet17::implementation::App>(); });
    ::MddBootstrapShutdown();
    return 0;
}
#endif
