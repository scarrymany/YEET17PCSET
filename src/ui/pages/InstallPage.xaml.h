#pragma once

#include "pch.h"

#if __has_include("modules/install/PackageCatalog.h")
#    include "modules/install/PackageCatalog.h"
#endif
#if __has_include("modules/install/WingetClient.h")
#    include "modules/install/WingetClient.h"
#endif

#ifdef _WIN32
// cppwinrt -comp generates the InstallPage_base authoring template into InstallPage.g.h,
// separately from XamlCompiler's InstallPage.xaml.g.h (which derives InstallPageT from it).
#    include "InstallPage.g.h"
#    include "InstallPage.xaml.g.h"
#endif

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include "persistence/Preset.h"

namespace winrt::yeet17::implementation {

#ifdef _WIN32
struct InstallPage : InstallPageT<InstallPage> {
    InstallPage();
    ~InstallPage();

    void SearchBox_TextChanged(winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    void Install_Click(winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Upgrade_Click(winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Uninstall_Click(winrt::Windows::Foundation::IInspectable const&,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Refresh_Click(winrt::Windows::Foundation::IInspectable const&,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    void CaptureTo(::yeet17::persistence::Preset& preset) const;
    void ApplyFrom(const ::yeet17::persistence::Preset& preset);

private:
    void RebuildCategories();
    void RebuildCatalog();
    void AppendLog(std::string_view line);
    void SetBusy(bool busy);
#if __has_include("modules/install/WingetClient.h")
    void RunOnSelected(::yeet17::install::PackageAction action);
#endif

#if __has_include("modules/install/PackageCatalog.h")
    ::yeet17::install::PackageCatalog catalog_;
    std::optional<std::string> categoryFilter_;
#endif
#if __has_include("modules/install/WingetClient.h")
    ::yeet17::install::WingetClient winget_;
    std::shared_ptr<::yeet17::install::WingetClient> inflight_;
#endif
    std::shared_ptr<std::atomic<bool>> cancel_{std::make_shared<std::atomic<bool>>(false)};
    std::thread worker_;
};
#else
struct InstallPage {
    InstallPage();
};
#endif

} // namespace winrt::yeet17::implementation

#ifdef _WIN32
namespace winrt::yeet17::factory_implementation {
struct InstallPage : InstallPageT<InstallPage, implementation::InstallPage> {};
}
#endif
