#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace yeet17::core {

// Central Russian string table. WinUI loads Resources.resw first; this map
// is the fallback so a missing PRI / unpackaged resource lookup cannot leak
// English (or empty) strings onto the glass UI.
struct Strings {
    static constexpr std::string_view AppDisplayName        = "YEET17PCSET / YEET17 Настройка ПК";
    static constexpr std::string_view WindowTitle           = "YEET17PCSET — Настройка Windows";
    static constexpr std::string_view NavInstall            = "Установка";
    static constexpr std::string_view NavTweaks             = "Твики";
    static constexpr std::string_view NavSystem             = "Система";
    static constexpr std::string_view NavUpdates            = "Обновления";
    static constexpr std::string_view Theme                 = "Тема";
    static constexpr std::string_view ThemeSystem           = "Системная";
    static constexpr std::string_view ThemeDark             = "Тёмная";
    static constexpr std::string_view ThemeLight            = "Светлая";
    static constexpr std::string_view SaveConfig            = "Сохранить конфиг";
    static constexpr std::string_view LoadConfig            = "Загрузить конфиг";
    static constexpr std::string_view SearchPlaceholder     = "Поиск программ…";
    static constexpr std::string_view InstallSelected       = "Установить выбранное";
    static constexpr std::string_view UpgradeSelected       = "Обновить";
    static constexpr std::string_view UninstallSelected     = "Удалить";
    static constexpr std::string_view RefreshInstalled      = "Получить установленные";
    static constexpr std::string_view Apply                 = "Применить";
    static constexpr std::string_view Undo                  = "Отменить";
    static constexpr std::string_view PresetMinimal         = "Минимальный";
    static constexpr std::string_view PresetStandard        = "Стандартный";
    static constexpr std::string_view PresetMaximal         = "Максимальный";
    static constexpr std::string_view SectionEssential      = "Основные";
    static constexpr std::string_view SectionAdvanced       = "Продвинутые";
    static constexpr std::string_view AdvancedWarning       = "Продвинутые твики меняют глубокие политики и службы. Создайте точку восстановления и читайте описание перед применением.";
    static constexpr std::string_view RepairNetwork         = "Сброс сети";
    static constexpr std::string_view RepairWu              = "Сброс Windows Update";
    static constexpr std::string_view RepairSfcDism         = "SFC + DISM";
    static constexpr std::string_view RepairWinget          = "Восстановить Winget";
    static constexpr std::string_view UpdateFull            = "Все обновления";
    static constexpr std::string_view UpdateSecurity        = "Только безопасность";
    static constexpr std::string_view UpdatePause           = "Приостановить";
    static constexpr std::string_view UpdateReset           = "Сбросить к умолчанию";
    static constexpr std::string_view NeedAdmin             = "Требуются права администратора";
    static constexpr std::string_view ElevationPrompt       = "YEET17PCSET меняет политики и службы. Подтвердите повышение прав.";
    static constexpr std::string_view RestorePointCaption   = "Точка восстановления YEET17PCSET";
    static constexpr std::string_view ConfirmDangerous      = "Этот твик помечен как опасный. Продолжить?";
    static constexpr std::string_view LogEmpty              = "Журнал операций появится здесь.";
    static constexpr std::string_view CatalogMissing        = "Каталог не найден рядом с YEET17PCSET.exe";
    static constexpr std::string_view WingetMissing         = "winget.exe не найден. Установите «Установщик приложений» из Microsoft Store.";
    static constexpr std::string_view Done                  = "Готово";
    static constexpr std::string_view Failed                = "Ошибка";
    static constexpr std::string_view InProgress            = "Выполняется…";

    static const std::unordered_map<std::string, std::string>& Table();

    // Thin lookup over Table(); keys are the field names above.
    static std::string GetUtf8(std::string_view key) {
        const auto& table = Table();
        if (auto it = table.find(std::string{key}); it != table.end()) {
            return it->second;
        }
        return std::string{key};
    }
    static std::string Get(std::string_view key) { return GetUtf8(key); }
};


} // namespace yeet17::core
