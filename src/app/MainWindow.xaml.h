#pragma once

#include "pch.h"
#include "persistence/Preset.h"
#include "modules/updates/SelfUpdate.h"

#include <thread>

#ifdef _WIN32
// cppwinrt -comp generates the MainWindow_base authoring template into MainWindow.g.h,
// separately from XamlCompiler's MainWindow.xaml.g.h (which derives MainWindowT from it).
#    include "MainWindow.g.h"
#    include "MainWindow.xaml.g.h"
#    include "ui/pages/InstallPage.xaml.h"
#    include "ui/pages/TweaksPage.xaml.h"
#    include "ui/pages/ConfigPage.xaml.h"
#    include "ui/pages/UpdatesPage.xaml.h"
#    include <winrt/Microsoft.UI.Composition.h>
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    ~MainWindow();

    void UpdateButton_Click(winrt::Windows::Foundation::IInspectable const&,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void NavView_SelectionChanged(
        winrt::Microsoft::UI::Xaml::Controls::NavigationView const& sender,
        winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
    void ThemeCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                     winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    winrt::fire_and_forget SaveButton_Click(winrt::Windows::Foundation::IInspectable const&,
                                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    winrt::fire_and_forget LoadButton_Click(winrt::Windows::Foundation::IInspectable const&,
                                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    void NavigateTo(std::wstring_view tag);
    void PlayPageTransition(winrt::Microsoft::UI::Xaml::UIElement const& page);
    void StartUpdateCheck();
    void OnUpdateAvailable(::yeet17::updates::ReleaseInfo release);
    void StartAmbientEffect();
    void LayoutAmbient(float width, float height);
    void ApplyWindowMetrics();
    void SyncThemeCombo();
    void SyncCaptionPad();
    void EnsurePages();
    // Not const: UpdatesPage::CaptureTo reads named XAML elements via non-const accessors.
    ::yeet17::persistence::Preset CollectPreset();
    void ApplyPreset(const ::yeet17::persistence::Preset& preset);
    winrt::fire_and_forget ShowError(std::wstring_view text);
    HWND WindowHandle() const;

    bool themeComboReady_{false};
    bool animationsEnabled_{true};
    bool updateBusy_{false};
    std::optional<::yeet17::updates::ReleaseInfo> pendingUpdate_;
    std::thread updateWorker_;
    winrt::Microsoft::UI::Composition::ContainerVisual ambientRoot_{nullptr};
    winrt::Microsoft::UI::Composition::ContainerVisual pixelArchAnchor_{nullptr};
    winrt::yeet17::InstallPage installPage_{nullptr};
    winrt::yeet17::TweaksPage tweaksPage_{nullptr};
    winrt::yeet17::ConfigPage configPage_{nullptr};
    winrt::yeet17::UpdatesPage updatesPage_{nullptr};
};
#else
struct MainWindow {
    MainWindow();
};
#endif

} // namespace winrt::yeet17::implementation

#ifdef _WIN32
namespace winrt::yeet17::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
#endif
