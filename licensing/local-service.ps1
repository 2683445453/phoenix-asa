[CmdletBinding()]
param(
    [ValidateSet('start','status','stop')][string]$Action = 'start',
    [ValidateRange(1024,65535)][int]$Port = 17891,
    [ValidateRange(2,600)][int]$LeaseSeconds = 600,
    [string]$Python
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')
$dataDirectory = [System.IO.Path]::GetFullPath((Join-Path (Split-Path $PSScriptRoot -Parent) '.local-license'))
$serviceScript = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'server\phoenix_license.py'))
$statePath = Join-Path $dataDirectory "service-$Port.json"
$healthUrl = "http://127.0.0.1:$Port/health"

function Get-ServiceHealth {
    try {
        # Use no proxy for loopback even when the user's system has a proxy set.
        $request = [System.Net.HttpWebRequest]::Create($healthUrl)
        $request.Proxy = $null
        $request.Timeout = 1000
        $request.ReadWriteTimeout = 1000
        $response = $request.GetResponse()
        try {
            $reader = [System.IO.StreamReader]::new($response.GetResponseStream())
            try { $health = $reader.ReadToEnd() | ConvertFrom-Json }
            finally { $reader.Dispose() }
            return $health.product -eq 'phoenix' -and $health.status -eq 'ok' -and $health.protocol -eq 1
        } finally { $response.Dispose() }
    } catch { return $false }
}

function Get-OwnedService {
    if (-not (Test-Path -LiteralPath $statePath)) { return $null }
    try {
        $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($state.port -ne $Port -or $state.script -ne $serviceScript -or $state.dataDirectory -ne $dataDirectory) { return $null }
        $process = Get-Process -Id $state.processId -ErrorAction Stop
        # Creation time and executable identity guard against reuse of an old PID.
        if ($process.StartTime.ToUniversalTime().Ticks.ToString() -ne $state.startTicks -or $process.Path -ne $state.executable) { return $null }
        $details = Get-CimInstance Win32_Process -Filter "ProcessId = $($process.Id)"
        if (-not $details.CommandLine.Contains(('"' + $serviceScript + '"')) -or
            -not $details.CommandLine.Contains(('"' + $dataDirectory + '"'))) { return $null }
        return $process
    } catch { return $null }
}

$owned = Get-OwnedService
if ($Action -eq 'stop') {
    if ($null -eq $owned) {
        if (Get-ServiceHealth) { throw 'The responding service was not started by this helper. Stop its original terminal instead.' }
        Write-Output "phoenix license service is not running on port $Port."
        return
    }
    $owned.Kill()
    $owned.WaitForExit(5000) | Out-Null
    if (-not $owned.HasExited) { throw 'The local service did not stop.' }
    Remove-Item -LiteralPath $statePath -Force
    Write-Output "phoenix license service stopped on port $Port."
    return
}

if (Get-ServiceHealth) {
    Write-Output "phoenix license service is ready: http://127.0.0.1:$Port"
    if ($owned -and $Action -eq 'start') {
        $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($state.leaseSeconds -ne $LeaseSeconds) {
            Write-Warning "The running service uses a $($state.leaseSeconds)-second lease. Stop it before starting with a different lease length."
        }
    }
    return
}
if ($Action -eq 'status') {
    throw "phoenix license service is unavailable. Run ./licensing/manage-local.ps1 -Action start -Port $Port"
}
if ($owned) { throw 'The managed service is running but not responding. Stop it before retrying.' }
if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) {
    throw "Port $Port is already occupied by another service."
}

$pythonPath = Resolve-PhoenixPython $Python
if (-not (Test-Path -LiteralPath (Join-Path $dataDirectory 'signing-key.pem')) -or
    -not (Test-Path -LiteralPath (Join-Path $dataDirectory 'licenses.sqlite3'))) {
    throw 'Local signing key or database is missing. Run ./licensing/manage-local.ps1 -Action init first; existing state will not be replaced.'
}
$arguments = @(('"' + $serviceScript + '"'), '--data-dir', ('"' + $dataDirectory + '"'),
    'serve', '--port', $Port, '--lease-seconds', $LeaseSeconds)
$started = Start-Process -FilePath $pythonPath -ArgumentList $arguments -WindowStyle Hidden -PassThru `
    -WorkingDirectory (Split-Path $serviceScript -Parent) `
    -RedirectStandardOutput (Join-Path $dataDirectory "service-$Port.stdout.log") `
    -RedirectStandardError (Join-Path $dataDirectory "service-$Port.stderr.log")
$state = [ordered]@{
    processId = $started.Id
    startTicks = $started.StartTime.ToUniversalTime().Ticks.ToString()
    executable = $started.Path
    script = $serviceScript
    dataDirectory = $dataDirectory
    port = $Port
    leaseSeconds = $LeaseSeconds
}
[System.IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json), [System.Text.UTF8Encoding]::new($false))
for ($attempt = 0; $attempt -lt 30; $attempt++) {
    $started.Refresh()
    if ($started.HasExited) { throw "The local service exited. See .local-license/service-$Port.stderr.log" }
    if (Get-ServiceHealth) {
        Write-Output "phoenix license service started in the background: http://127.0.0.1:$Port"
        Write-Output "Stop: ./licensing/manage-local.ps1 -Action stop -Port $Port"
        return
    }
    Start-Sleep -Milliseconds 100
}
throw "The service did not become ready. See .local-license/service-$Port.stderr.log"
