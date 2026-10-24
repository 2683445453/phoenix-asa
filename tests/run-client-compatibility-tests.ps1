[CmdletBinding()]
param([string]$PythonPath = 'python')

$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testDir = Join-Path $testRoot 'release/ci/build'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
$testRunner = Join-Path $testDir 'client-contract.cmd'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W0 /utf-8 /DNOMINMAX /Zs /I src release\ci\build\client-contract.cpp')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & $PythonPath tests/check_client_compatibility.py --emit-assertions release/ci/build/client-contract.cpp
    if ($LASTEXITCODE -ne 0) { throw 'Native client baseline check failed.' }
    & cmd.exe /c $testRunner
    if ($LASTEXITCODE -ne 0) { throw 'Compiled SDK layouts differ from the recorded native client.' }
    Write-Host 'Native client contract passed against the compiled SDK definitions.'
} finally { Pop-Location }
