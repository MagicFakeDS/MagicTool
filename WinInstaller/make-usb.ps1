param(

    [string[]]$Iso,

    [string[]]$Editions = @('Professional', 'Core'),
    [switch]$AllEditions,

    [ValidateSet('recovery', 'max')][string]$Compress = 'recovery',
    [int]$DiskNumber = -1,

    [switch]$Yes,
    [string]$IsoOut,
    [string]$VhdOut,

    [string]$AdkRoot,

    [string]$BootWim,
    [switch]$UpdateOnly,

    [string]$DataOut,

    [string]$DataFrom,
    [string]$DataDrive,
    [string]$WorkDir = "$env:SystemDrive\PCInstallerWork"
)

$ErrorActionPreference = 'Stop'

$Iso = @($Iso | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Editions = @($Editions | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$src      = Join-Path $root 'src'
$usb      = Join-Path $root 'usb'
$buildDir = Join-Path $root 'build'

function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "`nОШИБКА: $m" -ForegroundColor Red; exit 1 }

function Robo($from, $to, [string[]]$extra = @()) {
    if (-not (Test-Path $from)) { return }
    & robocopy $from $to /E /NFL /NDL /NJH /NJS /NP @extra | Out-Null
    if ($LASTEXITCODE -ge 8) { Fail "Не удалось скопировать $from -> $to (robocopy $LASTEXITCODE)" }
}

function Build-App {
    Step 'Собираю PCInstaller.exe'
    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        Fail '.NET SDK не найден. Установите: winget install Microsoft.DotNet.SDK.9'
    }
    & dotnet build (Join-Path $src 'PCInstaller.csproj') -c Release -o $buildDir --nologo -v q
    if ($LASTEXITCODE -ne 0) { Fail 'Сборка программы не удалась' }
}

function Copy-Data($dataRoot) {
    Step "Копирую программу, каталог, драйверы и установщики на $dataRoot"
    $inst = Join-Path $dataRoot 'installer'
    Robo (Join-Path $usb 'installer') $inst
    Copy-Item (Join-Path $buildDir 'PCInstaller.exe') $inst -Force
    $cfg = Join-Path $buildDir 'PCInstaller.exe.config'
    if (Test-Path $cfg) { Copy-Item $cfg $inst -Force }
    Robo (Join-Path $usb 'Drivers')  (Join-Path $dataRoot 'Drivers')
    Sync-ToolboxPC
    Robo (Join-Path $usb 'Programs') (Join-Path $dataRoot 'Programs')
    foreach ($f in 'REPAIR-WINDOWS.cmd', 'TOOLBOXPC.cmd') {
        $p = Join-Path $usb $f
        if (Test-Path $p) { Copy-Item $p $dataRoot -Force }
    }
}

function Sync-ToolboxPC {
    $exe   = Join-Path $root '..\ToolboxPC\ToolboxPC.exe'
    $tools = Join-Path $root '..\tools'
    $dst   = Join-Path $usb 'Programs\ToolboxPC'
    if (-not (Test-Path $exe)) {
        if (-not (Test-Path (Join-Path $dst 'ToolboxPC.exe'))) { Write-Warning 'ToolboxPC.exe не найден — карточка TOOLBOX PC будет серой. Соберите ..\ToolboxPC\build.bat.' }
        return
    }
    New-Item -ItemType Directory -Force $dst | Out-Null
    Copy-Item $exe $dst -Force
    Robo $tools (Join-Path $dst 'tools') @('/XF', 'avz4.zip')

    $data = Join-Path $root '..\ToolboxPC\data'
    if (Test-Path $data) { Robo $data (Join-Path $dst 'data') }
}

$adk = if ($AdkRoot) { $AdkRoot } else { Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Assessment and Deployment Kit' }
$dism = Join-Path $adk 'Deployment Tools\amd64\DISM\dism.exe'
if (-not (Test-Path $dism)) { $dism = 'dism.exe' }

function Dism([string[]]$a) {

    $line = ($a | ForEach-Object {
        if ($_ -match '^(/[^:]+:)(.*\s.*)$') { $matches[1] + '"' + $matches[2] + '"' }
        elseif ($_ -match '\s') { '"' + $_ + '"' }
        else { $_ }
    }) -join ' '

    $psi = New-Object System.Diagnostics.ProcessStartInfo $dism, $line
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "DISM: $line (код $($p.ExitCode))" }
}

