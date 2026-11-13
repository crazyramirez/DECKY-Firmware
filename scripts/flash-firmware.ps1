# Build, flash and (optionally) monitor one deck's firmware (environments: firmware/platformio.ini).
#   scripts/flash-firmware.ps1 -Env JC3248W535 -Port COM6 -Monitor   # product UI + serial log
#   scripts/flash-firmware.ps1 -Env JC1060P470C -Port COM3
#   scripts/flash-firmware.ps1 -Env JC1060P470C_r3                   # boards with an ESP32-P4 v3.x
#   scripts/flash-firmware.ps1 -Env hwtest_JC4827W543                # hardware test
# The PC app (DECKY Agent) does the same without PlatformIO: Update deck firmware.
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("JC1060P470C", "JC1060P470C_r3", "JC4827W543", "JC3248W535",
                 "hwtest_JC1060P470C", "hwtest_JC4827W543", "hwtest_JC3248W535")][string]$Env,
    [string]$Port = "",
    [switch]$Monitor
)
$ErrorActionPreference = "Stop"
$fw = Join-Path (Split-Path -Parent $PSScriptRoot) "firmware"
Push-Location $fw
try {
    $portArgs = @()
    if ($Port) { $portArgs = @("--upload-port", $Port) }
    pio run -e $Env -t upload @portArgs
    if ($LASTEXITCODE -ne 0) { throw "upload failed (exit $LASTEXITCODE)" }
    if ($Monitor) {
        $mon = @("device", "monitor", "-e", $Env)
        if ($Port) { $mon += @("--port", $Port) }
        pio @mon
    }
} finally {
    Pop-Location
}
