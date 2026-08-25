#include "pch.h"
#include "ui/pages/InstallPage.xaml.h"
#include "core/Logger.h"
#include "core/Localization.h"
#include "core/Strings.h"
#include "core/Utf8.h"

#ifdef _WIN32
#    include <winrt/Microsoft.UI.Xaml.Controls.h>
#    include <winrt/Microsoft.UI.Dispatching.h>
#    include <winrt/Windows.UI.Text.h>
#    include <winrt/Windows.Foundation.h>
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;

namespace {
winrt::hstring Loc(std::string_view id) {
    return winrt::hstring{::yeet17::core::Localization::Instance().GetWide(id)};
}

Style NamedStyle(std::wstring_view key) {
    return Application::Current().Resources().Lookup(box_value(key)).as<winrt::Microsoft::UI::Xaml::Style>();
}
} // namespace

InstallPage::InstallPage() {
    InitializeComponent();
    PageTitle().Text(Loc("NavInstall"));
    SearchBox().PlaceholderText(Loc("SearchPlaceholder"));
    InstallButton().Content(winrt::box_value(Loc("InstallSelected")));
    UpgradeButton().Content(winrt::box_value(Loc("UpgradeSelected")));
    UninstallButton().Content(winrt::box_value(Loc("UninstallSelected")));
    RefreshButton().Content(winrt::box_value(Loc("RefreshInstalled")));
    LogBox().Text(Loc("LogEmpty"));
    LogBox().PlaceholderText(Loc("LogEmpty"));

#if __has_include("modules/install/PackageCatalog.h")
    catalog_.Load();
    RebuildCategories();
    RebuildCatalog();
    if (!catalog_.Loaded()) {
        AppendLog(::yeet17::core::Localization::Instance().Get("CatalogMissing"));
    }
#endif
}

InstallPage::~InstallPage() {
    if (cancel_) {
        cancel_->store(true);
    }
#if __has_include("modules/install/WingetClient.h")
    if (inflight_) {
        inflight_->Cancel();
    }
    winget_.Cancel();
#endif
    if (worker_.joinable()) {
        worker_.join();
    }
}

void InstallPage::RebuildCategories() {
#if __has_include("modules/install/PackageCatalog.h")
    CategoryHost().Children().Clear();
    for (const auto& cat : catalog_.Categories()) {
        ToggleButton chip;
        chip.Content(box_value(::yeet17::core::Utf8ToWide(cat.name)));
        chip.Style(NamedStyle(L"ChipToggleStyle"));
        chip.MinHeight(32);
        chip.Tag(box_value(::yeet17::core::Utf8ToWide(cat.id)));
        const auto catId = cat.id;
        chip.Click([this, catId](auto&& sender, auto&&) {
            auto btn = sender.as<ToggleButton>();
            if (auto checked = btn.IsChecked(); checked && checked.Value()) {
                categoryFilter_ = catId;
                for (auto const& child : CategoryHost().Children()) {
                    if (auto other = child.try_as<ToggleButton>(); other && other != btn) {
                        other.IsChecked(false);
                    }
                }
            } else {
                categoryFilter_.reset();
            }
            RebuildCatalog();
        });
        CategoryHost().Children().Append(chip);
    }
#endif
}

void InstallPage::RebuildCatalog() {
#if __has_include("modules/install/PackageCatalog.h")
    CatalogHost().Children().Clear();
    const auto query = ::yeet17::core::WideToUtf8(std::wstring_view{SearchBox().Text()});
    auto items = query.empty() ? catalog_.All() : catalog_.Filtered(query);
    std::string currentCat;
    StackPanel group{nullptr};

    for (const auto& pkg : items) {
        if (categoryFilter_ && pkg.category != *categoryFilter_) {
            continue;
        }
        if (pkg.category != currentCat || !group) {
            currentCat = pkg.category;
            Border card;
            card.Style(NamedStyle(L"CardStyle"));
            group = StackPanel();
            group.Spacing(4);
            TextBlock title;
            std::string catName = pkg.category;
            for (const auto& c : catalog_.Categories()) {
                if (c.id == pkg.category) {
                    catName = c.name;
                    break;
                }
            }
            title.Text(::yeet17::core::Utf8ToWide(catName));
            title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            title.Margin(Thickness{0, 0, 0, 8});
            group.Children().Append(title);
            card.Child(group);
            CatalogHost().Children().Append(card);
        }
        CheckBox box;
        const auto label = pkg.name + (pkg.description.empty() ? "" : ("  ·  " + pkg.description));
        box.Content(box_value(::yeet17::core::Utf8ToWide(label)));
        box.IsChecked(pkg.selected);
        box.Tag(box_value(::yeet17::core::Utf8ToWide(pkg.id)));
        box.Style(NamedStyle(L"PremiumCheckBoxStyle"));
        const auto id = pkg.id;
        box.Checked([this, id](auto&&, auto&&) { catalog_.SetSelected(id, true); });
        box.Unchecked([this, id](auto&&, auto&&) { catalog_.SetSelected(id, false); });
        if (group) group.Children().Append(box);
    }
#endif
}

void InstallPage::SearchBox_TextChanged(IInspectable const&, TextChangedEventArgs const&) {
    RebuildCatalog();
}

void InstallPage::AppendLog(std::string_view line) {
    std::wstring current{LogBox().Text()};
    const auto empty = ::yeet17::core::Localization::Instance().GetWide("LogEmpty");
    if (current == empty) current.clear();
    if (!current.empty()) current += L"\n";
    current += ::yeet17::core::Utf8ToWide(line);
    LogBox().Text(current);
    ::yeet17::core::Logger::Instance().Info(std::string{line});
}

void InstallPage::SetBusy(bool busy) {
    Progress().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
    Progress().IsIndeterminate(busy);
}

