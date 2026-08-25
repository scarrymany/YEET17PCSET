#include "pch.h"
#include "app/MainWindow.xaml.h"
#include "core/Logger.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "core/Localization.h"
#include "ui/ThemeService.h"
#include "ui/pages/InstallPage.xaml.h"
#include "ui/pages/TweaksPage.xaml.h"
#include "ui/pages/ConfigPage.xaml.h"
#include "ui/pages/UpdatesPage.xaml.h"
#include "persistence/ConfigStore.h"

#ifdef _WIN32
#    include <array>
#    include <chrono>
#    include <cmath>
#    include <filesystem>
#    include <winrt/Microsoft.UI.Composition.h>
#    include <winrt/Microsoft.UI.Dispatching.h>
#    include <winrt/Microsoft.UI.Xaml.Hosting.h>
#    include <winrt/Microsoft.UI.Xaml.Media.h>
// windows.h defines GetCurrentTime as a macro, which collides with
// Storyboard::GetCurrentTime inside the projection header.
#    ifdef GetCurrentTime
#        undef GetCurrentTime
#    endif
#    include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#    include <winrt/Microsoft.UI.Windowing.h>
#    include <winrt/Microsoft.UI.Interop.h>
#    include <winrt/Windows.Foundation.Numerics.h>
#    include <winrt/Windows.Storage.h>
#    include <winrt/Windows.Storage.Pickers.h>
#    include <winrt/Windows.UI.ViewManagement.h>
#    include <microsoft.ui.xaml.window.h>
#    include <ShObjIdl_core.h>
#endif

namespace winrt::yeet17::implementation {

#ifdef _WIN32
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;

namespace {
winrt::hstring Loc(std::string_view id) {
    return winrt::hstring{::yeet17::core::Localization::Instance().GetWide(id)};
}

template <typename TPicker>
void BindPicker(TPicker const& picker, HWND hwnd) {
    if (auto init = picker.try_as<::IInitializeWithWindow>()) {
        init->Initialize(hwnd);
    }
}

// Ambient background accent: native retelling of ThreeUI's Predictive Arc -
// a halftone grid of pixels whose brightness follows an arch curve, with
// elongated luminous cores travelling along that arch. Grid dots are static
// shapes (rasterized once); only the two cores animate, so GPU cost stays tiny.
constexpr winrt::Windows::UI::Color kAccentColor{0xFF, 0x5B, 0x8C, 0xFF};
constexpr float kPageSlideDistance = 14.f;
constexpr auto kPageFadeDuration = std::chrono::milliseconds(240);
constexpr auto kPageSlideDuration = std::chrono::milliseconds(280);

constexpr float kArchGridWidth = 960.f;
constexpr float kArchGridHeight = 430.f;
constexpr float kArchDotSpacing = 20.f;
constexpr float kArchDotRadius = 1.4f;
constexpr float kArchFalloff = 130.f;
constexpr float kPi = 3.14159265f;

// Arch centerline inside the grid's local coordinates (rainbow-like curve).
float ArchY(float x) {
    const float t = x / kArchGridWidth;
    return kArchGridHeight * 0.82f - kArchGridHeight * 0.62f * std::sin(kPi * t);
}

winrt::Windows::UI::Color AccentWithAlpha(uint8_t alpha) {
    auto color = kAccentColor;
    color.A = alpha;
    return color;
}

bool SystemAnimationsEnabled() {
    try {
        return winrt::Windows::UI::ViewManagement::UISettings{}.AnimationsEnabled();
    } catch (...) {
        return true;
    }
}
} // namespace

MainWindow::MainWindow() {
    InitializeComponent();
    Title(::yeet17::core::Utf8ToWide(::yeet17::core::Localization::Instance().Get("WindowTitle")));
    TitleText().Text(Loc("WindowTitle"));
    ThemeLabel().Text(Loc("Theme"));
    SaveButton().Content(winrt::box_value(Loc("SaveConfig")));
    LoadButton().Content(winrt::box_value(Loc("LoadConfig")));

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());

    ::yeet17::ui::ThemeService::Instance().Apply(*this);
    animationsEnabled_ = SystemAnimationsEnabled();
    StartAmbientEffect();
    SyncThemeCombo();
    ApplyWindowMetrics();
    SyncCaptionPad();

