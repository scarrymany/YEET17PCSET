#pragma once

#include "pch.h"
#include "modules/config/Features.h"
#include "modules/config/Repairs.h"
#include "modules/config/LegacyPanels.h"
#include "persistence/Preset.h"

#ifdef _WIN32
// cppwinrt -comp generates the ConfigPage_base authoring template into ConfigPage.g.h,
// separately from XamlCompiler's ConfigPage.xaml.g.h (which derives ConfigPageT from it).
#    include "ConfigPage.g.h"
#    include "ConfigPage.xaml.g.h"
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
struct ConfigPage : ConfigPageT<ConfigPage> {
    ConfigPage();
    void Repair_Click(winrt::Windows::Foundation::IInspectable const&,
                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Legacy_Click(winrt::Windows::Foundation::IInspectable const&,
                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    void CaptureTo(::yeet17::persistence::Preset& preset) const;
    void ApplyFrom(const ::yeet17::persistence::Preset& preset);

private:
    void RebuildFeatures();
    void AppendLog(std::string_view line);
    winrt::fire_and_forget ConfirmDisableFeature(std::string id, std::string title,
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch toggle);
    ::yeet17::config::Features features_;
    ::yeet17::config::Repairs repairs_;
    ::yeet17::config::LegacyPanels legacy_;
    bool suppressToggle_{false};
};
#else
struct ConfigPage { ConfigPage(); };
#endif

} // namespace winrt::yeet17::implementation

#ifdef _WIN32
namespace winrt::yeet17::factory_implementation {
struct ConfigPage : ConfigPageT<ConfigPage, implementation::ConfigPage> {};
}
#endif
