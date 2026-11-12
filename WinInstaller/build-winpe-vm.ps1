param(
    [string]$AdkRoot,
    [string]$Out,
    [string]$VmDir = "$env:SystemDrive\PCInstallerVM\winpe-build",
    [int]$TimeoutMin = 40
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Out) { $Out = Join-Path $root 'build\winpe-components.wim' }
$vmName = 'PCInstaller-WinPE-Build'
$packages = 'WinPE-WMI', 'WinPE-NetFx', 'WinPE-Scripting', 'WinPE-PowerShell', 'WinPE-StorageWMI', 'WinPE-DismCmdlets'

function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "`nОШИБКА: $m" -ForegroundColor Red; exit 1 }

if (-not (Get-Command New-VM -ErrorAction SilentlyContinue)) { Fail 'Нужен Hyper-V (Windows Pro: «Компоненты Windows» → Hyper-V).' }
$adk = if ($AdkRoot) { $AdkRoot } else { Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Assessment and Deployment Kit' }
$pe = Join-Path $adk 'Windows Preinstallation Environment\amd64'
$srcWim = Join-Path $pe 'en-us\winpe.wim'
if (-not (Test-Path $srcWim)) { Fail "Не найден $srcWim" }
$dism = Join-Path $adk 'Deployment Tools\amd64\DISM\dism.exe'
if (-not (Test-Path $dism)) { $dism = 'dism.exe' }

function Dism([string]$line) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo $dism, $line
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "DISM $line (код $($p.ExitCode))" }
}

function New-Disk($path, $sizeGB, $fs, $label) {
    New-VHD -Path $path -SizeBytes ($sizeGB * 1GB) -Dynamic | Out-Null
    $disk = Mount-VHD -Path $path -Passthru | Get-Disk
    $dp = Join-Path $env:TEMP 'pcinstaller_build_vhd.txt'
    Set-Content $dp -Encoding ASCII -Value @"
select disk $($disk.Number)
convert gpt
create partition primary
format quick fs=$fs label="$label"
assign
"@
    & diskpart /s $dp | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail "diskpart не смог разметить $path" }
    Start-Sleep 2
    $part = Get-Partition -DiskNumber $disk.Number | Where-Object DriveLetter | Select-Object -First 1
    return @{ Disk = $disk.Number; Part = $part.PartitionNumber; Root = "$($part.DriveLetter):\" }
}

Step 'Готовлю временную VM'
if (Get-VM -Name $vmName -ErrorAction SilentlyContinue) { Stop-VM -Name $vmName -TurnOff -Force -ErrorAction SilentlyContinue; Remove-VM -Name $vmName -Force }
if (Test-Path $VmDir) { Remove-Item $VmDir -Recurse -Force }
New-Item -ItemType Directory -Force $VmDir | Out-Null
$bootVhd = Join-Path $VmDir 'boot.vhdx'
$dataVhd = Join-Path $VmDir 'data.vhdx'
$mount = Join-Path $VmDir 'mount'
New-Item -ItemType Directory -Force $mount | Out-Null

