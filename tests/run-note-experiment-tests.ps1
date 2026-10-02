$ErrorActionPreference = 'Stop'
$noteTestRoot = Split-Path -Parent $PSScriptRoot
$noteTestOutput = Join-Path $noteTestRoot 'release\note-experiment-tests'
$noteTestVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$noteTestVs = & $noteTestVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $noteTestVs) { throw 'Visual Studio C++ Build Tools are required.' }
$noteTestDev = Join-Path $noteTestVs 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $noteTestOutput -Force | Out-Null
$noteTestRunner = Join-Path $noteTestOutput 'run-tests.cmd'
$noteTestLines = @('@echo off', ('call "' + $noteTestDev + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W4 /utf-8 /DNOMINMAX tests\note_experiment.cpp /Fe:release\note-experiment-tests\note_experiment.exe /Fo:release\note-experiment-tests\',
    'if errorlevel 1 exit /b 1', 'release\note-experiment-tests\note_experiment.exe')
Set-Content -LiteralPath $noteTestRunner -Value $noteTestLines -Encoding ascii
Push-Location -LiteralPath $noteTestRoot
try { & cmd.exe /c $noteTestRunner; if ($LASTEXITCODE -ne 0) { throw 'Note experiment regression checks failed.' } }
finally { Pop-Location }
