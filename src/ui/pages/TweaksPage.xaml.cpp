#include "pch.h"
#include "ui/pages/TweaksPage.xaml.h"
#include "core/Logger.h"
#include "core/Localization.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "core/Paths.h"
#include "core/Utf8.h"
#include "persistence/ConfigStore.h"

#ifdef _WIN32
#    include <winrt/Microsoft.UI.Xaml.Controls.h>
#    include <winrt/Windows.UI.Text.h>
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

TweaksPage::TweaksPage() {
    InitializeComponent();
    const auto catalog = ::yeet17::core::Paths::CatalogDir() / "tweaks.json";
    if (auto loaded = engine_.Load(catalog); !loaded) {
        AppendLog(loaded.error());
    }
    Rebuild();
}

void TweaksPage::AppendLog(std::string_view line) {
    std::wstring current{LogBox().Text()};
    if (!current.empty()) current += L"\n";
    current += ::yeet17::core::Utf8ToWide(line);
    LogBox().Text(current);
    ::yeet17::core::Logger::Instance().Info(std::string{line});
}

void TweaksPage::Rebuild() {
    auto fill = [this](StackPanel const& host, ::yeet17::tweaks::Tier tier) {
        host.Children().Clear();
        for (const auto* tweak : engine_.ByTier(tier)) {
            if (!tweak) continue;
            Border card;
            card.Style(Application::Current().Resources().Lookup(box_value(L"CardStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
            Grid row;
            row.ColumnDefinitions().Append(ColumnDefinition{});
            auto autoCol = ColumnDefinition();
            autoCol.Width(GridLength{1, GridUnitType::Auto});
            row.ColumnDefinitions().Append(autoCol);

            // Title + "?" hint with the description in a tooltip: an Expander here
            // resized the card on open, which read as layout jank next to the toggle.
            StackPanel titleRow;
            titleRow.Orientation(Orientation::Horizontal);
            titleRow.Spacing(8);
            titleRow.VerticalAlignment(VerticalAlignment::Center);

            TextBlock title;
            title.Text(::yeet17::core::Utf8ToWide(tweak->title));
            title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            title.VerticalAlignment(VerticalAlignment::Center);
            titleRow.Children().Append(title);

            if (!tweak->description.empty()) {
                FontIcon help;
                help.Glyph(L"");
                help.FontSize(13);
                help.VerticalAlignment(VerticalAlignment::Center);
                // No explicit Foreground: a brush looked up at build time is
                // frozen to the then-active theme and turns invisible after a
                // theme switch. Default foreground follows the theme; opacity
                // mutes the icon instead.
                help.Opacity(0.55);
                ToolTip tip;
                tip.Content(box_value(::yeet17::core::Utf8ToWide(tweak->description)));
                ToolTipService::SetToolTip(help, tip);
                titleRow.Children().Append(help);
            }

            Grid::SetColumn(titleRow, 0);
            row.Children().Append(titleRow);

            ToggleSwitch sw;
            sw.Style(Application::Current().Resources().Lookup(box_value(L"TelegramToggleStyle")).as<winrt::Microsoft::UI::Xaml::Style>());
            const bool on = selected_.count(tweak->id) != 0 || engine_.IsApplied(tweak->id);
            sw.IsOn(on);
            if (on) selected_.insert(tweak->id);
            sw.OffContent(box_value(L""));
            sw.OnContent(box_value(L""));
            const auto id = tweak->id;
            sw.Toggled([this, id](auto&& sender, auto&&) {
                if (sender.as<ToggleSwitch>().IsOn()) selected_.insert(id);
                else selected_.erase(id);
            });
            Grid::SetColumn(sw, 1);
            row.Children().Append(sw);
            card.Child(row);
            host.Children().Append(card);
        }
    };
    fill(EssentialHost(), ::yeet17::tweaks::Tier::Essential);
    fill(AdvancedHost(), ::yeet17::tweaks::Tier::Advanced);
}

std::string TweaksPage::TweakTitle(std::string_view id) const {
    if (const auto* t = engine_.Find(id)) return t->title;
    return {};
}

void TweaksPage::ApplyEnabled(bool confirmed) {
    std::unordered_map<std::string, bool> enabled;
    for (const auto& tweak : engine_.All()) {
        enabled[tweak.id] = selected_.count(tweak.id) != 0;
    }
    if (selected_.empty()) {
        AppendLog("Нет выбранных твиков");
        return;
    }
    ::yeet17::tweaks::ApplyOptions opt;
    opt.confirmed = confirmed;
    opt.createRestorePoint = ::yeet17::core::Settings::Instance().Current().createRestorePoint;
    auto r = engine_.ApplyEnabled(enabled, opt);
    AppendLog(r ? std::string{"Твики применены"} : r.error());
}

winrt::fire_and_forget TweaksPage::Apply_Click(IInspectable const&, RoutedEventArgs const&) {
    auto lifetime = get_strong();
    std::vector<std::string> ids(selected_.begin(), selected_.end());
    if (ids.empty()) {
        AppendLog("Нет выбранных твиков");
        co_return;
    }
    bool needsConfirm = false;
    for (const auto& id : ids) {
        if (const auto* t = engine_.Find(id); t && t->requiresConfirm) {
            needsConfirm = true;
            break;
        }
    }
    if (needsConfirm) {
        ContentDialog dialog;
        dialog.XamlRoot(XamlRoot());
        dialog.Title(box_value(L"Подтверждение"));
        dialog.Content(box_value(::yeet17::core::Utf8ToWide(::yeet17::core::Strings::ConfirmDangerous)));
        dialog.PrimaryButtonText(L"Да");
        dialog.CloseButtonText(L"Нет");
        dialog.DefaultButton(ContentDialogButton::Close);
        const auto result = co_await dialog.ShowAsync();
        if (result != ContentDialogResult::Primary) {
            co_return;
        }
    }
    ApplyEnabled(true);
}

void TweaksPage::Undo_Click(IInspectable const&, RoutedEventArgs const&) {
    bool any = false;
    for (const auto& id : selected_) {
        auto r = engine_.Undo(id);
        const auto title = TweakTitle(id);
        AppendLog(r ? (std::string{"Отменено: "} + (title.empty() ? std::string{"твик"} : title)) : r.error());
        any = true;
    }
    if (!any) AppendLog("Нечего отменять");
}

void TweaksPage::Preset_Click(IInspectable const& sender, RoutedEventArgs const&) {
    auto tag = unbox_value_or<hstring>(sender.as<Button>().Tag(), L"standard");
    const auto id = ::yeet17::core::WideToUtf8(std::wstring_view{tag});
    selected_.clear();
    if (auto preset = ::yeet17::persistence::ConfigStore::Instance().LoadPreset(id)) {
        for (const auto& [tid, on] : preset->tweaks) {
            if (on) selected_.insert(tid);
        }
    } else {
        for (const auto* tw : engine_.ByPreset(id)) {
            if (tw) selected_.insert(tw->id);
        }
    }
    ::yeet17::core::Settings::Instance().Current().lastPreset = id;
    Rebuild();
    const char* label = ::yeet17::core::Strings::PresetStandard.data();
    if (id == "minimal") label = ::yeet17::core::Strings::PresetMinimal.data();
    else if (id == "maximal") label = ::yeet17::core::Strings::PresetMaximal.data();
    AppendLog(std::string{"Пресет: "} + label);
}

void TweaksPage::CaptureTo(::yeet17::persistence::Preset& preset) const {
    preset.tweaks.clear();
    for (const auto& tweak : engine_.All()) {
        preset.tweaks[tweak.id] = selected_.count(tweak.id) != 0;
    }
}

void TweaksPage::ApplyFrom(const ::yeet17::persistence::Preset& preset) {
    selected_.clear();
    for (const auto& [id, on] : preset.tweaks) {
        if (on) selected_.insert(id);
    }
    Rebuild();
}

#else
TweaksPage::TweaksPage() = default;
#endif

} // namespace winrt::yeet17::implementation