    if (auto content = Content().try_as<FrameworkElement>()) {
        content.Loaded([this](auto&&, auto&&) {
            ApplyWindowMetrics();
            SyncCaptionPad();
        });
    }

    if (NavView().MenuItems().Size() > 0) {
        NavView().SelectedItem(NavView().MenuItems().GetAt(0));
    }

    StartUpdateCheck();
}

MainWindow::~MainWindow() {
    if (updateWorker_.joinable()) {
        updateWorker_.join();
    }
}

void MainWindow::StartUpdateCheck() {
    VersionText().Text(L"v" + winrt::hstring{::yeet17::core::Utf8ToWide(YEET17_VERSION)});
    auto weak = get_weak();
    updateWorker_ = std::thread([weak] {
        auto result = ::yeet17::updates::SelfUpdate::Instance().CheckForUpdate(YEET17_VERSION);
        if (!result) {
            ::yeet17::core::Logger::Instance().Warn(
                std::string{"Проверка обновлений: "} + result.error());
            return;
        }
        if (!result->has_value()) {
            ::yeet17::core::Logger::Instance().Info("Обновлений нет: установлена последняя версия");
            return;
        }
        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak, release = std::move(**result)]() mutable {
                if (auto page = weak.get()) {
                    page->OnUpdateAvailable(std::move(release));
                }
            });
        }
    });
}

void MainWindow::OnUpdateAvailable(::yeet17::updates::ReleaseInfo release) {
    ::yeet17::core::Logger::Instance().Info(
        std::string{"Доступно обновление "} + release.tag);
    pendingUpdate_ = std::move(release);
    UpdateButton().Content(box_value(
        L"Обновить до v" + winrt::hstring{::yeet17::core::Utf8ToWide(pendingUpdate_->version)}));
    UpdateButton().Visibility(Visibility::Visible);
}

void MainWindow::UpdateButton_Click(IInspectable const&, RoutedEventArgs const&) {
    if (updateBusy_ || !pendingUpdate_) {
        return;
    }
    updateBusy_ = true;
    UpdateButton().IsEnabled(false);
    UpdateButton().Content(box_value(L"Загрузка… 0%"));
    if (updateWorker_.joinable()) {
        updateWorker_.join();
    }

    auto weak = get_weak();
    auto release = *pendingUpdate_;
    auto lastPercent = std::make_shared<std::atomic<int>>(-1);
    updateWorker_ = std::thread([weak, release, lastPercent] {
        auto& updater = ::yeet17::updates::SelfUpdate::Instance();
        auto zip = updater.DownloadAsset(
            release, [weak, lastPercent](std::uint64_t received, std::uint64_t total) {
                if (total == 0) return;
                const int percent = static_cast<int>(received * 100 / total);
                if (percent == lastPercent->exchange(percent)) return;
                if (auto self = weak.get()) {
                    self->DispatcherQueue().TryEnqueue([weak, percent] {
                        if (auto page = weak.get()) {
                            page->UpdateButton().Content(box_value(
                                L"Загрузка… " + winrt::to_hstring(percent) + L"%"));
                        }
                    });
                }
            });

        std::optional<std::string> error;
        if (!zip) {
            error = zip.error();
        } else if (auto launched = updater.LaunchUpdater(*zip); !launched) {
            error = launched.error();
        }

        if (auto self = weak.get()) {
            self->DispatcherQueue().TryEnqueue([weak, error] {
                auto page = weak.get();
                if (!page) return;
                if (error) {
                    ::yeet17::core::Logger::Instance().Warn(
                        std::string{"Обновление: "} + *error);
                    page->updateBusy_ = false;
                    page->UpdateButton().IsEnabled(true);
                    page->UpdateButton().Content(box_value(L"Повторить обновление"));
                    page->ShowError(::yeet17::core::Utf8ToWide(*error));
                    return;
                }
                // The detached updater waits for this process to exit,
                // unpacks the zip over the install dir and restarts the app.
                Microsoft::UI::Xaml::Application::Current().Exit();
            });
        }
    });
}

