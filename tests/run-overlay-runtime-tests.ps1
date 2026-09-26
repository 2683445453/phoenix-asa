$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release/injection-crash-2157/tests/overlay'
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-overlay-runtime.cmd'
$testLines = @('@echo off', ('call "' + $testDev + '" -no_logo -arch=x64'), 'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /bigobj /O1 /EHsc /W0 /utf-8 /DNOMINMAX /DPHOENIX_UI_TEST /I src\external\LuaJIT-2.1\src tests\overlay_runtime.cpp src\external\Minimal-D3D12-Hook-ImGui\Main\mdx12_globals.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_draw.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_widgets.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_tables.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_impl_dx12.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_impl_win32.cpp /Fe:release\injection-crash-2157\tests\overlay\overlay_runtime.exe /Fo:release\injection-crash-2157\tests\overlay\ /link d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib dwmapi.lib imm32.lib',
    'if errorlevel 1 exit /b 1', 'release\injection-crash-2157\tests\overlay\overlay_runtime.exe')
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & cmd.exe /c $testRunner 2>&1 | Tee-Object -FilePath (Join-Path $testOutput 'overlay-runtime.log')
    if ($LASTEXITCODE -ne 0) { throw 'Production overlay WARP regression checks failed.' }
}
finally { Pop-Location }
