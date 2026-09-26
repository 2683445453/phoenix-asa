[CmdletBinding()]
param([switch]$Rebuild, [string]$Python, [ValidateRange(1024,65535)][int]$Port = 17891)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'licensing\manage-local.ps1') -Action init -Python $Python
& (Join-Path $PSScriptRoot 'build-release.ps1') -Rebuild:$Rebuild -LicenseMode Local -LicensePort $Port
