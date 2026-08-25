#include "pch.h"
#include "ui/pages/ConfigPage.xaml.h"
#include "core/Logger.h"
#include "core/Localization.h"
#include "core/Utf8.h"

#ifdef _WIN32
#    include <winrt/Microsoft.UI.Xaml.Controls.h>
#    include <winrt/Windows.UI.Text.h>
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

ConfigPage::ConfigPage() {
    InitializeComponent();
    std::string err;
    features_.Refresh(err);
    if (!err.empty()) AppendLog(err);
    RebuildFeatures();
}

void ConfigPage::AppendLog(std::string_view line) {
    std::wstring current{LogBox().Text()};
    if (!current.empty()) current += L"\n";
    current += ::yeet17::core::Utf8ToWide(line);
    LogBox().Text(current);
    ::yeet17::core::Logger::Instance().Info(std::string{line});
}

void ConfigPage::RebuildFeatures() {
    FeaturesHost().Children().Clear();
    for (const auto& f : features_.All()) {
        Grid row;
        row.ColumnDefinitions().Append(ColumnDefinition{});
        auto autoCol = ColumnDefinition();
        autoCol.Width(GridLength{1, GridUnitType::Auto});
        row.ColumnDefinitions().Append(autoCol);
        row.Margin(Thickness{0, 4, 0, 4});

        StackPanel text;
        TextBlock title;
        title.Text(::yeet17::core::Utf8ToWide(f.title));
        title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        TextBlock desc;
        desc.Text(::yeet17::core::Utf8ToWide(f.description));
        desc.Style(Application::Current().Resources().Lookup(box_value(L"CaptionStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
        desc.TextWrapping(TextWrapping::Wrap);
        text.Children().Append(title);
        text.Children().Append(desc);
        Grid::SetColumn(text, 0);
        row.Children().Append(text);

        ToggleSwitch sw;
        sw.Style(Application::Current().Resources().Lookup(box_value(L"TelegramToggleStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
        sw.IsOn(f.enabled);
        sw.IsEnabled(f.available);
        sw.OffContent(box_value(L""));
        sw.OnContent(box_value(L""));
        const auto id = f.id;
        const auto titleUtf8 = f.title;
        sw.Toggled([this, id, titleUtf8](auto&& sender, auto&&) {
            if (suppressToggle_) {
                return;
            }
            auto toggle = sender.as<ToggleSwitch>();
            const bool on = toggle.IsOn();
            if (!on) {
                ConfirmDisableFeature(id, titleUtf8, toggle);
                return;
            }
            std::string error;
            if (!features_.Set(id, true, error)) {
                AppendLog(error.empty() ? "Не удалось изменить компонент" : error);
                suppressToggle_ = true;
                toggle.IsOn(false);
                suppressToggle_ = false;
            } else {
                AppendLog(std::string{"Компонент включён: "} + titleUtf8);
            }
        });
        Grid::SetColumn(sw, 1);
        row.Children().Append(sw);
        FeaturesHost().Children().Append(row);
    }
}

void ConfigPage::Repair_Click(IInspectable const& sender, RoutedEventArgs const&) {
    const auto tag = unbox_value_or<hstring>(sender.as<Button>().Tag(), L"");
    std::string error;
    auto log = [this](std::string_view line) { AppendLog(line); };
    bool ok = false;
    if (tag == L"wu") ok = repairs_.ResetWindowsUpdate(log, error);
    else if (tag == L"sfc") ok = repairs_.RepairSystemImage(log, error);
    else if (tag == L"winget") ok = repairs_.RepairWinget(log, error);
    else ok = repairs_.ResetNetwork(log, error);
    if (!ok && !error.empty()) AppendLog(error);
}

void ConfigPage::Legacy_Click(IInspectable const& sender, RoutedEventArgs const&) {
    const auto tag = unbox_value_or<hstring>(sender.as<Button>().Tag(), L"control");
    std::string error;
    if (!legacy_.Open(::yeet17::core::WideToUtf8(std::wstring_view{tag}), error) && !error.empty()) {
        AppendLog(error);
    } else {
        AppendLog("Открыто");
    }
}

void ConfigPage::CaptureTo(::yeet17::persistence::Preset& preset) const {
    preset.features.clear();
    for (const auto& f : features_.All()) {
        preset.features[f.id] = f.enabled;
    }
}

winrt::fire_and_forget ConfigPage::ConfirmDisableFeature(std::string id, std::string title,
                                                        ToggleSwitch toggle) {
    auto lifetime = get_strong();
    ContentDialog dialog;
    dialog.XamlRoot(XamlRoot());
    dialog.Title(box_value(L"Отключить компонент?"));
    dialog.Content(box_value(::yeet17::core::Utf8ToWide(
        "DISM сразу изменит «" + title +
        "». Это может сломать приложения, которым нужен этот компонент. Продолжить?")));
    dialog.PrimaryButtonText(L"Отключить");
    dialog.CloseButtonText(L"Отмена");
    dialog.DefaultButton(ContentDialogButton::Close);
    const auto result = co_await dialog.ShowAsync();
    if (result != ContentDialogResult::Primary) {
        suppressToggle_ = true;
        toggle.IsOn(true);
        suppressToggle_ = false;
        co_return;
    }
    std::string error;
    if (!features_.Set(id, false, error)) {
        AppendLog(error.empty() ? "Не удалось отключить компонент" : error);
        suppressToggle_ = true;
        toggle.IsOn(true);
        suppressToggle_ = false;
    } else {
        AppendLog(std::string{"Компонент отключён: "} + title);
    }
}

void ConfigPage::ApplyFrom(const ::yeet17::persistence::Preset& preset) {
    // Load only updates the switches. DISM runs after the user confirms disable
    // (or enables a toggle). Silent apply from a file would skip that confirm.
    (void)preset;
    std::string err;
    features_.Refresh(err);
    if (!err.empty()) {
        AppendLog(err);
    }
    RebuildFeatures();
}

#else
ConfigPage::ConfigPage() = default;
#endif

} // namespace winrt::yeet17::implementation