function Copy-WindowsImage($dataRoot) {
    if (-not $Iso) {
        Write-Warning 'ISO Windows не указан (-Iso). Положите install.wim / install.esd в папку sources на разделе DATA вручную.'
        return
    }
    foreach ($isoPath in $Iso) {
        if (-not (Test-Path $isoPath)) { Fail "Не найден файл $isoPath" }
        $full = (Resolve-Path $isoPath).Path
        $img = Mount-DiskImage -ImagePath $full -PassThru
        try {
            $l = ($img | Get-Volume).DriveLetter
            $srcWim = @('install.wim', 'install.esd') | ForEach-Object { "${l}:\sources\$_" } | Where-Object { Test-Path $_ } | Select-Object -First 1
            if (-not $srcWim) { Fail "В $isoPath нет sources\install.wim или install.esd" }
            $all = @(Get-WindowsImage -ImagePath $srcWim | ForEach-Object { Get-WindowsImage -ImagePath $srcWim -Index $_.ImageIndex })
            $os = if ([version]$all[0].Version -ge [version]'10.0.22000') { 'win11' } else { 'win10' }
            $dst = Join-Path $dataRoot $os

            Step "Windows $($os.Substring(3)): копирую установочные файлы из $(Split-Path $isoPath -Leaf)"
            & robocopy "${l}:\" $dst /E /NFL /NDL /NJH /NJS /NP /XF install.wim install.esd boot.wim /XD 'System Volume Information' '$RECYCLE.BIN' | Out-Null
            if ($LASTEXITCODE -ge 8) { Fail 'Не удалось скопировать установочные файлы' }

            $sel = if ($AllEditions) { $all } else { @($all | Where-Object { $Editions -contains $_.EditionId }) }
            if ($sel.Count -eq 0) {
                Write-Warning ("В образе нет редакций {0}. Есть: {1}" -f ($Editions -join ', '), (($all | ForEach-Object EditionId) -join ', '))
                $sel = $all
            }
            $out = Join-Path $dst ('sources\install.' + $(if ($Compress -eq 'recovery') { 'esd' } else { 'wim' }))
            Remove-Item (Join-Path $dst 'sources\install.esd'), (Join-Path $dst 'sources\install.wim') -Force -ErrorAction SilentlyContinue
            foreach ($e in $sel) {
                Step "Сжимаю редакцию «$($e.ImageName)» в install.esd (10–20 минут)"
                Dism @('/Export-Image', "/SourceImageFile:$srcWim", "/SourceIndex:$($e.ImageIndex)", "/DestinationImageFile:$out", "/Compress:$Compress", '/CheckIntegrity')
            }
            Write-Host ("   {0}: {1:N1} ГБ, редакций: {2}" -f $out, ((Get-Item $out).Length / 1GB), $sel.Count)
        }
        finally { Dismount-DiskImage -ImagePath $full | Out-Null }
    }
    Copy-Item (Join-Path $root 'usb\REPAIR-WINDOWS.cmd') $dataRoot -Force -ErrorAction SilentlyContinue
}

if ($DataOut) {
    New-Item -ItemType Directory -Force $DataOut | Out-Null
    Build-App
    Copy-Data $DataOut
    Copy-WindowsImage $DataOut
    $gb = (Get-ChildItem $DataOut -Recurse -File | Measure-Object Length -Sum).Sum / 1GB
    Write-Host ("`nГотово: {0} — {1:N1} ГБ. Флешке нужно ещё 1,2 ГБ под BOOT, итого {2:N1} ГБ." -f $DataOut, $gb, ($gb + 1.2)) -ForegroundColor Green
    Write-Host "Записать на флешку: .\make-usb.ps1 -DataFrom `"$DataOut`" ..."
    exit 0
}

if ($UpdateOnly) {
    if (-not $DataDrive) { Fail 'Укажите раздел DATA флешки: -DataDrive E:' }
    $dataRoot = $DataDrive.TrimEnd('\') + '\'
    if (-not (Test-Path (Join-Path $dataRoot 'installer'))) { Fail "$dataRoot не похож на раздел DATA флешки (нет папки installer)" }
    Build-App
    Copy-Data $dataRoot
    if ($Iso) { Copy-WindowsImage $dataRoot }
    Write-Host "`nГотово: флешка обновлена." -ForegroundColor Green
    exit 0
}

