# ============================================================
#  Flash ESP32 directly with esptool (no DTR/RTS fiddling)
# ------------------------------------------------------------
#  Why: this board's auto-reset circuit is unreliable, and driving
#  DTR/RTS repeatedly can put the CP2102 into a stuck state that
#  reports "Write timeout". So this script does NOT touch the
#  control lines at all. It assumes you already put the chip into
#  download mode by hand:
#
#      1) unplug USB
#      2) hold BOOT
#      3) plug USB back in
#      4) release BOOT
#      5) run this script
#
#  Usage:
#      powershell -ExecutionPolicy Bypass -File tools\flash.ps1
#      powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Port COM5
#
#  Kept ASCII-only so Windows PowerShell 5.1 can parse it.
# ============================================================

param(
    [string]$Port = 'COM3',
    [int]$Baud = 460800
)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$esp  = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptool\esptool.exe'
$boot0 = Join-Path $env:USERPROFILE '.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin'

if (-not (Test-Path $esp)) {
    Write-Host "[!] esptool not found: $esp" -ForegroundColor Red
    exit 1
}

# ---- kill leftover serial monitor processes holding the port ----
$stale = Get-CimInstance Win32_Process |
    Where-Object { $_.CommandLine -match 'device monitor' -and $_.Name -match 'python' }
if ($stale) {
    foreach ($p in $stale) {
        try {
            Stop-Process -Id $p.ProcessId -Force -ErrorAction Stop
            Write-Host "Killed leftover monitor PID $($p.ProcessId)" -ForegroundColor DarkGray
        } catch { }
    }
    Start-Sleep -Milliseconds 500
}

# ---- build (in case sources changed) ----
Write-Host "Building ..." -ForegroundColor Cyan
Push-Location $root
& (Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe') run -s
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Host "[FAIL] Build failed" -ForegroundColor Red
    exit 1
}

$b = Join-Path $root '.pio\build\esp32dev'

# ---- flash (no reset lines touched) ----
Write-Host "Flashing $Port at $Baud ..." -ForegroundColor Cyan
& $esp --chip esp32 --port $Port --baud $Baud --before default-reset --after hard-reset write-flash -z `
    --flash-mode dio --flash-freq 40m --flash-size detect `
    0x1000  (Join-Path $b 'bootloader.bin') `
    0x8000  (Join-Path $b 'partitions.bin') `
    0xe000  $boot0 `
    0x10000 (Join-Path $b 'firmware.bin')
$code = $LASTEXITCODE
Pop-Location

if ($code -eq 0) {
    Write-Host ""
    Write-Host "[OK] Flash succeeded" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[FAIL] Flash failed (exit $code)" -ForegroundColor Red
    Write-Host "If it says 'Write timeout': unplug USB, wait 3s, replug (this resets the CP2102)," -ForegroundColor Yellow
    Write-Host "then hold BOOT while replugging and run this script again." -ForegroundColor Yellow
}
exit $code
