# YEET17PCSET web installer
#   irm https://scarrymany.github.io/YEET17PCSET/install.ps1 | iex
# Downloads the latest GitHub release and installs it: MSI when the release
# ships one (Program Files + Start Menu + proper uninstall), zip fallback
# into %LOCALAPPDATA%\Programs otherwise.

$ErrorActionPreference = 'Stop'
$repo = 'scarrymany/YEET17PCSET'

Write-Host ''
Write-Host '  YEET17PCSET — установка' -ForegroundColor Cyan
Write-Host "  https://github.com/$repo"
Write-Host ''

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$release = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest" `
    -Headers @{ 'User-Agent' = 'YEET17PCSET-Install' }
$tag = $release.tag_name

$msi = $release.assets | Where-Object { $_.name -like '*.msi' } | Select-Object -First 1
if ($msi) {
    $path = Join-Path $env:TEMP $msi.name
    Write-Host "  Загрузка $($msi.name) ($tag)..."
    Invoke-WebRequest $msi.browser_download_url -OutFile $path -UseBasicParsing
    Write-Host '  Установка — подтвердите запрос UAC...'
    $p = Start-Process msiexec -ArgumentList '/i', "`"$path`"", '/passive' -Wait -PassThru
    Remove-Item $path -ErrorAction SilentlyContinue
    if ($p.ExitCode -ne 0) { throw "msiexec завершился с кодом $($p.ExitCode)" }
    Write-Host ''
    Write-Host "  Готово: YEET17PCSET $tag установлен." -ForegroundColor Green
    Write-Host '  Запуск — ярлык YEET17PCSET в меню «Пуск».'
    return
}

$zip = $release.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
if (-not $zip) { throw "В релизе $tag нет установочных файлов" }
$zipPath = Join-Path $env:TEMP $zip.name
$dest = Join-Path $env:LOCALAPPDATA 'Programs\YEET17PCSET'
Write-Host "  Загрузка $($zip.name) ($tag)..."
Invoke-WebRequest $zip.browser_download_url -OutFile $zipPath -UseBasicParsing
Write-Host "  Распаковка в $dest..."
Expand-Archive -LiteralPath $zipPath -DestinationPath $dest -Force
Remove-Item $zipPath -ErrorAction SilentlyContinue
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut((Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\YEET17PCSET.lnk'))
$lnk.TargetPath = Join-Path $dest 'YEET17PCSET.exe'
$lnk.WorkingDirectory = $dest
$lnk.IconLocation = Join-Path $dest 'resources\app.ico'
$lnk.Save()
Write-Host ''
Write-Host "  Готово: YEET17PCSET $tag установлен в $dest" -ForegroundColor Green
Write-Host '  Запуск — ярлык YEET17PCSET в меню «Пуск».'