$pe  = Join-Path $adk 'Windows Preinstallation Environment\amd64'
if (-not (Test-Path (Join-Path $pe 'en-us\winpe.wim'))) {
    Fail ("Не найден Windows PE.`n" +
          "Установите с https://learn.microsoft.com/windows-hardware/get-started/adk-install :`n" +
          "  1) Windows ADK (достаточно компонента Deployment Tools)`n" +
          "  2) Windows PE add-on for the ADK")
}
$oscdimgDir = Join-Path $adk 'Deployment Tools\amd64\Oscdimg'
$bootsect   = Join-Path $adk 'Deployment Tools\amd64\BCDBoot\bootsect.exe'

Build-App

Step 'Готовлю рабочую папку'
$media = Join-Path $WorkDir 'media'
$mount = Join-Path $WorkDir 'mount'
if (Test-Path $mount) { & $dism /Unmount-Image /MountDir:$mount /Discard 2>$null | Out-Null }
if (Test-Path $WorkDir) { Remove-Item $WorkDir -Recurse -Force }
New-Item -ItemType Directory $media, $mount | Out-Null
Robo (Join-Path $pe 'Media') $media
New-Item -ItemType Directory -Force (Join-Path $media 'sources') | Out-Null
$wim = Join-Path $WorkDir 'winpe.wim'
if ($BootWim) {
    if (-not (Test-Path $BootWim)) { Fail "Не найден $BootWim" }
    Copy-Item $BootWim $wim
}
else { Copy-Item (Join-Path $pe 'en-us\winpe.wim') $wim }

Step 'Собираю WinPE: .NET, WMI, PowerShell, управление дисками (5–10 минут)'
Dism @('/Mount-Image', "/ImageFile:$wim", '/Index:1', "/MountDir:$mount")
try {
    $ocs = Join-Path $pe 'WinPE_OCs'
    $pkgs = if ($BootWim) { @() } else { 'WinPE-WMI', 'WinPE-NetFx', 'WinPE-Scripting', 'WinPE-PowerShell', 'WinPE-StorageWMI', 'WinPE-DismCmdlets' }
    foreach ($p in $pkgs) {
        Write-Host "   + $p"
        Dism @("/Image:$mount", '/Add-Package', "/PackagePath:$(Join-Path $ocs "$p.cab")")
        $lp = Join-Path $ocs "en-us\${p}_en-us.cab"
        if (Test-Path $lp) { Dism @("/Image:$mount", '/Add-Package', "/PackagePath:$lp") }
    }

    $peInst = Join-Path $mount 'Installer'
    Robo (Join-Path $usb 'installer') $peInst
    Copy-Item (Join-Path $buildDir 'PCInstaller.exe') $peInst -Force

    $startnet = @'
@echo off
wpeinit
powercfg /s 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c >nul 2>&1
title PCInstaller
rem Drivers\WinPE on the stick: storage controllers (Intel RST/VMD, AMD RAID, NVMe) so disks are visible on any PC
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist "%%d:\Drivers\WinPE\" (
  echo Loading drivers from %%d:\Drivers\WinPE ...
  for /r "%%d:\Drivers\WinPE" %%f in (*.inf) do drvload "%%f" >nul 2>&1
)
set tries=0
:find
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist "%%d:\installer\PCInstaller.exe" (
  "%%d:\installer\PCInstaller.exe" --data %%d:\
  goto end
)
set /a tries+=1
if %tries% lss 10 (
  ping -n 2 127.0.0.1 >nul
  goto find
)
X:\Installer\PCInstaller.exe
:end
echo.
echo Installer closed. Type PCInstaller to start again, "wpeutil reboot" to restart.
'@
    Set-Content -Path (Join-Path $mount 'Windows\System32\startnet.cmd') -Value $startnet -Encoding ASCII
    Set-Content -Path (Join-Path $mount 'Windows\System32\PCInstaller.cmd') -Encoding ASCII -Value @'