$plainWim = Join-Path $VmDir 'boot-plain.wim'
Copy-Item $srcWim $plainWim
Dism "/Mount-Image /ImageFile:`"$plainWim`" /Index:1 /MountDir:`"$mount`""
try {
    Set-Content (Join-Path $mount 'Windows\System32\startnet.cmd') -Encoding ASCII -Value @'
@echo off
wpeinit
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%d:\pebuild.cmd call %%d:\pebuild.cmd %%d:
wpeutil shutdown
'@
    $bootFiles = Join-Path $VmDir 'bootfiles'
    New-Item -ItemType Directory -Force $bootFiles | Out-Null
    Copy-Item (Join-Path $mount 'Windows\Boot\EFI\bootmgfw.efi'), (Join-Path $mount 'Windows\Boot\EFI\bootmgr.efi'), (Join-Path $mount 'Windows\Boot\PCAT\bootmgr') $bootFiles
    Dism "/Unmount-Image /MountDir:`"$mount`" /Commit"
}
catch { & $dism /Unmount-Image /MountDir:$mount /Discard | Out-Null; throw }

Step 'Загрузочный диск VM'
$b = New-Disk $bootVhd 1 'fat32' 'PEBOOT'
& robocopy (Join-Path $pe 'Media') $b.Root /E /NFL /NDL /NJH /NJS /NP | Out-Null
New-Item -ItemType Directory -Force (Join-Path $b.Root 'EFI\Boot'), (Join-Path $b.Root 'sources') | Out-Null
Copy-Item (Join-Path $bootFiles 'bootmgfw.efi') (Join-Path $b.Root 'EFI\Boot\bootx64.efi')
Copy-Item (Join-Path $bootFiles 'bootmgr.efi') $b.Root
Copy-Item (Join-Path $bootFiles 'bootmgr') $b.Root
Copy-Item $plainWim (Join-Path $b.Root 'sources\boot.wim')
$dp = Join-Path $env:TEMP 'pcinstaller_build_vhd.txt'
Set-Content $dp -Encoding ASCII -Value "select disk $($b.Disk)`r`nselect partition $($b.Part)`r`nset id=c12a7328-f81f-11d2-ba4b-00a0c93ec93b override"
& diskpart /s $dp | Out-Null
Dismount-VHD $bootVhd
Remove-Item $plainWim

Step 'Диск с пакетами'
$d = New-Disk $dataVhd 6 'ntfs' 'PEBUILD'
Copy-Item $srcWim (Join-Path $d.Root 'winpe.wim')
$ocs = Join-Path $d.Root 'OCs'
New-Item -ItemType Directory -Force (Join-Path $ocs 'en-us') | Out-Null
foreach ($p in $packages) {
    Copy-Item (Join-Path $pe "WinPE_OCs\$p.cab") $ocs
    $lp = Join-Path $pe "WinPE_OCs\en-us\${p}_en-us.cab"
    if (Test-Path $lp) { Copy-Item $lp (Join-Path $ocs 'en-us') }
}
$pkgList = $packages -join ' '
Set-Content (Join-Path $d.Root 'pebuild.cmd') -Encoding ASCII -Value @"
@echo off
set D=%1
set L=%D%\pebuild.log
md %D%\mount %D%\scratch
echo === mount > %L%
dism /English /Mount-Image /ImageFile:%D%\winpe.wim /Index:1 /MountDir:%D%\mount /ScratchDir:%D%\scratch >> %L% || goto fail
for %%p in ($pkgList) do (
  echo === %%p >> %L%
  dism /English /Image:%D%\mount /ScratchDir:%D%\scratch /Add-Package /PackagePath:%D%\OCs\%%p.cab >> %L% || goto fail
  if exist %D%\OCs\en-us\%%p_en-us.cab dism /English /Image:%D%\mount /ScratchDir:%D%\scratch /Add-Package /PackagePath:%D%\OCs\en-us\%%p_en-us.cab >> %L% || goto fail
)
echo === scratch >> %L%
dism /English /Image:%D%\mount /Set-ScratchSpace:512 >> %L% || goto fail
echo === commit >> %L%
dism /English /Unmount-Image /MountDir:%D%\mount /Commit /ScratchDir:%D%\scratch >> %L% || goto fail
echo OK > %D%\done.txt
goto :eof
:fail
echo FAIL >> %L%
dism /English /Unmount-Image /MountDir:%D%\mount /Discard >> %L%
"@
Dismount-VHD $dataVhd

Step 'Запускаю VM — сборка идёт внутри, окно открывать не нужно'
New-VM -Name $vmName -Generation 2 -MemoryStartupBytes 4GB -VHDPath $bootVhd -Path $VmDir | Out-Null
Add-VMHardDiskDrive -VMName $vmName -Path $dataVhd
Set-VMProcessor -VMName $vmName -Count 4
Set-VMMemory -VMName $vmName -DynamicMemoryEnabled $false
Set-VMFirmware -VMName $vmName -EnableSecureBoot On -SecureBootTemplate MicrosoftWindows -FirstBootDevice (Get-VMHardDiskDrive -VMName $vmName | Where-Object Path -eq $bootVhd)
Start-VM -Name $vmName
$deadline = (Get-Date).AddMinutes($TimeoutMin)
while ((Get-VM -Name $vmName).State -ne 'Off') {
    if ((Get-Date) -gt $deadline) { Stop-VM -Name $vmName -TurnOff -Force; Fail "VM не завершила сборку за $TimeoutMin мин." }
    Write-Host -NoNewline '.'
    Start-Sleep 10
}
Write-Host ''
Remove-VM -Name $vmName -Force

Step 'Забираю результат'
$disk = Mount-VHD -Path $dataVhd -Passthru | Get-Disk
try {
    Start-Sleep 2
    $r = "$((Get-Partition -DiskNumber $disk.Number | Where-Object DriveLetter | Select-Object -First 1).DriveLetter):\"
    $log = Join-Path $root 'build\winpe-build.log'
    New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
    if (Test-Path (Join-Path $r 'pebuild.log')) { Copy-Item (Join-Path $r 'pebuild.log') $log -Force }
    if (-not (Test-Path (Join-Path $r 'done.txt'))) { Fail "Сборка внутри VM не удалась. Журнал: $log" }
    Copy-Item (Join-Path $r 'winpe.wim') $Out -Force
}
finally { Dismount-VHD $dataVhd }
Remove-Item $VmDir -Recurse -Force
Write-Host "`nГотово: $Out" -ForegroundColor Green
Write-Host "Дальше: .\make-usb.ps1 -BootWim `"$Out`" -Iso <ISO Windows>"