void MainWindow::ApplyWindowMetrics() {
    auto appWindow = this->AppWindow();
    if (!appWindow) return;

    try {
        const auto icon = ::yeet17::core::Paths::ResourcesDir() / "app.ico";
        if (std::filesystem::exists(icon)) {
            appWindow.SetIcon(icon.wstring());
        }
    } catch (...) {
    }

    float scale = 1.f;
    if (auto fe = Content().try_as<FrameworkElement>()) {
        if (auto root = fe.XamlRoot()) {
            scale = static_cast<float>(root.RasterizationScale());
        }
    }
    const auto px = [scale](int epx) {
        return static_cast<int32_t>(std::lround(static_cast<double>(epx) * scale));
    };

    appWindow.ResizeClient({px(1100), px(720)});
    // OverlappedPresenter::PreferredMinimumWidth/Height were added in a WindowsAppSDK release
    // newer than 1.6.240923002 (the version this build pins for its standalone XamlCompiler.exe
    // dependency - see ARCHITECTURE.md); not available here, so no minimum-size enforcement.
}

void MainWindow::SyncCaptionPad() {
    try {
        if (auto titleBar = this->AppWindow().TitleBar()) {
            const auto inset = titleBar.RightInset();
            if (inset > 0) {
                TitleBarCaptionPad().Width(static_cast<double>(inset));
            }
        }
    } catch (...) {
    }
}

void MainWindow::SyncThemeCombo() {
    themeComboReady_ = false;
    switch (::yeet17::ui::ThemeService::Instance().Current()) {
    case ::yeet17::core::ThemeMode::Dark:  ThemeCombo().SelectedIndex(1); break;
    case ::yeet17::core::ThemeMode::Light: ThemeCombo().SelectedIndex(2); break;
    default:                             ThemeCombo().SelectedIndex(0); break;
    }
    themeComboReady_ = true;
}

void MainWindow::NavView_SelectionChanged(NavigationView const&,
                                          NavigationViewSelectionChangedEventArgs const& args) {
    if (auto item = args.SelectedItem().try_as<NavigationViewItem>()) {
        if (auto tag = item.Tag()) {
            NavigateTo(winrt::unbox_value_or<hstring>(tag, L"install"));
        }
    }
}

void MainWindow::EnsurePages() {
    if (!installPage_) installPage_ = winrt::yeet17::InstallPage();
    if (!tweaksPage_) tweaksPage_ = winrt::yeet17::TweaksPage();
    if (!configPage_) configPage_ = winrt::yeet17::ConfigPage();
    if (!updatesPage_) updatesPage_ = winrt::yeet17::UpdatesPage();
}

void MainWindow::NavigateTo(std::wstring_view tag) {
    EnsurePages();
    winrt::Windows::Foundation::IInspectable next{nullptr};
    if (tag == L"tweaks") {
        next = tweaksPage_;
    } else if (tag == L"system") {
        next = configPage_;
    } else if (tag == L"updates") {
        next = updatesPage_;
    } else {
        next = installPage_;
    }
    if (ContentFrame().Content() == next) {
        return;
    }
    ContentFrame().Content(next);
    if (auto element = next.try_as<UIElement>()) {
        PlayPageTransition(element);
    }
}

void MainWindow::PlayPageTransition(UIElement const& page) {
    if (!animationsEnabled_) {
        return;
    }
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Microsoft::UI::Xaml::Media::Animation;
    using winrt::Windows::Foundation::TimeSpan;

    TranslateTransform slide;
    page.RenderTransform(slide);

    Storyboard storyboard;

    DoubleAnimation fade;
    fade.From(0.0);
    fade.To(1.0);
    fade.Duration(Duration{TimeSpan{kPageFadeDuration}, DurationType::TimeSpan});
    Storyboard::SetTarget(fade, page);
    Storyboard::SetTargetProperty(fade, L"Opacity");
    storyboard.Children().Append(fade);

    DoubleAnimation rise;
    rise.From(static_cast<double>(kPageSlideDistance));
    rise.To(0.0);
    rise.Duration(Duration{TimeSpan{kPageSlideDuration}, DurationType::TimeSpan});
    CubicEase ease;
    ease.EasingMode(EasingMode::EaseOut);
    rise.EasingFunction(ease);
    Storyboard::SetTarget(rise, slide);
    Storyboard::SetTargetProperty(rise, L"Y");
    storyboard.Children().Append(rise);

    storyboard.Begin();
}

