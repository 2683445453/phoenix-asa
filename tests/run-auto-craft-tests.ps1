$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release\tests'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-auto-craft-tests.cmd'
$testLines = @(
    '@echo off',
    ('call "' + $testDev + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W4 /WX tests\auto_craft_scheduler.cpp /Fe:release\tests\auto_craft_scheduler.exe /Fo:release\tests\auto_craft_scheduler.obj',
    'if errorlevel 1 exit /b 1',
    'release\tests\auto_craft_scheduler.exe'
)
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & cmd.exe /c $testRunner
    if ($LASTEXITCODE -ne 0) { throw 'Auto-craft regression checks failed.' }
}
finally { Pop-Location }
