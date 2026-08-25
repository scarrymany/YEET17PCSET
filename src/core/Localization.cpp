#include "pch.h"
#include "core/Localization.h"
#include "core/Strings.h"
#include "core/Logger.h"

#ifdef _WIN32
#    include <winrt/Windows.ApplicationModel.Resources.h>
#endif

namespace yeet17::core {

Localization& Localization::Instance() {
    static Localization instance;
    return instance;
}

void Localization::Initialize() {
    ready_ = true;
    Logger::Instance().Debug("Локализация: ru-RU (таблица + Resources.resw)");
}

std::string Localization::Get(std::string_view id) const {
#ifdef _WIN32
    try {
        winrt::Windows::ApplicationModel::Resources::ResourceLoader loader{};
        const auto key = Utf8ToWide(id);
        const auto value = loader.GetString(key);
        if (!value.empty()) {
            return WideToUtf8(std::wstring_view{value});
        }
    } catch (...) {
        // Unpackaged builds often have no PRI yet — fall through to Strings.
    }
#endif
    const auto& table = Strings::Table();
    if (auto it = table.find(std::string{id}); it != table.end()) {
        return it->second;
    }
    // Last resort: still Russian, never an English key on screen.
    return std::string{id};
}

std::wstring Localization::GetWide(std::string_view id) const {
    return Utf8ToWide(Get(id));
}

const std::unordered_map<std::string, std::string>& Strings::Table() {
    static const std::unordered_map<std::string, std::string> k{
        {"AppDisplayName", std::string{AppDisplayName}},
        {"WindowTitle", std::string{WindowTitle}},
        {"NavInstall", std::string{NavInstall}},
        {"NavTweaks", std::string{NavTweaks}},
        {"NavSystem", std::string{NavSystem}},
        {"NavUpdates", std::string{NavUpdates}},
        {"Theme", std::string{Theme}},
        {"ThemeSystem", std::string{ThemeSystem}},
        {"ThemeDark", std::string{ThemeDark}},
        {"ThemeLight", std::string{ThemeLight}},
        {"SaveConfig", std::string{SaveConfig}},
        {"LoadConfig", std::string{LoadConfig}},
        {"SearchPlaceholder", std::string{SearchPlaceholder}},
        {"InstallSelected", std::string{InstallSelected}},
        {"UpgradeSelected", std::string{UpgradeSelected}},
        {"UninstallSelected", std::string{UninstallSelected}},
        {"RefreshInstalled", std::string{RefreshInstalled}},
        {"Apply", std::string{Apply}},
        {"Undo", std::string{Undo}},
        {"PresetMinimal", std::string{PresetMinimal}},
        {"PresetStandard", std::string{PresetStandard}},
        {"PresetMaximal", std::string{PresetMaximal}},
        {"SectionEssential", std::string{SectionEssential}},
        {"SectionAdvanced", std::string{SectionAdvanced}},
        {"AdvancedWarning", std::string{AdvancedWarning}},
        {"RepairNetwork", std::string{RepairNetwork}},
        {"RepairWu", std::string{RepairWu}},
        {"RepairSfcDism", std::string{RepairSfcDism}},
        {"RepairWinget", std::string{RepairWinget}},
        {"UpdateFull", std::string{UpdateFull}},
        {"UpdateSecurity", std::string{UpdateSecurity}},
        {"UpdatePause", std::string{UpdatePause}},
        {"UpdateReset", std::string{UpdateReset}},
        {"NeedAdmin", std::string{NeedAdmin}},
        {"ElevationPrompt", std::string{ElevationPrompt}},
        {"RestorePointCaption", std::string{RestorePointCaption}},
        {"ConfirmDangerous", std::string{ConfirmDangerous}},
        {"LogEmpty", std::string{LogEmpty}},
        {"CatalogMissing", std::string{CatalogMissing}},
        {"WingetMissing", std::string{WingetMissing}},
        {"Done", std::string{Done}},
        {"Failed", std::string{Failed}},
        {"InProgress", std::string{InProgress}},
    };
    return k;
}

} // namespace yeet17::core