void MainWindow::StartAmbientEffect() {
    using namespace winrt::Microsoft::UI::Composition;
    using namespace winrt::Microsoft::UI::Xaml::Hosting;
    using winrt::Windows::Foundation::Numerics::float2;
    using winrt::Windows::Foundation::Numerics::float3;

    auto compositor = ElementCompositionPreview::GetElementVisual(AmbientHost()).Compositor();
    ambientRoot_ = compositor.CreateContainerVisual();
    ElementCompositionPreview::SetElementChildVisual(AmbientHost(), ambientRoot_);

    pixelArchAnchor_ = compositor.CreateContainerVisual();
    ambientRoot_.Children().InsertAtTop(pixelArchAnchor_);

    // --- Halftone pixel field with arch-shaped brightness -------------------
    // One shared geometry + a handful of shared brushes (alpha buckets) keep
    // the ~1000 dots cheap; the whole grid is static and rasterized once.
    auto dotGeometry = compositor.CreateEllipseGeometry();
    dotGeometry.Radius({kArchDotRadius, kArchDotRadius});
    dotGeometry.Center({kArchDotRadius, kArchDotRadius});

    constexpr int kAlphaBuckets = 8;
    constexpr uint8_t kDotAlphaBase = 0x07;
    constexpr uint8_t kDotAlphaPeak = 0x52;
    std::vector<CompositionColorBrush> bucketBrushes;
    bucketBrushes.reserve(kAlphaBuckets);
    for (int i = 0; i < kAlphaBuckets; ++i) {
        const float frac = static_cast<float>(i) / (kAlphaBuckets - 1);
        const auto alpha = static_cast<uint8_t>(
            kDotAlphaBase + frac * (kDotAlphaPeak - kDotAlphaBase));
        bucketBrushes.push_back(compositor.CreateColorBrush(AccentWithAlpha(alpha)));
    }

    // Dots are split into phase groups (bands along the arch). Animating each
    // group's opacity with a staggered delay makes a brightness wave travel
    // through the halftone field - the reference's sin(dist - time) pulse -
    // while costing only kWavePhases opacity animations for the whole grid.
    constexpr int kWavePhases = 6;
    constexpr float kWaveBandWidth = 56.f;
    std::vector<ShapeVisual> phaseGroups;
    phaseGroups.reserve(kWavePhases);
    for (int i = 0; i < kWavePhases; ++i) {
        auto group = compositor.CreateShapeVisual();
        group.Size({kArchGridWidth, kArchGridHeight});
        pixelArchAnchor_.Children().InsertAtTop(group);
        phaseGroups.push_back(group);
    }
    for (float y = 0.f; y <= kArchGridHeight; y += kArchDotSpacing) {
        for (float x = 0.f; x <= kArchGridWidth; x += kArchDotSpacing) {
            const float distance = std::fabs(y - ArchY(x));
            const float frac = std::max(0.f, 1.f - distance / kArchFalloff);
            const int bucket = std::min(kAlphaBuckets - 1,
                                        static_cast<int>(frac * kAlphaBuckets));
            auto dot = compositor.CreateSpriteShape(dotGeometry);
            dot.FillBrush(bucketBrushes[static_cast<size_t>(bucket)]);
            dot.Offset({x, y});
            const int band = static_cast<int>(x / kWaveBandWidth) % kWavePhases;
            phaseGroups[static_cast<size_t>(band)].Shapes().Append(dot);
        }
    }

    if (animationsEnabled_) {
        constexpr auto kWavePeriod = std::chrono::milliseconds(3600);
        auto waveEase = compositor.CreateCubicBezierEasingFunction({0.45f, 0.f}, {0.55f, 1.f});
        for (int i = 0; i < kWavePhases; ++i) {
            auto wave = compositor.CreateScalarKeyFrameAnimation();
            wave.InsertKeyFrame(0.f, 0.35f, waveEase);
            wave.InsertKeyFrame(0.5f, 1.f, waveEase);
            wave.InsertKeyFrame(1.f, 0.35f, waveEase);
            wave.Duration(kWavePeriod);
            wave.IterationBehavior(AnimationIterationBehavior::Forever);
            wave.DelayBehavior(AnimationDelayBehavior::SetInitialValueBeforeDelay);
            wave.DelayTime(std::chrono::milliseconds(kWavePeriod.count() * i / kWavePhases));
            phaseGroups[static_cast<size_t>(i)].StartAnimation(L"Opacity", wave);
        }
    }

    // --- Luminous cores travelling along the arch ---------------------------
    auto makeCore = [&](float diameter, uint8_t alpha, float stretch) {
        auto brush = compositor.CreateRadialGradientBrush();
        brush.ColorStops().Append(compositor.CreateColorGradientStop(0.f, AccentWithAlpha(alpha)));
        brush.ColorStops().Append(compositor.CreateColorGradientStop(
            0.45f, AccentWithAlpha(static_cast<uint8_t>(alpha / 3))));
        brush.ColorStops().Append(compositor.CreateColorGradientStop(1.f, AccentWithAlpha(0)));
        auto visual = compositor.CreateSpriteVisual();
        visual.Brush(brush);
        visual.Size({diameter, diameter});
        visual.AnchorPoint({0.5f, 0.5f});
        visual.Scale({stretch, 0.85f, 1.f});
        pixelArchAnchor_.Children().InsertAtTop(visual);
        return visual;
    };

    auto coreBright = makeCore(240.f, 0x4E, 2.6f);
    auto coreSoft = makeCore(300.f, 0x2C, 2.0f);

    auto archPoint = [](float t) {
        const float x = kArchGridWidth * (0.06f + 0.88f * t);
        return float3{x, ArchY(x), 0.f};
    };

    if (animationsEnabled_) {
        auto linear = compositor.CreateLinearEasingFunction();
        auto travel = [&](Visual const& visual, int seconds, bool reverse) {
            auto animation = compositor.CreateVector3KeyFrameAnimation();
            constexpr int kSteps = 8;
            for (int i = 0; i <= kSteps; ++i) {
                float progress = static_cast<float>(i) / kSteps;
                const float t = reverse ? 1.f - progress : progress;
                animation.InsertKeyFrame(progress, archPoint(t), linear);
            }
            animation.Duration(std::chrono::seconds(seconds));
            animation.IterationBehavior(AnimationIterationBehavior::Forever);
            animation.Direction(AnimationDirection::Alternate);
            visual.StartAnimation(L"Offset", animation);
        };
        travel(coreBright, 13, false);
        travel(coreSoft, 19, true);

        auto pulse = [&](Visual const& visual, float from, float to, int seconds) {
            auto animation = compositor.CreateScalarKeyFrameAnimation();
            auto ease = compositor.CreateCubicBezierEasingFunction({0.45f, 0.f}, {0.55f, 1.f});
            animation.InsertKeyFrame(0.f, from, ease);
            animation.InsertKeyFrame(1.f, to, ease);
            animation.Duration(std::chrono::seconds(seconds));
            animation.IterationBehavior(AnimationIterationBehavior::Forever);
            animation.Direction(AnimationDirection::Alternate);
            visual.StartAnimation(L"Opacity", animation);
        };
        pulse(coreBright, 0.65f, 1.f, 4);
        pulse(coreSoft, 0.45f, 0.9f, 6);
    } else {
        coreBright.Offset(archPoint(0.32f));
        coreSoft.Offset(archPoint(0.71f));
    }

    AmbientHost().SizeChanged([weak = get_weak()](auto&&, auto&& args) {
        if (auto self = weak.get()) {
            const auto size = args.NewSize();
            self->LayoutAmbient(static_cast<float>(size.Width),
                                static_cast<float>(size.Height));
        }
    });
}