#if __has_include("modules/install/WingetClient.h")
void InstallPage::RunOnSelected(::yeet17::install::PackageAction action) {
#if __has_include("modules/install/PackageCatalog.h")
    auto selected = catalog_.Selected();
    if (selected.empty()) {
        return;
    }
    if (!winget_.Available()) {
        AppendLog(::yeet17::core::Localization::Instance().Get("WingetMissing"));
        return;
    }
    SetBusy(true);
    AppendLog(::yeet17::core::Localization::Instance().Get("InProgress"));

    std::vector<::yeet17::install::PackageJob> jobs;
    jobs.reserve(selected.size());
    for (const auto& pkg : selected) {
        ::yeet17::install::PackageJob job;
        job.action = action;
        job.id = pkg.id;
        job.source = pkg.source.empty() ? "winget" : pkg.source;
        jobs.push_back(std::move(job));
    }

    if (cancel_) {
        cancel_->store(true);
    }
    if (inflight_) {
        inflight_->Cancel();
    }
    winget_.Cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    inflight_ = std::make_shared<::yeet17::install::WingetClient>();
    auto cancel = cancel_;
    auto client = inflight_;
    auto weak = get_weak();
    worker_ = std::thread([weak, cancel, client, jobs = std::move(jobs)]() {
        auto result = client->RunBulk(jobs, [weak, cancel](const ::yeet17::install::ProgressEvent& ev) {
            if (cancel && cancel->load()) {
                return;
            }
            if (auto self = weak.get()) {
                self->DispatcherQueue().TryEnqueue([weak, cancel, ev] {
                    if (cancel && cancel->load()) {
                        return;
                    }
                    if (auto page = weak.get()) {
                        if (!ev.line.empty()) page->AppendLog(ev.line);
                        if (ev.finished) {
                            page->AppendLog(::yeet17::core::Localization::Instance().Get(
                                ev.success ? "Done" : "Failed"));
                        }
                    }
                });
            }
        });
        if (cancel && cancel->load()) {
            return;
        }
        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak, cancel, result] {
                if (cancel && cancel->load()) {
                    return;
                }
                if (auto page = weak.get()) {
                    if (!result) page->AppendLog(result.error());
                    page->SetBusy(false);
                }
            });
        }
    });
#endif
}
#endif

void InstallPage::Install_Click(IInspectable const&, RoutedEventArgs const&) {
#if __has_include("modules/install/WingetClient.h")
    RunOnSelected(::yeet17::install::PackageAction::Install);
#endif
}
void InstallPage::Upgrade_Click(IInspectable const&, RoutedEventArgs const&) {
#if __has_include("modules/install/WingetClient.h")
    RunOnSelected(::yeet17::install::PackageAction::Upgrade);
#endif
}
void InstallPage::Uninstall_Click(IInspectable const&, RoutedEventArgs const&) {
#if __has_include("modules/install/WingetClient.h")
    RunOnSelected(::yeet17::install::PackageAction::Uninstall);
#endif
}

void InstallPage::Refresh_Click(IInspectable const&, RoutedEventArgs const&) {
#if __has_include("modules/install/WingetClient.h") && __has_include("modules/install/PackageCatalog.h")
    if (!winget_.Available()) {
        AppendLog(::yeet17::core::Localization::Instance().Get("WingetMissing"));
        return;
    }
    SetBusy(true);
    AppendLog(::yeet17::core::Localization::Instance().Get("InProgress"));
    if (cancel_) {
        cancel_->store(true);
    }
    if (inflight_) {
        inflight_->Cancel();
    }
    winget_.Cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    inflight_ = std::make_shared<::yeet17::install::WingetClient>();
    auto cancel = cancel_;
    auto client = inflight_;
    auto weak = get_weak();
    worker_ = std::thread([weak, cancel, client]() {
        auto listed = client->ListInstalled();
        if (cancel && cancel->load()) {
            return;
        }
        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak, cancel, listed] {
                if (cancel && cancel->load()) {
                    return;
                }
                if (auto page = weak.get()) {
                    if (!listed) {
                        page->AppendLog(listed.error());
                    } else {
                        for (const auto& inst : *listed) {
                            page->catalog_.MarkInstalled(inst.id, true);
                        }
                        page->AppendLog(::yeet17::core::Localization::Instance().Get("Done"));
                    }
                    page->SetBusy(false);
                }
            });
        }
    });
#endif
}

void InstallPage::CaptureTo(::yeet17::persistence::Preset& preset) const {
#if __has_include("modules/install/PackageCatalog.h")
    preset.packages.clear();
    for (const auto& pkg : catalog_.Selected()) {
        preset.packages.push_back(pkg.id);
    }
    preset.customPackages.clear();
    for (const auto& pkg : catalog_.Custom()) {
        ::yeet17::persistence::CustomPackage row;
        row.id = pkg.id;
        row.name = pkg.name;
        preset.customPackages.push_back(std::move(row));
    }
#else
    (void)preset;
#endif
}

void InstallPage::ApplyFrom(const ::yeet17::persistence::Preset& preset) {
#if __has_include("modules/install/PackageCatalog.h")
    for (const auto& pkg : catalog_.All()) {
        catalog_.SetSelected(pkg.id, false);
    }
    for (const auto& id : preset.packages) {
        catalog_.SetSelected(id, true);
    }
    nlohmann::json custom = nlohmann::json::array();
    for (const auto& row : preset.customPackages) {
        custom.push_back({{"id", row.id}, {"name", row.name}});
    }
    catalog_.ImportCustomPackages(custom);
    RebuildCatalog();
#else
    (void)preset;
#endif
}

#else
InstallPage::InstallPage() = default;
#endif

} // namespace winrt::yeet17::implementation
