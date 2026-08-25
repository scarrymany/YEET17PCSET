#pragma once

#include "pch.h"
#include "modules/updates/UpdatePolicy.h"
#include "persistence/Preset.h"

#ifdef _WIN32
// cppwinrt -comp generates the UpdatesPage_base authoring template into UpdatesPage.g.h,
// separately from XamlCompiler's UpdatesPage.xaml.g.h (which derives UpdatesPageT from it).
#    include "UpdatesPage.g.h"
#    include "UpdatesPage.xaml.g.h"
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
struct UpdatesPage : UpdatesPageT<UpdatesPage> {
    UpdatesPage();
    void ModeGroup_SelectionChanged(winrt::Windows::Foundation::IInspectable const&,
                                    winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void Apply_Click(winrt::Windows::Foundation::IInspectable const&,
                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Reset_Click(winrt::Windows::Foundation::IInspectable const&,
                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    // Not const: reads named XAML elements (ModeGroup/PauseDays), whose generated
    // accessors are non-const.
    void CaptureTo(::yeet17::persistence::Preset& preset);
    void ApplyFrom(const ::yeet17::persistence::Preset& preset);

private:
    void AppendLog(std::string_view line);
    void SyncFromPolicy();
    void UpdateDescription();
    ::yeet17::updates::Mode CurrentMode();
    bool syncing_{false};
};
#else
struct UpdatesPage { UpdatesPage(); };
#endif

} // namespace winrt::yeet17::implementation

#ifdef _WIN32
namespace winrt::yeet17::factory_implementation {
struct UpdatesPage : UpdatesPageT<UpdatesPage, implementation::UpdatesPage> {};
}
#endif
