$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release/water-performance/tests'
$testLog = Join-Path $testRoot 'release/water-performance/runtime-tests.log'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-runtime.cmd'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'), 'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W4 /utf-8 tests\underwater_weapons.cpp /Fe:release\water-performance\tests\underwater_weapons.exe /Fo:release\water-performance\tests\',
    'if errorlevel 1 exit /b 1', 'release\water-performance\tests\underwater_weapons.exe')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & cmd.exe /c $testRunner 2>&1 | Tee-Object -FilePath $testLog
    if ($LASTEXITCODE -ne 0) { throw 'Underwater weapon runtime checks failed.' }
}
finally { Pop-Location }
