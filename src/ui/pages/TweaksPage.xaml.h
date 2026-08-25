#pragma once

#include "pch.h"
#include "modules/tweaks/TweakEngine.h"
#include "persistence/Preset.h"

#include <unordered_set>

#ifdef _WIN32
// cppwinrt -comp generates the TweaksPage_base authoring template into TweaksPage.g.h,
// separately from XamlCompiler's TweaksPage.xaml.g.h (which derives TweaksPageT from it).
#    include "TweaksPage.g.h"
#    include "TweaksPage.xaml.g.h"
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
struct TweaksPage : TweaksPageT<TweaksPage> {
    TweaksPage();
    winrt::fire_and_forget Apply_Click(winrt::Windows::Foundation::IInspectable const&,
                                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Undo_Click(winrt::Windows::Foundation::IInspectable const&,
                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Preset_Click(winrt::Windows::Foundation::IInspectable const&,
                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    void CaptureTo(::yeet17::persistence::Preset& preset) const;
    void ApplyFrom(const ::yeet17::persistence::Preset& preset);

private:
    void Rebuild();
    void AppendLog(std::string_view line);
    void ApplyEnabled(bool confirmed);
    [[nodiscard]] std::string TweakTitle(std::string_view id) const;
    ::yeet17::tweaks::TweakEngine engine_;
    std::unordered_set<std::string> selected_;
};
#else
struct TweaksPage { TweaksPage(); };
#endif

} // namespace winrt::yeet17::implementation

#ifdef _WIN32
namespace winrt::yeet17::factory_implementation {
struct TweaksPage : TweaksPageT<TweaksPage, implementation::TweaksPage> {};
}
#endif
