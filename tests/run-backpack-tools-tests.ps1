$ErrorActionPreference='Stop'
$testRoot=Split-Path -Parent $PSScriptRoot
$testOutput=Join-Path $testRoot 'release/feature-update/tests'
$testVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs=& $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$testDev=Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner=Join-Path $testOutput 'run.cmd'
$testLines=@('@echo off',('call "'+$testDev+'" -no_logo -arch=x64'),'if errorlevel 1 exit /b 1',
'cl /nologo /std:c++20 /EHsc /W3 /utf-8 tests\backpack_tools.cpp /Fe:release\feature-update\tests\backpack_tools.exe /Fo:release\feature-update\tests\',
'if errorlevel 1 exit /b 1','release\feature-update\tests\backpack_tools.exe','if errorlevel 1 exit /b 1',
'cl /nologo /std:c++20 /EHsc /W3 /utf-8 tests\native_flight.cpp /Fe:release\feature-update\tests\native_flight.exe /Fo:release\feature-update\tests\',
'if errorlevel 1 exit /b 1','release\feature-update\tests\native_flight.exe','if errorlevel 1 exit /b 1',
'cl /nologo /O1 /W0 /D_CRT_SECURE_NO_WARNINGS src\external\LuaJIT-2.1\src\ljamalg.c src\external\LuaJIT-2.1\src\luajit.c src\external\LuaJIT-2.1\src\lj_vm.obj /Fe:release\feature-update\tests\luajit.exe /Fo:release\feature-update\tests\',
'if errorlevel 1 exit /b 1','release\feature-update\tests\luajit.exe tests\lua_tools.lua')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try { & cmd.exe /c $testRunner; if($LASTEXITCODE -ne 0){throw 'Backpack/Lua checks failed.'} }
finally { Pop-Location }
