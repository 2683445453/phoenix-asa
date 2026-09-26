$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release/reinject-harvest/tests'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-window-procedure-routes.cmd'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'), 'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /O2 /EHsc /W4 /utf-8 tests\window_procedure_routes.cpp /Fe:release\reinject-harvest\tests\window_procedure_routes.exe /Fo:release\reinject-harvest\tests\ /link user32.lib',
    'if errorlevel 1 exit /b 1', 'release\reinject-harvest\tests\window_procedure_routes.exe')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try { & cmd.exe /c $testRunner; if ($LASTEXITCODE -ne 0) { throw 'Window procedure routing regression checks failed.' } }
finally { Pop-Location }
