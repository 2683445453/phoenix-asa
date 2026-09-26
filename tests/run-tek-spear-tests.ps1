$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release\tests'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-tek-spear-tests.cmd'
$testLines = @(
    '@echo off',
    ('call "' + $testDev + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W3 /utf-8 tests\tek_spear.cpp src\internal\Config\ConfigManager.cpp /Fe:release\tests\tek_spear.exe /Fo:release\tests\',
    'if errorlevel 1 exit /b 1',
    'release\tests\tek_spear.exe'
)
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & cmd.exe /c $testRunner
    if ($LASTEXITCODE -ne 0) { throw 'Tek spear regression checks failed.' }
}
finally { Pop-Location }
