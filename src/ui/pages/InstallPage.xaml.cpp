#include "pch.h"
#include "ui/pages/InstallPage.xaml.h"
#include "core/Logger.h"
#include "core/Localization.h"
#include "core/Paths.h"
#include "core/Strings.h"
#include "core/Utf8.h"

#ifdef _WIN32
#    include <cmath>
#    include <limits>
#    include <unordered_set>
#    include <winrt/Microsoft.UI.Xaml.Controls.h>
#    include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
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
constexpr int kPackageIconSizeEpx = 20;
constexpr int kPackageIconDecodePx = 40; // 2x for crispness on scaled displays

winrt::hstring Loc(std::string_view id) {
    return winrt::hstring{::yeet17::core::Localization::Instance().GetWide(id)};
}

Style NamedStyle(std::wstring_view key) {
    return Application::Current().Resources().Lookup(box_value(key)).as<winrt::Microsoft::UI::Xaml::Style>();
}

// file:/// URI for a local path; spaces are the only URI-breaking character
// our install layouts produce (e.g. "C:\Program Files\...").
winrt::hstring FileUri(const std::filesystem::path& path) {
    std::wstring uri = L"file:///";
    for (const wchar_t ch : path.wstring()) {
        if (ch == L'\\') {
            uri += L'/';
        } else if (ch == L' ') {
            uri += L"%20";
        } else {
            uri += ch;
        }
    }
    return winrt::hstring{uri};
}

constexpr DWORD kPostInstallTimeoutMs = 10 * 60 * 1000;

std::string AsciiLower(std::string_view text) {
    std::string out{text};
    for (auto& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

// -EncodedCommand payload: base64 of the UTF-16LE command text. Sidesteps
// every quoting problem a nested iwr/iex one-liner would otherwise cause.
std::wstring Base64EncodeUtf16(const std::wstring& text) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const size_t total = text.size() * sizeof(wchar_t);
    std::wstring out;
    out.reserve((total + 2) / 3 * 4);
    for (size_t i = 0; i < total; i += 3) {
        unsigned value = static_cast<unsigned>(bytes[i]) << 16;
        if (i + 1 < total) value |= static_cast<unsigned>(bytes[i + 1]) << 8;
        if (i + 2 < total) value |= bytes[i + 2];
        out += kAlphabet[(value >> 18) & 63];
        out += kAlphabet[(value >> 12) & 63];
        out += (i + 1 < total) ? kAlphabet[(value >> 6) & 63] : L'=';
        out += (i + 2 < total) ? kAlphabet[value & 63] : L'=';
    }
    return out;
}

// Blocking, hidden PowerShell run; returns the exit code, -1 on start
// failure or timeout. Called from the install worker thread only.
int RunHiddenPowerShell(const std::wstring& command) {
    std::wstring cmdline = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand " +
                           Base64EncodeUtf16(command);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return -1;
    }
    int exitCode = -1;
    if (::WaitForSingleObject(process.hProcess, kPostInstallTimeoutMs) == WAIT_OBJECT_0) {
        DWORD code = 0;
        if (::GetExitCodeProcess(process.hProcess, &code)) {
            exitCode = static_cast<int>(code);
        }
    } else {
        ::TerminateProcess(process.hProcess, 1);
    }
    ::CloseHandle(process.hProcess);
    ::CloseHandle(process.hThread);
    return exitCode;
}

// Green "installed" pill. Colors are hardcoded midtones readable on both
// themes: a theme-dictionary lookup here would freeze the build-time theme
// (the tooltip icon had exactly that bug).
UIElement InstalledBadge() {
    Border badge;
    badge.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{8, 8, 8, 8});
    badge.Padding(Thickness{8, 1, 8, 2});
    badge.VerticalAlignment(VerticalAlignment::Center);
    badge.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
        winrt::Windows::UI::Color{0x2E, 0x3F, 0xB9, 0x50}});
    TextBlock label;
    label.Text(L"установлено");
    label.FontSize(11);
    label.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
        winrt::Windows::UI::Color{0xFF, 0x4C, 0xB8, 0x5C}});
    badge.Child(label);
    return badge;
}

