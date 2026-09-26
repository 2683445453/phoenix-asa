$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release\native-features'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-native-camera-tests.cmd'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W4 /utf-8 tests\gcm_local_flight.cpp /Fe:release\native-features\native_camera.exe /Fo:release\native-features\',
    'if errorlevel 1 exit /b 1', 'release\native-features\native_camera.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W4 /utf-8 tests\tek_heat_rules.cpp /Fe:release\native-features\tek_heat_rules.exe /Fo:release\native-features\',
    'if errorlevel 1 exit /b 1', 'release\native-features\tek_heat_rules.exe',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W4 /utf-8 tests\placement_experiment.cpp /Fe:release\native-features\placement_experiment.exe /Fo:release\native-features\',
    'if errorlevel 1 exit /b 1', 'release\native-features\placement_experiment.exe')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try { & cmd.exe /c $testRunner; if ($LASTEXITCODE -ne 0) { throw 'Native camera checks failed.' } }
finally { Pop-Location }
