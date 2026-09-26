$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release/injection-crash-2157/tests'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-swap-chain-queue-tracker.cmd'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'), 'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /O2 /EHsc /W4 /WX /utf-8 /DNOMINMAX tests\swap_chain_queue_tracker.cpp /Fe:release\injection-crash-2157\tests\swap_chain_queue_tracker.exe /Fo:release\injection-crash-2157\tests\ /link d3d12.lib dxgi.lib user32.lib',
    'if errorlevel 1 exit /b 1', 'release\injection-crash-2157\tests\swap_chain_queue_tracker.exe')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try { & cmd.exe /c $testRunner; if ($LASTEXITCODE -ne 0) { throw 'Swap-chain queue tracker regression checks failed.' } }
finally { Pop-Location }
