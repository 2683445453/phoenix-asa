[CmdletBinding()]
param(
    [ValidateSet('init','start','status','stop','serve','issue','list','extend','revoke','restore','unbind')]
    [string]$Action = 'list',
    [string]$LicenseId,
    [ValidateRange(1,316224000)][int]$Seconds = 86400,
    [ValidateRange(1024,65535)][int]$Port = 17891,
    [ValidateRange(2,600)][int]$LeaseSeconds = 600,
    [string]$Python
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
if ($Action -in 'start','status','stop') {
    & (Join-Path $PSScriptRoot 'local-service.ps1') -Action $Action -Port $Port -LeaseSeconds $LeaseSeconds -Python $Python
    return
}
$pythonPath = Resolve-PhoenixPython $Python
$dataDirectory = Join-Path (Split-Path $PSScriptRoot -Parent) '.local-license'
$serviceScript = Join-Path $PSScriptRoot 'server\phoenix_license.py'
if ($Action -eq 'init') { Protect-PhoenixLocalData $dataDirectory }
$serviceArguments = @($serviceScript, '--data-dir', $dataDirectory, $Action)
switch ($Action) {
    'issue' { $serviceArguments += @('--seconds', $Seconds) }
    'serve' { $serviceArguments += @('--port', $Port, '--lease-seconds', $LeaseSeconds) }
    { $_ -in 'extend','revoke','restore','unbind' } {
        if ($LicenseId -notmatch '^[0-9a-f]{32}$') { throw 'A license id from the list command is required.' }
        $serviceArguments += $LicenseId
        if ($Action -eq 'extend') { $serviceArguments += @('--seconds', $Seconds) }
    }
}
& $pythonPath @serviceArguments
if ($LASTEXITCODE -ne 0) { throw 'Local license operation failed.' }
