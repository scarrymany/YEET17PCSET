#include "pch.h"
#include "ui/pages/UpdatesPage.xaml.h"
#include "core/Localization.h"
#include "core/Utf8.h"

#ifdef _WIN32
#    include <winrt/Microsoft.UI.Xaml.Controls.h>
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

UpdatesPage::UpdatesPage() {
    InitializeComponent();
    SyncFromPolicy();
}

void UpdatesPage::AppendLog(std::string_view line) {
    std::wstring current{LogBox().Text()};
    const auto empty = ::yeet17::core::Localization::Instance().GetWide("LogEmpty");
    if (current == empty) current.clear();
    if (!current.empty()) current += L"\n";
    current += ::yeet17::core::Utf8ToWide(line);
    LogBox().Text(current);
}

::yeet17::updates::Mode UpdatesPage::CurrentMode() {
    switch (ModeGroup().SelectedIndex()) {
    case 1: return ::yeet17::updates::Mode::SecurityOnly;
    case 2: return ::yeet17::updates::Mode::Pause;
    case 3: return ::yeet17::updates::Mode::Default;
    default: return ::yeet17::updates::Mode::Full;
    }
}

void UpdatesPage::SyncFromPolicy() {
    syncing_ = true;
    const auto state = ::yeet17::updates::UpdatePolicy::Instance().Read();
    int index = 0;
    switch (state.mode) {
    case ::yeet17::updates::Mode::SecurityOnly: index = 1; break;
    case ::yeet17::updates::Mode::Pause:        index = 2; break;
    case ::yeet17::updates::Mode::Default:      index = 3; break;
    case ::yeet17::updates::Mode::Full:
    default:                                 index = 0; break;
    }
    ModeGroup().SelectedIndex(index);
    const auto days = state.pauseDays < 1 ? 7 : (state.pauseDays > 35 ? 35 : state.pauseDays);
    PauseDays().Value(static_cast<double>(days));
    PauseDays().Visibility(index == 2 ? Visibility::Visible : Visibility::Collapsed);
    UpdateDescription();
    syncing_ = false;
}

void UpdatesPage::UpdateDescription() {
    switch (CurrentMode()) {
    case ::yeet17::updates::Mode::SecurityOnly:
        ModeDescription().Text(L"Только обновления безопасности. Драйверы и функции откладываются.");
        break;
    case ::yeet17::updates::Mode::Pause:
        ModeDescription().Text(L"Официальная пауза Windows Update. Службы не отключаются.");
        break;
    case ::yeet17::updates::Mode::Default:
        ModeDescription().Text(L"Сброс политик: как только что установленная Windows, не «все обновления».");
        break;
    default:
        ModeDescription().Text(L"Обычное поведение Windows Update: получать все обновления.");
        break;
    }
}

void UpdatesPage::ModeGroup_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    if (syncing_) return;
    PauseDays().Visibility(ModeGroup().SelectedIndex() == 2
        ? Visibility::Visible : Visibility::Collapsed);
    UpdateDescription();
}

void UpdatesPage::Apply_Click(IInspectable const&, RoutedEventArgs const&) {
    ::yeet17::updates::State next;
    next.mode = CurrentMode();
    next.pauseDays = static_cast<int>(PauseDays().Value());
    std::string error;
    if (!::yeet17::updates::UpdatePolicy::Instance().Apply(next, error)) {
        AppendLog(error.empty() ? "Не удалось применить политику обновлений" : error);
        return;
    }
    SyncFromPolicy();
    AppendLog("Политика обновлений применена");
}

void UpdatesPage::Reset_Click(IInspectable const&, RoutedEventArgs const&) {
    std::string error;
    if (!::yeet17::updates::UpdatePolicy::Instance().ResetToDefault(error)) {
        AppendLog(error.empty() ? "Не удалось сбросить политику обновлений" : error);
        return;
    }
    SyncFromPolicy();
    AppendLog("Политика сброшена к умолчанию Windows");
}

void UpdatesPage::CaptureTo(::yeet17::persistence::Preset& preset) {
    preset.updates.mode = std::string{::yeet17::updates::UpdatePolicy::ToId(CurrentMode())};
    preset.updates.pauseDays = static_cast<int>(PauseDays().Value());
}

void UpdatesPage::ApplyFrom(const ::yeet17::persistence::Preset& preset) {
    syncing_ = true;
    int index = 0;
    if (auto parsed = ::yeet17::updates::UpdatePolicy::FromId(preset.updates.mode)) {
        switch (*parsed) {
        case ::yeet17::updates::Mode::SecurityOnly: index = 1; break;
        case ::yeet17::updates::Mode::Pause:        index = 2; break;
        case ::yeet17::updates::Mode::Default:      index = 3; break;
        default: break;
        }
    }
    ModeGroup().SelectedIndex(index);
    const auto days = preset.updates.pauseDays < 1 ? 7 : preset.updates.pauseDays;
    PauseDays().Value(static_cast<double>(days > 35 ? 35 : days));
    PauseDays().Visibility(index == 2 ? Visibility::Visible : Visibility::Collapsed);
    UpdateDescription();
    syncing_ = false;
}

#else
UpdatesPage::UpdatesPage() = default;
#endif

} // namespace winrt::yeet17::implementation
