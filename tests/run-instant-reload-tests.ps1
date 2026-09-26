$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$testOutput = Join-Path $testRoot 'release\tests'
# 本机 C++ 生成工具位于 D: 的 Build Tools（C: 的 VS 安装未包含 C++ 工作负载），
# 因此优先使用 vswhere，缺失时回落到已知的 Build Tools 安装。
$testVs = $null
$testVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $testVsWhere) {
    $testVs = & $testVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
}
if (-not $testVs) {
    foreach ($candidate in @(
        'D:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools',
        'D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools')) {
        if (Test-Path (Join-Path $candidate 'Common7\Tools\VsDevCmd.bat')) { $testVs = $candidate; break }
    }
}
if (-not $testVs) { throw 'Visual Studio C++ Build Tools are required.' }
$testDev = Join-Path $testVs 'Common7\Tools\VsDevCmd.bat'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$testRunner = Join-Path $testOutput 'run-instant-reload-tests.cmd'
$testLines = @(
    '@echo off',
    ('call "' + $testDev + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W3 /utf-8 tests\instant_reload.cpp src\internal\Config\ConfigManager.cpp /Fe:release\tests\instant_reload.exe /Fo:release\tests\',
    'if errorlevel 1 exit /b 1',
    'release\tests\instant_reload.exe'
)
Set-Content -LiteralPath $testRunner -Value $testLines -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & cmd.exe /c $testRunner
    if ($LASTEXITCODE -ne 0) { throw 'Instant reload / bow charge rule regression checks failed.' }
}
finally { Pop-Location }
