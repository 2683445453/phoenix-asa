[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('Core','Gameplay','Graphics')][string]$Suite)

$ErrorActionPreference = 'Stop'
$groups = @{
    Core = @('aim-selection','auto-craft','backpack-tools','instant-reload','interface','note-experiment','shortcut-runtime')
    Gameplay = @('buff-soup','dino-harvest-range','feature-expansion','gameplay-extensions','harvest-efficiency',
        'native-camera','tek-spear','underwater-performance','underwater-weapons','weapon-water-access')
    Graphics = @('dx12-backend','independent-overlay','interface-render','overlay-runtime','respawn-notification',
        'swap-chain-queue-tracker','vibrancy-gpu','vmt-reinject','window-procedure-routes')
}
$logDir = Join-Path $PSScriptRoot "..\release\ci\$Suite"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
foreach ($name in $groups[$Suite]) {
    $log = Join-Path $logDir "$name.log"
    Write-Host "Running $name..."
    try {
        & (Join-Path $PSScriptRoot "run-$name-tests.ps1") *> $log
        if ($LASTEXITCODE -ne 0) { throw "Exit code $LASTEXITCODE" }
    } catch {
        Get-Content -LiteralPath $log -Tail 60
        throw "$name failed. $($_.Exception.Message)"
    }
    Write-Host "PASS $name"
}
Write-Host "$Suite suite passed ($($groups[$Suite].Count) test scripts)."
