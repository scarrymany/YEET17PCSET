#pragma once

#include "pch.h"

#ifdef _WIN32
#    include "App.xaml.g.h"
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
struct App : AppT<App> {
    App();
    void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
};
#else
// Linux-readable stub so the file is not an empty placeholder.
struct App {
    App();
    int RunHeadlessForReview();
};
#endif

} // namespace winrt::yeet17::implementation

// No factory_implementation::App: unlike MainWindow/the pages, App has no runtimeclass IDL
// of its own (it is never activated via a WinRT factory - App.xaml.cpp constructs it directly
// with winrt::make<App>()), so there is no matching App.g.h defining AppT in that namespace.
