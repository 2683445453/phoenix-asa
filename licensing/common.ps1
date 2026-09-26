function Resolve-PhoenixPython {
    param([string]$Python)
    $candidates = @()
    if ($Python) { $candidates += $Python }
    $bundled = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    if (Test-Path -LiteralPath $bundled) { $candidates += $bundled }
    $installed = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($installed) { $candidates += $installed.Source }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        $version = & $candidate -c 'import cryptography; print(cryptography.__version__)' 2>$null
        if ($LASTEXITCODE -eq 0 -and $version) { return $candidate }
    }
    throw 'Python 3.12+ with cryptography is required. Install licensing/server/requirements.txt and pass -Python with its executable path.'
}

function Protect-PhoenixLocalData {
    param([string]$Path)
    # This directory contains local test signing keys and the license database.
    # Restrict it before initialization creates any private material.
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = [System.Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $inheritance = [System.Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [System.Security.AccessControl.PropagationFlags]::None
    foreach ($principal in @($identity, [System.Security.Principal.SecurityIdentifier]::new('S-1-5-18'))) {
        $rule = [System.Security.AccessControl.FileSystemAccessRule]::new($principal, 'FullControl', $inheritance, $propagation, 'Allow')
        $acl.SetAccessRule($rule)
    }
    $directoryInfo = [System.IO.DirectoryInfo]::new($Path)
    if ($PSVersionTable.PSEdition -eq 'Desktop') {
        $directoryInfo.SetAccessControl($acl)
    } else {
        [System.IO.FileSystemAclExtensions]::SetAccessControl($directoryInfo, $acl)
    }
}