@echo off
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist "%%d:\installer\PCInstaller.exe" ("%%d:\installer\PCInstaller.exe" --data %%d:\ & goto :eof)
X:\Installer\PCInstaller.exe
'@

    $bootDir = Join-Path $mount 'Windows\Boot'
    $efiBoot = Join-Path $media 'EFI\Boot'
    New-Item -ItemType Directory -Force $efiBoot | Out-Null
    if (-not (Test-Path (Join-Path $efiBoot 'bootx64.efi'))) { Copy-Item (Join-Path $bootDir 'EFI\bootmgfw.efi') (Join-Path $efiBoot 'bootx64.efi') }
    if (-not (Test-Path (Join-Path $media 'bootmgr.efi')))   { Copy-Item (Join-Path $bootDir 'EFI\bootmgr.efi') (Join-Path $media 'bootmgr.efi') }
    if (-not (Test-Path (Join-Path $media 'bootmgr')))       { Copy-Item (Join-Path $bootDir 'PCAT\bootmgr') (Join-Path $media 'bootmgr') }

    if (-not $BootWim) { Dism @("/Image:$mount", '/Set-ScratchSpace:512') }
    Dism @('/Unmount-Image', "/MountDir:$mount", '/Commit')
}
catch {
    & $dism /Unmount-Image /MountDir:$mount /Discard | Out-Null
    throw
}
Dism @('/Export-Image', "/SourceImageFile:$wim", '/SourceIndex:1', "/DestinationImageFile:$(Join-Path $media 'sources\boot.wim')", '/Bootable')

if ($IsoOut) {
    Step "Собираю ISO $IsoOut"
    Copy-Data $media
    Copy-WindowsImage $media
    $etfs = Join-Path $WorkDir 'etfsboot.com'
    $efis = Join-Path $WorkDir 'efisys.bin'
    Copy-Item (Join-Path $oscdimgDir 'etfsboot.com') $etfs
    Copy-Item (Join-Path $oscdimgDir 'efisys_noprompt.bin') $efis
    & (Join-Path $oscdimgDir 'oscdimg.exe') -m -o -u2 -udfver102 -lPCINSTALLER "-bootdata:2#p0,e,b$etfs#pEF,e,b$efis" $media $IsoOut
    if ($LASTEXITCODE -ne 0) { Fail 'oscdimg не смог собрать ISO' }
    Write-Host "`nГотово: $IsoOut" -ForegroundColor Green
    exit 0
}

if ($VhdOut) {
    Step "Собираю виртуальный диск $VhdOut"
    if (Test-Path $VhdOut) { Fail "Файл уже существует: $VhdOut" }
    New-Item -ItemType Directory -Force (Split-Path -Parent $VhdOut) | Out-Null
    New-VHD -Path $VhdOut -SizeBytes 4GB -Dynamic | Out-Null
    $vdisk = Mount-VHD -Path $VhdOut -Passthru | Get-Disk
    try {
        $dp = Join-Path $env:TEMP 'pcinstaller_vhd.txt'
        Set-Content $dp -Encoding ASCII -Value @"
select disk $($vdisk.Number)
convert gpt
create partition primary size=1024
format quick fs=fat32 label="BOOT"
assign
create partition primary
format quick fs=ntfs label="DATA"
assign
"@
        & diskpart /s $dp | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail 'diskpart не смог разметить виртуальный диск' }
        Start-Sleep 2
        $parts = @(Get-Partition -DiskNumber $vdisk.Number | Where-Object DriveLetter | Sort-Object Offset)
        $bootPart = $parts[0]
        $boot = "$($parts[0].DriveLetter):\"
        $data = "$($parts[1].DriveLetter):\"
        Robo $media $boot
        Copy-Data $data
        if ($Iso) { Copy-WindowsImage $data }

        Set-Content $dp -Encoding ASCII -Value @"
select disk $($vdisk.Number)
select partition $($bootPart.PartitionNumber)
set id=c12a7328-f81f-11d2-ba4b-00a0c93ec93b override
"@
        & diskpart /s $dp | Out-Null
        Remove-Item $dp -Force
    }
    finally { Dismount-VHD -Path $VhdOut }
    Write-Host "`nГотово: $VhdOut — подключите его первым диском VM поколения 2 (Secure Boot: шаблон «Microsoft Windows»)." -ForegroundColor Green
    exit 0
}

