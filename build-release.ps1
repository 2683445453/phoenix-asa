[CmdletBinding()]
param(
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'vswhere.exe was not found. Install Visual Studio Build Tools with Desktop development with C++.'
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    throw 'MSBuild was not found.'
}

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$commonArgs = @(
    "/t:$target",
    '/p:Configuration=Release',
    '/p:Platform=x64',
    '/m:2',
    '/nologo',
    '/v:minimal'
)

& $msbuild (Join-Path $projectRoot 'src\begeerte_ark_survival_ascended.sln') @commonArgs
if ($LASTEXITCODE -ne 0) {
    throw "DLL build failed with exit code $LASTEXITCODE."
}

& $msbuild (Join-Path $projectRoot 'ASA_Loader\ASA_Loader.sln') @commonArgs
if ($LASTEXITCODE -ne 0) {
    throw "Loader build failed with exit code $LASTEXITCODE."
}

$packageDir = Join-Path $projectRoot 'dist\38s-ASA'
$languageDir = Join-Path $packageDir 'language'
$workshopDir = Join-Path $packageDir 'work_shop'

New-Item -ItemType Directory -Path $packageDir, $languageDir, $workshopDir, (Join-Path $packageDir 'cfg'), (Join-Path $packageDir 'lua'), (Join-Path $packageDir 'logs') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'ASA_Loader\x64\Release\ASA_Loader.exe') -Destination (Join-Path $packageDir 'ASA_Loader.exe') -Force
$releaseDll = Join-Path $projectRoot 'release\begeerte_ark_survival_ascended.dll'
$packageDll = Join-Path $packageDir 'begeerte_ark_survival_ascended.dll'
$pendingDll = Join-Path $packageDir 'begeerte_ark_survival_ascended.dll.pending'
$dllWasStaged = $false
try {
    Copy-Item -LiteralPath $releaseDll -Destination $packageDll -Force
    if (Test-Path -LiteralPath $pendingDll) {
        Remove-Item -LiteralPath $pendingDll -Force
    }
}
catch [System.IO.IOException] {
    Copy-Item -LiteralPath $releaseDll -Destination $pendingDll -Force
    $dllWasStaged = $true
    Write-Warning 'The packaged DLL is in use. The new DLL was saved as a pending update and will be applied by the loader after the game fully exits.'
}
Copy-Item -Path (Join-Path $projectRoot 'language\*') -Destination $languageDir -Recurse -Force
Copy-Item -Path (Join-Path $projectRoot 'work_shop\*') -Destination $workshopDir -Recurse -Force

& (Join-Path $packageDir 'ASA_Loader.exe') --check
if ($LASTEXITCODE -ne 0) {
    throw "Packaged loader check failed with exit code $LASTEXITCODE."
}

$hashTargets = @((Join-Path $packageDir 'ASA_Loader.exe'))
$hashTargets += if ($dllWasStaged) { $pendingDll } else { $packageDll }
Get-FileHash -Algorithm SHA256 -LiteralPath $hashTargets | Format-Table -AutoSize

$archivePath = Join-Path $projectRoot 'dist\38s-ASA.zip'
$archiveItems = @(
    (Join-Path $packageDir 'ASA_Loader.exe'),
    $(if ($dllWasStaged) { $pendingDll } else { $packageDll }),
    $languageDir,
    $workshopDir
)
$usageGuide = Join-Path $packageDir '使用说明.md'
if (Test-Path -LiteralPath $usageGuide) {
    $archiveItems += $usageGuide
}
Compress-Archive -LiteralPath $archiveItems -DestinationPath $archivePath -CompressionLevel Optimal -Force

Write-Host "Release package: $packageDir"
Write-Host "Release archive: $archivePath"
