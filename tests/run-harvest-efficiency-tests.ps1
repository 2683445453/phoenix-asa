$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release\tests'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$harvestRuntimeOutput = Join-Path $testRoot 'release/harvest-performance'
New-Item -ItemType Directory -Path $harvestRuntimeOutput -Force | Out-Null
$harvestSource = Get-Content -LiteralPath (Join-Path $testRoot 'src/internal/Hack/Hack.cpp') -Raw -Encoding utf8
$harvestNavStart = $harvestSource.IndexOf('        bool RefreshAutoHarvestNavigationPath(')
$harvestNavEnd = $harvestSource.IndexOf('        SDK::FVector GetAutoHarvestNavigationGoal(', $harvestNavStart)
$harvestDistanceStart = $harvestSource.IndexOf('        double GetAutoHarvestRemainingPathDistance(')
$harvestDistanceEnd = $harvestSource.IndexOf('        bool AutoHarvestHasSafeGround(', $harvestDistanceStart)
$harvestSteeringStart = $harvestSource.IndexOf('        struct AutoHarvestSafeDirectionResult')
$harvestSteeringEnd = $harvestSource.IndexOf('#include "HarvestEfficiency.inl"', $harvestSteeringStart)
if ($harvestNavStart -lt 0 -or $harvestNavEnd -le $harvestNavStart -or $harvestDistanceStart -lt 0 -or $harvestDistanceEnd -le $harvestDistanceStart) { throw 'Production harvest function boundaries changed.' }
Set-Content -LiteralPath (Join-Path $harvestRuntimeOutput 'navigation-runtime.inc') -Value $harvestSource.Substring($harvestNavStart, $harvestNavEnd - $harvestNavStart) -Encoding utf8
Set-Content -LiteralPath (Join-Path $harvestRuntimeOutput 'path-distance-runtime.inc') -Value $harvestSource.Substring($harvestDistanceStart, $harvestDistanceEnd - $harvestDistanceStart) -Encoding utf8
if ($harvestSteeringStart -lt 0 -or $harvestSteeringEnd -le $harvestSteeringStart) { throw 'Production steering function boundaries changed.' }
Set-Content -LiteralPath (Join-Path $harvestRuntimeOutput 'steering-runtime.inc') -Value $harvestSource.Substring($harvestSteeringStart, $harvestSteeringEnd - $harvestSteeringStart) -Encoding utf8
$harvestReachStart = $harvestSource.IndexOf('        const float RangeMultiplier = PhoenixInterface::FiniteClamp(')
$harvestReachEnd = $harvestSource.IndexOf('        if (TekStriderMode && !IsTekStrider(Dino))', $harvestReachStart)
$harvestReachResetStart = $harvestSource.IndexOf('        void ResetAutoHarvestMeleeReach()')
$harvestReachResetEnd = $harvestSource.IndexOf('        void ClearAutoHarvestViewLock()', $harvestReachResetStart)
if ($harvestReachStart -lt 0 -or $harvestReachEnd -le $harvestReachStart -or $harvestReachResetStart -lt 0 -or $harvestReachResetEnd -le $harvestReachResetStart) { throw 'Production melee cache boundaries changed.' }
Set-Content -LiteralPath (Join-Path $harvestRuntimeOutput 'reach-reset-runtime.inc') -Value $harvestSource.Substring($harvestReachResetStart, $harvestReachResetEnd - $harvestReachResetStart) -Encoding utf8
Set-Content -LiteralPath (Join-Path $harvestRuntimeOutput 'reach-cache-runtime.inc') -Value $harvestSource.Substring($harvestReachStart, $harvestReachEnd - $harvestReachStart) -Encoding utf8
$testRunner = Join-Path $testOutput 'run-harvest-efficiency-tests.cmd'
$testLines = @(
    '@echo off',
    ('call "' + $testDev + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W3 /utf-8 tests\harvest_efficiency.cpp /Fe:release\tests\harvest_efficiency.exe /Fo:release\tests\',
    'if errorlevel 1 exit /b 1',
    'release\tests\harvest_efficiency.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W3 /utf-8 tests\harvest_navigation.cpp /Fe:release\tests\harvest_navigation.exe /Fo:release\tests\',
    'if errorlevel 1 exit /b 1',
    'release\tests\harvest_navigation.exe'
)
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & cmd.exe /c $testRunner
    if ($LASTEXITCODE -ne 0) { throw 'Harvest efficiency regression checks failed.' }
}
finally { Pop-Location }
