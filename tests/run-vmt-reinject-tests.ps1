$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release/reinject-harvest/tests'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-vmt-reinject.cmd'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'), 'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /O2 /EHsc /W3 /utf-8 /LD tests\vmt_reinject_plugin.cpp /Fe:release\reinject-harvest\tests\vmt_reinject_plugin.dll /Fo:release\reinject-harvest\tests\ /link /IMPLIB:release\reinject-harvest\tests\vmt_reinject_plugin.lib',
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /O2 /EHsc /W3 /utf-8 tests\vmt_reinject.cpp /Fe:release\reinject-harvest\tests\vmt_reinject.exe /Fo:release\reinject-harvest\tests\',
    'if errorlevel 1 exit /b 1', 'release\reinject-harvest\tests\vmt_reinject.exe')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try { & cmd.exe /c $testRunner; if ($LASTEXITCODE -ne 0) { throw 'VMT unload/reinjection regression checks failed.' } }
finally { Pop-Location }
