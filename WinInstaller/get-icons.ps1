param([switch]$Force, [switch]$NoDownload)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$catPath = Join-Path $root 'usb\installer\catalog.json'
$iconDir = Join-Path $root 'usb\installer\assets\icons'
New-Item -ItemType Directory -Force $iconDir | Out-Null

$json = (Get-Content $catPath -Raw -Encoding UTF8) -split "`n" | Where-Object { $_ -notmatch '^\s*//' }
$cat  = ($json -join "`n") | ConvertFrom-Json
$items = foreach ($c in $cat.categories) { $c.items }

$pf = $env:ProgramFiles; $pf86 = ${env:ProgramFiles(x86)}; $la = $env:LOCALAPPDATA; $ra = $env:APPDATA
$local = @{
    'chrome'          = @("$pf\Google\Chrome\Application\chrome.exe")
    'firefox'         = @("$pf\Mozilla Firefox\firefox.exe")
    'yandex'          = @("$la\Yandex\YandexBrowser\Application\browser.exe")
    '7zip'            = @("$pf\7-Zip\7zFM.exe")
    'steam'           = @("$pf86\Steam\steam.exe")
    'epic'            = @("$pf86\Epic Games\Launcher\Portal\Binaries\Win64\EpicGamesLauncher.exe")
    'discord'         = @("$la\Discord\Update.exe")
    'telegram'        = @("$ra\Telegram Desktop\Telegram.exe")
    'vlc'             = @("$pf\VideoLAN\VLC\vlc.exe")
    'notepadpp'       = @("$pf\Notepad++\notepad++.exe")
    'obs'             = @("$pf\obs-studio\bin\64bit\obs64.exe")
    'afterburner'     = @("$pf86\MSI Afterburner\MSIAfterburner.exe")
    'anydesk'         = @("$pf\AnyDesk\AnyDesk.exe", "$pf86\AnyDesk\AnyDesk.exe")
    'powertoys'       = @("$pf\PowerToys\PowerToys.exe")
    'crystaldiskinfo' = @("$pf\CrystalDiskInfo\DiskInfo64.exe")
    'gog'             = @("$pf86\GOG Galaxy\GalaxyClient.exe")
    'battlenet'       = @("$pf86\Battle.net\Battle.net Launcher.exe")
    'ea'              = @("$pf\Electronic Arts\EA Desktop\EA Desktop\EADesktop.exe")
    'ubisoft'         = @("$pf86\Ubisoft\Ubisoft Game Launcher\UbisoftConnect.exe")
    'ghub'            = @("$pf\LGHUB\lghub.exe")

    'rustdesk'        = @("$root\usb\Programs\ToolboxPC\tools\rustdesk\rustdesk.exe", "$pf\RustDesk\rustdesk.exe")
}

$done = 0; $miss = @()
foreach ($it in $items) {
    $out = Join-Path $iconDir "$($it.id).png"
    if ((Test-Path $out) -and -not $Force) { continue }

    $exe = $local[$it.id] | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if ($exe) {
        $ico = [System.Drawing.Icon]::ExtractAssociatedIcon($exe)
        $bmp = $ico.ToBitmap()
        $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose(); $ico.Dispose()
        Write-Host "  [ПК]   $($it.name)"
        $done++
        continue
    }

    if ($NoDownload -or -not $it.site) { $miss += $it.name; continue }
    try {
        $domain = ([uri]$it.site).Host
        $url = "https://www.google.com/s2/favicons?domain=$domain&sz=128"
        Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing -TimeoutSec 20
        Write-Host "  [сеть] $($it.name)  ($domain)"
        $done++
    }
    catch { $miss += $it.name; Remove-Item $out -ErrorAction SilentlyContinue }
}

Write-Host "`nГотово: иконок $done. Папка: $iconDir" -ForegroundColor Green
if ($miss.Count) { Write-Host ("Без иконки (будет буква на градиенте): " + ($miss -join ', ')) -ForegroundColor Yellow }
