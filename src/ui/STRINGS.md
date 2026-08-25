# Строки UI — ключи для `resources/strings/ru-RU/Resources.resw`

Core владеет `.resw` и `core::Strings`. Здесь только список ключей, которые
оболочка уже показывает (или будет показывать), чтобы PRI совпал с таблицей.
Значения — только русский.

XAML сейчас зашит по-русски напрямую. Когда Core заполнит `.resw`, страницы
могут перейти на `x:Uid` / `Localization::Get`.

| Ключ | Значение |
|---|---|
| AppDisplayName | YEET17PCSET / YEET17 Настройка ПК |
| WindowTitle | YEET17PCSET — Настройка Windows |
| PaneTitle | Настройка |
| NavInstall | Установка |
| NavTweaks | Твики |
| NavSystem | Система |
| NavUpdates | Обновления |
| PageInstallTitle | Установка |
| PageInstallSubtitle | Программы через winget |
| PageTweaksTitle | Твики |
| PageTweaksSubtitle | Приватность, телеметрия, интерфейс |
| PageConfigTitle | Система |
| PageConfigSubtitle | Компоненты Windows и исправления |
| PageUpdatesTitle | Обновления |
| PageUpdatesSubtitle | Политика Windows Update |
| Theme | Тема |
| ThemeSystem | Системная |
| ThemeDark | Тёмная |
| ThemeLight | Светлая |
| ThemeCycleHint | Системная, тёмная, светлая |
| Save | Сохранить |
| Load | Загрузить |
| SaveConfig | Сохранить конфиг |
| LoadConfig | Загрузить конфиг |
| SearchPlaceholder | Поиск программ… |
| InstallSelected | Установить выбранное |
| UpgradeSelected | Обновить |
| UninstallSelected | Удалить |
| RefreshInstalled | Получить установленные |
| Apply | Применить |
| Undo | Отменить |
| PresetMinimal | Минимальный |
| PresetStandard | Стандартный |
| PresetMaximal | Максимальный |
| SectionEssential | Основные |
| SectionAdvanced | Продвинутые |
| AdvancedWarning | Продвинутые твики меняют глубокие политики и службы. Создайте точку восстановления и читайте описание перед применением. |
| RepairNetwork | Сброс сети |
| RepairWu | Сброс Windows Update |
| RepairSfcDism | SFC + DISM |
| RepairWinget | Восстановить Winget |
| UpdateFull | Все обновления |
| UpdateSecurity | Только безопасность |
| UpdatePause | Приостановить |
| UpdateReset | Сбросить к умолчанию |
| NeedAdmin | Требуются права администратора |
| ElevationPrompt | YEET17PCSET меняет политики и службы. Подтвердите повышение прав. |
| RestorePointCaption | Точка восстановления YEET17PCSET |
| ConfirmDangerous | Этот твик помечен как опасный. Продолжить? |
| LogEmpty | Журнал операций появится здесь. |
| CatalogMissing | Каталог не найден рядом с YEET17PCSET.exe |
| WingetMissing | winget.exe не найден. Установите «Установщик приложений» из Microsoft Store. |
| Done | Готово |
| Failed | Ошибка |
| InProgress | Выполняется… |
| PageInstallPlaceholder | Список программ появится здесь. |
| PageTweaksPlaceholder | Список твиков появится здесь. |
| PageConfigPlaceholder | Компоненты и исправления появятся здесь. |
| PageUpdatesPlaceholder | Политика обновлений появится здесь. |

Не создавать этот `.resw` из UI-агента — Localization принадлежит Core.