// Icon image (catalog/icons/<id>.png) or a same-size spacer so rows with and
// without an icon (custom packages) keep their text aligned.
UIElement PackageIconElement(const std::string& packageId) {
    std::error_code ec;
    const auto iconPath = ::yeet17::core::Paths::CatalogDir() / "icons" /
                          (packageId + ".png");
    if (std::filesystem::exists(iconPath, ec)) {
        try {
            winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            bitmap.DecodePixelWidth(kPackageIconDecodePx);
            bitmap.UriSource(winrt::Windows::Foundation::Uri{FileUri(iconPath)});
            Image image;
            image.Source(bitmap);
            image.Width(kPackageIconSizeEpx);
            image.Height(kPackageIconSizeEpx);
            return image;
        } catch (...) {
        }
    }
    Border spacer;
    spacer.Width(kPackageIconSizeEpx);
    spacer.Height(kPackageIconSizeEpx);
    return spacer;
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
    CategoryHost().SizeChanged(
        [this](auto&&, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args) {
            const double width = args.NewSize().Width;
            if (std::abs(width - categoryWidth_) > 1.0) {
                categoryWidth_ = width;
                LayoutCategoryChips();
            }
        });

#if __has_include("modules/install/WingetClient.h") && __has_include("modules/install/PackageCatalog.h")
    // Quiet scan at startup so already-installed packages are badged
    // without the user pressing the refresh button.
    StartInstalledScan(false);
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
    categoryChips_.clear();
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
                for (auto const& other : categoryChips_) {
                    if (other != btn) {
                        other.IsChecked(false);
                    }
                }
            } else {
                categoryFilter_.reset();
            }
            RebuildCatalog();
        });
        categoryChips_.push_back(chip);
    }
    LayoutCategoryChips();
#endif
}

