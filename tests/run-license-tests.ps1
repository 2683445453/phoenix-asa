[CmdletBinding()]
param([string]$Python)
$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $testRoot 'licensing\common.ps1')
$pythonPath = Resolve-PhoenixPython $Python
& (Join-Path $testRoot 'licensing\manage-local.ps1') -Action init -Python $pythonPath
$testOutput = Join-Path $testRoot 'release\tests\licensing'
New-Item -ItemType Directory -Path $testOutput -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$developerCommand = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
$runner = Join-Path $testOutput 'compile.cmd'
$commands = @(
    '@echo off',
    ('call "' + $developerCommand + '" -no_logo -arch=x64'),
    'if errorlevel 1 exit /b 1',
    'cl /nologo /std:c++20 /EHsc /W3 /utf-8 /DPHOENIX_LICENSE_ENABLED=1 /DPHOENIX_LICENSE_LOCAL=1 /DNOMINMAX /Ilicensing\client /I.local-license tests\license_probe.cpp licensing\client\LicenseClient.cpp /Fe:release\tests\licensing\license_probe.exe /Fo:release\tests\licensing\',
    'exit /b %errorlevel%'
)
Set-Content -LiteralPath $runner -Value $commands -Encoding ascii
Push-Location -LiteralPath $testRoot
try {
    & cmd.exe /c $runner
    if ($LASTEXITCODE -ne 0) { throw 'Native licensing test compilation failed.' }
    $licenseTests = @('tests\test_licensing.py', '--probe', (Join-Path $testOutput 'license_probe.exe'), '--key-dir', (Join-Path $testRoot '.local-license'))
    $fullDll = Join-Path $testRoot 'dist\phoenix-local-license\phoenix.dll'
    if (Test-Path -LiteralPath $fullDll) { $licenseTests += @('--dll', $fullDll) }
    $fullLoader = Join-Path $testRoot 'dist\phoenix-local-license\phoenix.exe'
    if (Test-Path -LiteralPath $fullLoader) { $licenseTests += @('--loader', $fullLoader) }
    & $pythonPath @licenseTests
    if ($LASTEXITCODE -ne 0) { throw 'Licensing checks failed.' }
} finally { Pop-Location }