void MainWindow::LayoutAmbient(float width, float height) {
    if (!pixelArchAnchor_) return;
    // The arch is authored at a fixed logical size and pinned to the lower
    // right of the window; only the anchor moves on resize, so the core
    // animations (relative to the anchor) never restart mid-flight.
    const float x = std::max(width - kArchGridWidth - 8.f, 160.f);
    const float y = std::max(height - kArchGridHeight - 84.f, 48.f);
    pixelArchAnchor_.Offset({x, y, 0.f});
}

void MainWindow::ThemeCombo_SelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    if (!themeComboReady_) return;
    const auto index = ThemeCombo().SelectedIndex();
    auto mode = ::yeet17::core::ThemeMode::System;
    if (index == 1) mode = ::yeet17::core::ThemeMode::Dark;
    else if (index == 2) mode = ::yeet17::core::ThemeMode::Light;
    ::yeet17::ui::ThemeService::Instance().SetMode(mode);
}

HWND MainWindow::WindowHandle() const {
    HWND hwnd{};
    if (auto native = this->try_as<::IWindowNative>()) {
        native->get_WindowHandle(&hwnd);
    }
    return hwnd;
}

::yeet17::persistence::Preset MainWindow::CollectPreset() {
    ::yeet17::persistence::Preset preset;
    preset.app = std::string{::yeet17::persistence::kAppId};
    preset.schemaVersion = ::yeet17::persistence::kSchemaVersion;
    preset.name = "Экспорт";
    // theme stays out of the file — SoT is settings.json
    preset.theme = "system";
    if (installPage_) {
        winrt::get_self<InstallPage>(installPage_)->CaptureTo(preset);
    }
    if (tweaksPage_) {
        winrt::get_self<TweaksPage>(tweaksPage_)->CaptureTo(preset);
    }
    if (configPage_) {
        winrt::get_self<ConfigPage>(configPage_)->CaptureTo(preset);
    }
    if (updatesPage_) {
        winrt::get_self<UpdatesPage>(updatesPage_)->CaptureTo(preset);
    }
    return preset;
}