Step 'Выбор флешки'
if ($DiskNumber -lt 0) {
    $cands = @(Get-Disk | Where-Object { $_.BusType -eq 'USB' })
    if ($cands.Count -eq 0) { Fail 'USB-накопители не найдены. Вставьте флешку (от 16 ГБ).' }
    $cands | Format-Table Number, FriendlyName, @{ n = 'Размер, ГБ'; e = { [math]::Round($_.Size / 1GB, 1) } }, PartitionStyle -AutoSize | Out-Host
    $DiskNumber = [int](Read-Host 'Номер диска флешки')
}
$disk = Get-Disk -Number $DiskNumber
if ($disk.IsBoot -or $disk.IsSystem) { Fail "Диск $DiskNumber — системный. Отмена." }
if ($disk.BusType -ne 'USB') { Write-Warning "Диск $DiskNumber подключён не по USB ($($disk.BusType))." }
if ($disk.Size -lt 8GB) { Fail 'Флешка слишком маленькая: нужно минимум 8 ГБ (лучше 16+).' }
Write-Host ("`nВСЕ ДАННЫЕ на диске {0} «{1}» ({2} ГБ) будут удалены." -f $DiskNumber, $disk.FriendlyName, [math]::Round($disk.Size / 1GB)) -ForegroundColor Yellow
if (-not $Yes -and (Read-Host 'Напишите YES для продолжения') -ne 'YES') { Write-Host 'Отменено.'; exit 1 }

Step 'Размечаю флешку: BOOT (FAT32, 1,2 ГБ) + DATA (NTFS)'
$dp = Join-Path $env:TEMP 'pcinstaller_usb.txt'
Set-Content $dp -Encoding ASCII -Value @"
select disk $DiskNumber
clean
convert mbr
create partition primary size=1200
format quick fs=fat32 label="BOOT"
active
assign
create partition primary
format quick fs=ntfs label="DATA"
assign
"@
& diskpart /s $dp | Out-Host
if ($LASTEXITCODE -ne 0) { Fail 'diskpart не смог разметить флешку' }
Remove-Item $dp -Force
Start-Sleep 3
Update-HostStorageCache
$parts = Get-Partition -DiskNumber $DiskNumber | Sort-Object PartitionNumber
$boot = "$($parts[0].DriveLetter):\"
$data = "$($parts[1].DriveLetter):\"
if ($boot.Length -ne 3 -or $data.Length -ne 3) { Fail 'Разделам флешки не назначились буквы' }

if (Test-Path $bootsect) { & $bootsect /nt60 $boot.TrimEnd('\') /force /mbr | Out-Null }

Step "Копирую WinPE на $boot"
Robo $media $boot

if ($DataFrom) {
    Step "Копирую готовое содержимое DATA из $DataFrom"
    Robo $DataFrom $data
    Copy-Data $data
}
else {
    Copy-Data $data
    Copy-WindowsImage $data
}

Write-Host "`nГотово! Загрузитесь с флешки (меню загрузки: F8 / F11 / F12 / Esc — зависит от платы)." -ForegroundColor Green
Write-Host "Обновить программу/каталог позже: .\make-usb.ps1 -UpdateOnly -DataDrive $($data.TrimEnd('\'))"
