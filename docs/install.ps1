# YEET17PCSET web installer
#   irm https://scarrymany.github.io/YEET17PCSET/install.ps1 | iex
# Downloads the latest GitHub release and installs it: MSI when the release
# ships one (Program Files + Start Menu + proper uninstall), zip fallback
# into %LOCALAPPDATA%\Programs otherwise.

$ErrorActionPreference = 'Stop'
$repo = 'scarrymany/YEET17PCSET'

Write-Host ''
Write-Host '  YEET17PCSET installer' -ForegroundColor Cyan
Write-Host "  https://github.com/$repo"
Write-Host ''

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# The app is an unpackaged WinUI 3 binary: it needs the Windows App Runtime
# 1.6 framework packages, or it dies at startup with 0x80070016.
$runtime = Get-AppxPackage -Name 'Microsoft.WindowsAppRuntime.1.6*' -ErrorAction SilentlyContinue
if (-not $runtime) {
    Write-Host '  Installing Windows App Runtime 1.6...'
    $rtPath = Join-Path $env:TEMP 'windowsappruntimeinstall-x64.exe'
    Invoke-WebRequest 'https://aka.ms/windowsappsdk/1.6/latest/windowsappruntimeinstall-x64.exe' `
        -OutFile $rtPath -UseBasicParsing
    $rt = Start-Process $rtPath -ArgumentList '--quiet' -Wait -PassThru
    Remove-Item $rtPath -ErrorAction SilentlyContinue
    if ($rt.ExitCode -ne 0) {
        Write-Host "  Warning: runtime installer exited with code $($rt.ExitCode)" -ForegroundColor Yellow
    }
}

$release = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest" `
    -Headers @{ 'User-Agent' = 'YEET17PCSET-Install' }
$tag = $release.tag_name

$msi = $release.assets | Where-Object { $_.name -like '*.msi' } | Select-Object -First 1
if ($msi) {
    $path = Join-Path $env:TEMP $msi.name
    Write-Host "  Downloading $($msi.name) ($tag)..."
    Invoke-WebRequest $msi.browser_download_url -OutFile $path -UseBasicParsing
    Write-Host '  Installing - confirm the UAC prompt...'
    $p = Start-Process msiexec -ArgumentList '/i', "`"$path`"", '/passive' -Wait -PassThru
    Remove-Item $path -ErrorAction SilentlyContinue
    if ($p.ExitCode -ne 0) { throw "msiexec exited with code $($p.ExitCode)" }
    Write-Host ''
    Write-Host "  Done: YEET17PCSET $tag installed." -ForegroundColor Green
    Write-Host '  Launch it from the Start Menu (YEET17PCSET shortcut).'
    return
}

$zip = $release.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
if (-not $zip) { throw "Release $tag has no installable assets" }
$zipPath = Join-Path $env:TEMP $zip.name
$dest = Join-Path $env:LOCALAPPDATA 'Programs\YEET17PCSET'
Write-Host "  Downloading $($zip.name) ($tag)..."
Invoke-WebRequest $zip.browser_download_url -OutFile $zipPath -UseBasicParsing
Write-Host "  Extracting to $dest..."
Expand-Archive -LiteralPath $zipPath -DestinationPath $dest -Force
Remove-Item $zipPath -ErrorAction SilentlyContinue
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut((Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\YEET17PCSET.lnk'))
$lnk.TargetPath = Join-Path $dest 'YEET17PCSET.exe'
$lnk.WorkingDirectory = $dest
$lnk.IconLocation = Join-Path $dest 'resources\app.ico'
$lnk.Save()
Write-Host ''
Write-Host "  Done: YEET17PCSET $tag installed to $dest" -ForegroundColor Green
Write-Host '  Launch it from the Start Menu (YEET17PCSET shortcut).'