void MainWindow::ApplyPreset(const ::yeet17::persistence::Preset& preset) {
    EnsurePages();
    winrt::get_self<InstallPage>(installPage_)->ApplyFrom(preset);
    winrt::get_self<TweaksPage>(tweaksPage_)->ApplyFrom(preset);
    winrt::get_self<ConfigPage>(configPage_)->ApplyFrom(preset);
    winrt::get_self<UpdatesPage>(updatesPage_)->ApplyFrom(preset);
}

winrt::fire_and_forget MainWindow::ShowError(std::wstring_view text) {
    auto lifetime = get_strong();
    ContentDialog dialog;
    dialog.XamlRoot(Content().as<FrameworkElement>().XamlRoot());
    dialog.Title(box_value(L"Ошибка"));
    dialog.Content(box_value(hstring{text}));
    dialog.CloseButtonText(L"Закрыть");
    co_await dialog.ShowAsync();
}

winrt::fire_and_forget MainWindow::SaveButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto lifetime = get_strong();
    EnsurePages();
    FileSavePicker picker;
    BindPicker(picker, WindowHandle());
    picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
    picker.SuggestedFileName(L"YEET17PCSET");
    picker.FileTypeChoices().Insert(L"Конфиг YEET17PCSET",
                                    single_threaded_vector<hstring>({L".json"}));
    StorageFile file = co_await picker.PickSaveFileAsync();
    if (!file) co_return;
    auto preset = CollectPreset();
    const std::filesystem::path path{std::wstring{file.Path()}};
    if (!::yeet17::persistence::ConfigStore::Instance().Export(path, preset)) {
        ShowError(L"Не удалось сохранить конфиг.");
    }
}

winrt::fire_and_forget MainWindow::LoadButton_Click(IInspectable const&, RoutedEventArgs const&) {
    auto lifetime = get_strong();
    FileOpenPicker picker;
    BindPicker(picker, WindowHandle());
    picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
    picker.FileTypeFilter().Append(L".json");
    StorageFile file = co_await picker.PickSingleFileAsync();
    if (!file) co_return;
    const std::filesystem::path path{std::wstring{file.Path()}};
    auto imported = ::yeet17::persistence::ConfigStore::Instance().Import(path);
    if (!imported) {
        ShowError(L"Файл не является конфигом YEET17PCSET (нужны schemaVersion 1 и app YEET17PCSET).");
        co_return;
    }
    // theme is not applied — Settings / ThemeService remain the source of truth
    ApplyPreset(*imported);
}

#else
MainWindow::MainWindow() = default;
#endif

} // namespace winrt::yeet17::implementation
