[CmdletBinding()]
param(
    [switch]$Rebuild,
    [string]$MSBuildPath = $env:PHOENIX_MSBUILD,
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if (-not $Version) { $Version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim() }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $MSBuildPath
if (-not $msbuild -and (Test-Path -LiteralPath $vswhere)) {
    $msbuild = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw 'MSBuild was not found. Install the v145 C++ tools or supply -MSBuildPath / PHOENIX_MSBUILD.'
}

# Native compiler/linker output.
$target = if ($Rebuild) { 'Rebuild' } else { 'Build' }
$commonArgs = @("/t:$target", '/p:Configuration=Release', '/p:Platform=x64',
    '/m:2', '/nologo', '/v:minimal')
foreach ($solution in @('src\begeerte_ark_survival_ascended.sln', 'ASA_Loader\ASA_Loader.sln')) {
    & $msbuild (Join-Path $projectRoot $solution) @commonArgs
    if ($LASTEXITCODE -ne 0) { throw "$solution build failed with exit code $LASTEXITCODE." }
}

$packageName = 'phoenix'
$distRoot = Join-Path $projectRoot 'dist'
$packageDir = Join-Path $distRoot $packageName
$stageDir = Join-Path $distRoot ('.staging-' + [guid]::NewGuid().ToString('N'))
$archivePath = Join-Path $distRoot "$packageName.zip"
$loaderOutput = 'ASA_Loader\x64\Release\phoenix.exe'
$dllOutput = 'release\phoenix.dll'

$utf8 = [Text.UTF8Encoding]::new($false)

New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $projectRoot $loaderOutput) -Destination (Join-Path $stageDir 'phoenix.exe')
    Copy-Item -LiteralPath (Join-Path $projectRoot $dllOutput) -Destination (Join-Path $stageDir 'phoenix.dll')
    foreach ($folder in @('language', 'lua', 'work_shop')) {
        $extension = if ($folder -eq 'language') { '*.ini' } else { '*.lua' }
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot $folder) -Filter $extension -File -Recurse) {
            $destination = Join-Path $stageDir $file.FullName.Substring($projectRoot.Length + 1)
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination
        }
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $stageDir
    Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $stageDir
    [IO.File]::WriteAllText((Join-Path $stageDir 'VERSION'), "$Version`n", $utf8)
    $notices = @{
        'LuaJIT.txt' = 'src\external\LuaJIT-2.1\COPYRIGHT'
        'ImGui.txt' = 'src\external\Minimal-D3D12-Hook-ImGui\ImGui\LICENSE.txt'
        'D3D12-Hook.txt' = 'src\external\Minimal-D3D12-Hook-ImGui\LICENSE'
        'MinHook.txt' = 'src\external\Minimal-D3D12-Hook-ImGui\MinHook\LICENSE.txt'
        'ezVMT.txt' = 'src\external\ezVMT\LICENSE'
        'sol2.txt' = 'src\external\sol2\License.txt'
        'HarmonyOS-Sans.txt' = 'src\external\Minimal-D3D12-Hook-ImGui\Font\LICENSE.txt'
        'UnrealContainers.txt' = 'src\external\CppSDK\LICENSE.UnrealContainers'
    }
    $noticeDir = Join-Path $stageDir 'licenses'
    New-Item -ItemType Directory -Path $noticeDir | Out-Null
    foreach ($entry in $notices.GetEnumerator()) {
        Copy-Item -LiteralPath (Join-Path $projectRoot $entry.Value) -Destination (Join-Path $noticeDir $entry.Key)
    }
    & (Join-Path $stageDir 'phoenix.exe') --check
    if ($LASTEXITCODE -ne 0) { throw "Packaged loader check failed with exit code $LASTEXITCODE." }
    # --check creates runtime logs. Archive only the explicit distribution files.
    $stagedLogs = Join-Path $stageDir 'logs'
    if (Test-Path -LiteralPath $stagedLogs) {
        if (-not ([IO.Path]::GetFullPath($stagedLogs)).StartsWith($stageDir + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid staged log path.' }
        Remove-Item -LiteralPath $stagedLogs -Recurse -Force
    }
    $sourceCommit = $null
    $sourceDirty = $null
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $sourceCommit = & git -C $projectRoot rev-parse HEAD
        if ($LASTEXITCODE -ne 0) { $sourceCommit = $null }
        else { $sourceDirty = [bool](& git -C $projectRoot status --porcelain) }
    }
    $metadata = [ordered]@{
        product = 'phoenix'; version = $Version; architecture = 'x64'
        source_commit = $sourceCommit; source_dirty = $sourceDirty
    }
    [IO.File]::WriteAllText((Join-Path $stageDir 'release.json'), ($metadata | ConvertTo-Json), $utf8)
    $hashLines = @(Get-ChildItem -LiteralPath $stageDir -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($stageDir.Length + 1).Replace('\', '/')
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $relative
    })
    [IO.File]::WriteAllLines((Join-Path $stageDir 'SHA256SUMS.txt'), $hashLines, $utf8)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
    [IO.Compression.ZipFile]::CreateFromDirectory($stageDir, $archivePath, [IO.Compression.CompressionLevel]::Optimal, $false)

    # Refresh program files while retaining personal configuration and logs.
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $stageDir -File -Recurse) {
        $destination = Join-Path $packageDir $file.FullName.Substring($stageDir.Length + 1)
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        if ($file.Name -eq 'phoenix.dll') {
            try {
                Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
                if (Test-Path -LiteralPath "$destination.pending") { Remove-Item -LiteralPath "$destination.pending" -Force }
            } catch [IO.IOException] {
                Copy-Item -LiteralPath $file.FullName -Destination "$destination.pending" -Force
                Write-Warning 'DLL in use: staged as phoenix.dll.pending. The ZIP contains the fresh phoenix.dll.'
            }
        } else { Copy-Item -LiteralPath $file.FullName -Destination $destination -Force }
    }
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(("$archivePath.sha256"), "$archiveHash  $packageName.zip`n", $utf8)
    Write-Host "Native x64 release: $archivePath"
    Write-Host "SHA256: $archiveHash"
} finally {
    $resolvedStage = [IO.Path]::GetFullPath($stageDir)
    if (-not $resolvedStage.StartsWith(([IO.Path]::GetFullPath($distRoot) + '\.staging-'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid staging cleanup path.' }
    if (Test-Path -LiteralPath $resolvedStage) { Remove-Item -LiteralPath $resolvedStage -Recurse -Force }
}
