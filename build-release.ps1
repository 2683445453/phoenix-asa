[CmdletBinding()]
param(
    [switch]$Rebuild,
    [string]$MSBuildPath = $env:PHOENIX_MSBUILD,
    [ValidateSet('Development','Local')][string]$LicenseMode = 'Development',
    [ValidateRange(1024,65535)][int]$LicensePort = 17891
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

$msbuild = $MSBuildPath
if (-not $msbuild -and (Test-Path -LiteralPath $vswhere)) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild) {
    throw 'MSBuild was not found. Supply -MSBuildPath or PHOENIX_MSBUILD for a custom C++ Build Tools installation.'
}
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw "MSBuild does not exist: $msbuild" }

$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$commonArgs = @(
    "/t:$target",
    '/p:Configuration=Release',
    '/p:Platform=x64',
    "/p:PhoenixLicenseMode=$LicenseMode",
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

$localLicense = $LicenseMode -eq 'Local'
$packageName = if ($localLicense) { 'phoenix-local-license' } else { 'phoenix' }
$packageDir = Join-Path $projectRoot "dist\$packageName"
$languageDir = Join-Path $packageDir 'language'
$workshopDir = Join-Path $packageDir 'work_shop'

New-Item -ItemType Directory -Path $packageDir, $languageDir, $workshopDir, (Join-Path $packageDir 'cfg'), (Join-Path $packageDir 'lua'), (Join-Path $packageDir 'logs') -Force | Out-Null
$loaderOutput = if ($localLicense) { 'release\license-local\ASA_Loader\phoenix.exe' } else { 'ASA_Loader\x64\Release\phoenix.exe' }
$dllOutput = if ($localLicense) { 'release\license-local\begeerte_ark_survival_ascended\phoenix.dll' } else { 'release\phoenix.dll' }
Copy-Item -LiteralPath (Join-Path $projectRoot $loaderOutput) -Destination (Join-Path $packageDir 'phoenix.exe') -Force
$releaseDll = Join-Path $projectRoot $dllOutput
$packageDll = Join-Path $packageDir 'phoenix.dll'
$pendingDll = Join-Path $packageDir 'phoenix.dll.pending'
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
Copy-Item -Path (Join-Path $projectRoot 'lua\*.lua') -Destination (Join-Path $packageDir 'lua') -Force
# Windows PowerShell 5.1 reads UTF-8 scripts without a BOM through the active
# ANSI code page. Keep package-document path literals ASCII-safe.
function ConvertFrom-Utf8Base64([string]$Value) {
    [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String($Value))
}
$usageSourceName = ConvertFrom-Utf8Base64 'cGhvZW5peC3kvb/nlKjor7TmmI4ubWQ='
$featureSourceName = ConvertFrom-Utf8Base64 'cGhvZW5peC0yMDI2MDkxNy3lip/og73ooaXlhYUubWQ='
$localUsageSourceName = ConvertFrom-Utf8Base64 'cGhvZW5peC3mnKzlnLDmjojmnYPpqozor4EubWQ='
$usagePackageName = ConvertFrom-Utf8Base64 '5L2/55So6K+05piOLm1k'
$featurePackageName = ConvertFrom-Utf8Base64 '5Yqf6IO96KGl5YWF5LiO5a+554WnLm1k'
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\$usageSourceName") -Destination (Join-Path $packageDir $usagePackageName) -Force
Copy-Item -LiteralPath (Join-Path $projectRoot "docs\$featureSourceName") -Destination (Join-Path $packageDir $featurePackageName) -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $packageDir -Force
if ($localLicense) {
    $serviceConfig = Join-Path $packageDir 'license-server.json'
    # Preserve an explicitly configured service address on incremental builds.
    if (-not (Test-Path -LiteralPath $serviceConfig)) {
        [System.IO.File]::WriteAllText($serviceConfig, ('{"url":"http://127.0.0.1:' + $LicensePort + '"}'), [System.Text.UTF8Encoding]::new($false))
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot "docs\$localUsageSourceName") -Destination (Join-Path $packageDir $usagePackageName) -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'licensing\client\vendor\LICENSE.nlohmann-json') -Destination $packageDir -Force
}

& (Join-Path $packageDir 'phoenix.exe') --check
if ($LASTEXITCODE -ne 0) {
    throw "Packaged loader check failed with exit code $LASTEXITCODE."
}

$hashTargets = @((Join-Path $packageDir 'phoenix.exe'))
$hashTargets += if ($dllWasStaged) { $pendingDll } else { $packageDll }
Get-FileHash -Algorithm SHA256 -LiteralPath $hashTargets | Format-Table -AutoSize

$archivePath = Join-Path $projectRoot "dist\$packageName.zip"
$archiveItems = @(
    (Join-Path $packageDir 'phoenix.exe'),
    # Always package the fresh build as phoenix.dll, even when the local copy is locked.
    $releaseDll,
    $languageDir,
    $workshopDir,
    (Join-Path $projectRoot 'lua'),
    (Join-Path $packageDir 'LICENSE')
)
if ($localLicense) {
    $archiveItems += (Join-Path $packageDir 'license-server.json'), (Join-Path $packageDir 'LICENSE.nlohmann-json')
}
$usageGuide = Join-Path $packageDir $usagePackageName
if (Test-Path -LiteralPath $usageGuide) {
    $archiveItems += $usageGuide
}
$archiveItems += (Join-Path $packageDir $featurePackageName)
Compress-Archive -LiteralPath $archiveItems -DestinationPath $archivePath -CompressionLevel Optimal -Force

Write-Host "Release package: $packageDir"
Write-Host "Release archive: $archivePath"
if ($localLicense) {
    Write-Host 'Local license validation build: activation and expiry enforced; no protection shell or code signing.'
} else {
    Write-Host 'Development build: no activation, expiration enforcement, or protection shell.'
}
