$ErrorActionPreference='Stop'
$testRoot=Split-Path -Parent $PSScriptRoot
$testVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs=& $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$testDev=Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
$testRunner=Join-Path $testRoot 'release/ui-work/gpu-tests.cmd'
$testLines=@('@echo off',('call "'+$testDev+'" -no_logo -arch=x64'),'if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W0 /utf-8 /DNOMINMAX /O1 tests\vibrancy_gpu.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_draw.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_widgets.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_tables.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_impl_dx12.cpp /Fe:release\ui-work\vibrancy_gpu.exe /Fo:release\ui-work\gpu\ /link d3d12.lib dxgi.lib d3dcompiler.lib user32.lib','if errorlevel 1 exit /b 1','release\ui-work\vibrancy_gpu.exe')
New-Item -ItemType Directory -Path (Join-Path $testRoot 'release/ui-work/gpu') -Force | Out-Null
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try{& cmd.exe /c $testRunner;if($LASTEXITCODE-ne 0){throw 'DX12 vibrancy checks failed.'}}
finally{Pop-Location}
