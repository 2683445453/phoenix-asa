$ErrorActionPreference='Stop'
$testRoot=Split-Path -Parent $PSScriptRoot
$testVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs=& $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if(-not $testVs){throw 'Visual Studio C++ Build Tools are required.'}
$testDev=Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
$testDir=Join-Path $testRoot 'release/gpu-crash/backend'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
$testRunner=Join-Path $testDir 'run.cmd'
$testLines=@('@echo off',('call "'+$testDev+'" -no_logo -arch=x64'),'if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W3 /utf-8 /DNOMINMAX /O1 tests\dx12_backend_lifecycle.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_draw.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_widgets.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_tables.cpp /Fe:release\gpu-crash\backend\dx12_backend_lifecycle.exe /Fo:release\gpu-crash\backend\ /link d3d12.lib dxgi.lib d3dcompiler.lib user32.lib','if errorlevel 1 exit /b 1','release\gpu-crash\backend\dx12_backend_lifecycle.exe')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try{& cmd.exe /c $testRunner;if($LASTEXITCODE-ne 0){throw 'DX12 backend lifecycle checks failed.'}}
finally{Pop-Location}
