param([string]$VisualStudioPath, [switch]$OverlayOnly)
$ErrorActionPreference='Stop'
$testRoot=Split-Path -Parent $PSScriptRoot
$testVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$testVs=$VisualStudioPath
if (-not $testVs -and (Test-Path -LiteralPath $testVsWhere)) {
    $testVs=& $testVsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $testVs) { throw 'Supply -VisualStudioPath for the C++ Build Tools installation.' }
$testDev=Join-Path $testVs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Path (Join-Path $testRoot 'release/ui-work') -Force | Out-Null
$testRunner=Join-Path $testRoot 'release/ui-work/render-tests.cmd'
$testLines=@('@echo off',('call "'+$testDev+'" -no_logo -arch=x64'),'if errorlevel 1 exit /b 1','cl /nologo /std:c++20 /EHsc /W0 /utf-8 /DPHOENIX_UI_TEST /DIMGUI_ENABLE_TEST_ENGINE /DNOMINMAX /O1 /I src\external\LuaJIT-2.1\src tests\interface_render.cpp src\internal\Menu\DrawImGui.cpp src\internal\Menu\Aimbot_Menu.cpp src\internal\Menu\Visuals_Menu.cpp src\internal\Menu\Misc_Menu.cpp src\internal\Language\LanguageManager.cpp src\internal\Config\ConfigManager.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_draw.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_widgets.cpp src\external\Minimal-D3D12-Hook-ImGui\ImGui\imgui_tables.cpp /Fe:release\ui-work\interface_render.exe /Fo:release\ui-work\ /link user32.lib shell32.lib','if errorlevel 1 exit /b 1','release\ui-work\interface_render.exe')
if ($OverlayOnly) { $testLines[-1] += ' --overlay-only' }
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try{& cmd.exe /c $testRunner;if($LASTEXITCODE-ne 0){throw 'UI rendering checks failed.'}}
finally{Pop-Location}