// Wraps the category chips into as many rows as the available width needs.
void InstallPage::LayoutCategoryChips() {
    constexpr double kFallbackRowWidth = 860.0;
    const double rowWidth = categoryWidth_ > 60.0 ? categoryWidth_ : kFallbackRowWidth;

    // Chips must leave their previous row panels before re-parenting.
    for (auto const& child : CategoryHost().Children()) {
        if (auto row = child.try_as<StackPanel>()) {
            row.Children().Clear();
        }
    }
    CategoryHost().Children().Clear();

    StackPanel row{nullptr};
    double used = 0;
    for (auto const& chip : categoryChips_) {
        chip.Measure({std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::infinity()});
        const double width = chip.DesiredSize().Width;
        if (!row || (used > 0 && used + width > rowWidth)) {
            row = StackPanel();
            row.Orientation(Orientation::Horizontal);
            CategoryHost().Children().Append(row);
            used = 0;
        }
        row.Children().Append(chip);
        used += width;
    }
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
        StackPanel rowContent;
        rowContent.Orientation(Orientation::Horizontal);
        rowContent.Spacing(10);
        rowContent.VerticalAlignment(VerticalAlignment::Center);
        rowContent.Children().Append(PackageIconElement(pkg.id));
        TextBlock nameText;
        nameText.Text(::yeet17::core::Utf8ToWide(pkg.name));
        nameText.VerticalAlignment(VerticalAlignment::Center);
        rowContent.Children().Append(nameText);
        if (!pkg.description.empty()) {
            TextBlock descText;
            descText.Text(::yeet17::core::Utf8ToWide(pkg.description));
            descText.Style(NamedStyle(L"CaptionStyle"));
            descText.TextWrapping(TextWrapping::NoWrap);
            descText.VerticalAlignment(VerticalAlignment::Center);
            rowContent.Children().Append(descText);
        }
        if (pkg.installed) {
            rowContent.Children().Append(InstalledBadge());
        }
        box.Content(rowContent);
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
    if (action == ::yeet17::install::PackageAction::Install) {
        // Skip what the installed-scan already found: no point reinstalling.
        std::erase_if(selected, [this](const ::yeet17::install::Package& pkg) {
            if (pkg.installed) {
                AppendLog(pkg.name + ": уже установлено, пропускаю");
                return true;
            }
            return false;
        });
        if (selected.empty()) {
            AppendLog("Всё выбранное уже установлено");
            return;
        }
    }
    if (!winget_.Available()) {
        AppendLog(::yeet17::core::Localization::Instance().Get("WingetMissing"));
        return;
    }
    SetBusy(true);
    AppendLog(::yeet17::core::Localization::Instance().Get("InProgress"));

    std::vector<::yeet17::install::PackageJob> jobs;
    jobs.reserve(selected.size());
    std::unordered_map<std::string, std::string> postInstall;
    for (const auto& pkg : selected) {
        ::yeet17::install::PackageJob job;
        job.action = action;
        job.id = pkg.id;
        job.source = pkg.source.empty() ? "winget" : pkg.source;
        jobs.push_back(std::move(job));
        if (!pkg.postInstall.empty()) {
            postInstall[pkg.id] = pkg.postInstall;
        }
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
    worker_ = std::thread([weak, cancel, client, jobs = std::move(jobs),
                           postInstall = std::move(postInstall)]() {
        auto notify = [weak, cancel](std::string line) {
            if (auto self = weak.get()) {
                self->DispatcherQueue().TryEnqueue([weak, cancel, line = std::move(line)] {
                    if (cancel && cancel->load()) {
                        return;
                    }
                    if (auto page = weak.get()) {
                        page->AppendLog(line);
                    }
                });
            }
        };
        auto result = client->RunBulk(jobs, [weak, cancel, &postInstall,
                                             &notify](const ::yeet17::install::ProgressEvent& ev) {
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
            // Post-install hook (e.g. SpotX over Spotify): runs synchronously on
            // this worker thread so the queue stays sequential, never on the UI.
            if (ev.finished && ev.success &&
                ev.action != ::yeet17::install::PackageAction::Uninstall) {
                if (auto found = postInstall.find(ev.packageId); found != postInstall.end()) {
                    notify("Пост-установка для " + ev.packageId + " — подождите…");
                    const int code = RunHiddenPowerShell(::yeet17::core::Utf8ToWide(found->second));
                    notify(code == 0
                               ? "Пост-установка завершена: " + ev.packageId
                               : "Пост-установка не удалась (" + ev.packageId +
                                     ", код " + std::to_string(code) + ")");
                }
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
                    // Refresh the installed badges to reflect what just changed.
                    page->StartInstalledScan(false);
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
    StartInstalledScan(true);
#endif
}

#if __has_include("modules/install/WingetClient.h") && __has_include("modules/install/PackageCatalog.h")
void InstallPage::StartInstalledScan(bool announce) {
    if (!winget_.Available()) {
        if (announce) {
            AppendLog(::yeet17::core::Localization::Instance().Get("WingetMissing"));
        }
        return;
    }
    SetBusy(true);
    if (announce) {
        AppendLog(::yeet17::core::Localization::Instance().Get("InProgress"));
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
    worker_ = std::thread([weak, cancel, client, announce]() {
        auto listed = client->ListInstalled();
        if (cancel && cancel->load()) {
            return;
        }
        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak, cancel, listed, announce] {
                if (cancel && cancel->load()) {
                    return;
                }
                if (auto page = weak.get()) {
                    if (!listed) {
                        if (announce) {
                            page->AppendLog(listed.error());
                        }
                    } else {
                        // winget shows raw "ARP\..." / "MSIX\..." ids for installs
                        // it can't map to a source package (Chrome and Edge do
                        // this), so ids alone miss them - fall back to display
                        // names: exact match, or "<name> (something)".
                        std::unordered_set<std::string> installedIds;
                        std::vector<std::string> installedNames;
                        for (const auto& inst : *listed) {
                            installedIds.insert(AsciiLower(inst.id));
                            installedNames.push_back(AsciiLower(inst.name));
                        }
                        size_t found = 0;
                        for (const auto& pkg : page->catalog_.All()) {
                            const auto idKey = AsciiLower(pkg.id);
                            const auto nameKey = AsciiLower(pkg.name);
                            const auto namePrefix = nameKey + " (";
                            bool installed = installedIds.contains(idKey);
                            if (!installed) {
                                for (const auto& name : installedNames) {
                                    if (name == nameKey || name.starts_with(namePrefix)) {
                                        installed = true;
                                        break;
                                    }
                                }
                            }
                            page->catalog_.MarkInstalled(pkg.id, installed);
                            if (installed) ++found;
                        }
                        // Re-render so the "installed" badges show up.
                        page->RebuildCatalog();
                        page->AppendLog("Обнаружено установленных из каталога: " +
                                        std::to_string(found));
                    }
                    page->SetBusy(false);
                }
            });
        }
    });
}
#endif

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
