param([switch]$Baseline)
$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release/water-performance'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testVariant = if ($Baseline) { 'baseline' } else { 'current' }
$testDefine = if ($Baseline) { ' /DWATER_BENCH_BASELINE' } else { '' }
$testRunner = Join-Path $testOutput ('run-' + $testVariant + '.cmd')
$testCommand = 'cl /nologo /std:c++20 /O2 /EHsc /W3 /utf-8 /DNOMINMAX' + $testDefine + ' tests\underwater_performance.cpp /Fe:release\water-performance\' + $testVariant + '.exe /Fo:release\water-performance\'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'), 'if errorlevel 1 exit /b 1', $testCommand,
    'if errorlevel 1 exit /b 1', ('release\water-performance\' + $testVariant + '.exe > release\water-performance\' + $testVariant + '.csv'))
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try { & cmd.exe /c $testRunner; if ($LASTEXITCODE -ne 0) { throw 'Underwater performance checks failed.' }; Get-Content -LiteralPath (Join-Path $testOutput ($testVariant + '.csv')) }
finally { Pop-Location }
