[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Install','ResumeInstall','Update','Rollback','Start','Stop','Status','Uninstall')][string]$Action,
    [string]$Root = "$env:ProgramData\JAMboreeVision",
    [string]$SourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$PythonExe,
    [string]$ReleaseDirectory,
    [string]$FFmpegDirectory,
    [string]$WinSWExe,
    [string]$WinSWSha256,
    [string]$AllowedSubnet,
    [ValidateRange(1,65535)][int]$Port = 5004,
    [string]$ImportDataDirectory
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 can turn harmless native stderr into terminating errors.
function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Exe @Arguments; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $saved }
    if ($code -ne 0) { throw "$Exe exited with code $code" }
}
function Wait-Healthy {
    $deadline = (Get-Date).AddSeconds(60)
    do {
        if ((Get-Service JAMboreeVision).Status -ne 'Running') {
            Start-Sleep -Seconds 1
            continue
        }
        try {
            $s = Invoke-RestMethod "http://127.0.0.1:$Port/api/vision/state" -TimeoutSec 3
            if ($null -ne $s.stbs -and $null -ne $s.remotes) { return }
        } catch { }
        Start-Sleep -Seconds 1
    } while ((Get-Date) -lt $deadline)
    throw 'Service HTTP check failed; inspect logs'
}
function Start-Wrapper {
    Invoke-Native $wrapper @('start')
    Wait-Healthy
}
function Stop-Wrapper {
    if ((Get-Service JAMboreeVision).Status -ne 'Stopped') { Invoke-Native $wrapper @('stop') }
}
function Grant-Tree([string]$Directory, [string]$ServiceRights) {
    # Protect this boundary directory, then let descendants inherit its ACEs.
    # Applying /inheritance:r with directory-only flags recursively can leave
    # file DACLs empty (observed on the Windows service host).
    Invoke-Native "$env:SystemRoot\System32\icacls.exe" @($Directory,'/inheritance:r','/grant:r',
        '*S-1-5-18:(OI)(CI)F','*S-1-5-32-544:(OI)(CI)F',"*S-1-5-19:(OI)(CI)$ServiceRights",'/Q')
    foreach ($child in Get-ChildItem -LiteralPath $Directory -Force) {
        Invoke-Native "$env:SystemRoot\System32\icacls.exe" @($child.FullName,'/inheritance:e','/T','/Q')
    }
}
$Root = [IO.Path]::GetFullPath($Root)
$wrapper = Join-Path $Root 'service\JAMboreeVision.exe'
$config = Join-Path $Root 'service\JAMboreeVision.xml'
$settingsFile = Join-Path $Root 'service\installation.json'
if ($Action -eq 'Status') {
    Get-Service JAMboreeVision
    if (Test-Path $settingsFile) { Get-Content $settingsFile }
    return
}
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { throw 'Run PowerShell as Administrator for service changes' }
if ($Action -notin @('Install','ResumeInstall')) {
    if (-not (Test-Path $settingsFile)) { throw "No installation manifest at $settingsFile" }
    $installation = Get-Content $settingsFile -Raw | ConvertFrom-Json
    $Port = [int]$installation.port
    $FFmpegDirectory = $installation.ffmpeg
    if (-not $PythonExe) { $PythonExe = $installation.python }
}
switch ($Action) {
    'Status' { return }
    'Start' { Start-Wrapper; return }
    'Stop' { Stop-Wrapper; return }
    'Uninstall' {
        Stop-Wrapper
        Invoke-Native $wrapper @('uninstall')
        Get-NetFirewallRule -Name 'JAMboreeVision-LAN' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
        Write-Host "Service removed. Files and configuration retained at $Root"
        return
    }
    'Rollback' {
        $previous = "$config.previous"
        if (-not (Test-Path $previous)) { throw 'No previous release configuration is available' }
        Stop-Wrapper
        Copy-Item $config "$config.failed" -Force
        Copy-Item $previous $config -Force
        Start-Wrapper
        Write-Host 'Previous application release started; shared data was not rolled back'
        return
    }
}
if (-not $PythonExe -or -not (Test-Path -LiteralPath $PythonExe -PathType Leaf)) { throw 'Supply -PythonExe with a machine-wide Python 3.11/3.12 executable' }
$PythonExe = (Resolve-Path -LiteralPath $PythonExe).Path
if ($PythonExe -like "$env:SystemDrive\Users\*") { throw 'Service requires machine-wide Python; a per-user installation is unavailable after logout' }
Invoke-Native $PythonExe @('-c','import sys; assert (3,10) <= sys.version_info[:2] < (3,13), "Use machine-wide Python 3.10-3.12"')
foreach ($file in 'vision\__main__.py','requirements.txt') {
    if (-not (Test-Path -LiteralPath (Join-Path $SourceDirectory $file))) { throw "Source directory is missing $file" }
}
foreach ($file in 'ffmpeg.exe','ffprobe.exe') {
    if (-not $FFmpegDirectory -or -not (Test-Path -LiteralPath (Join-Path $FFmpegDirectory $file))) { throw "Supply -FFmpegDirectory containing $file" }
}
if ($Action -in @('Install','ResumeInstall')) {
    if (Get-Service JAMboreeVision -ErrorAction SilentlyContinue) { throw 'Service already exists; use Update' }
    if ($Action -eq 'Install' -and (Test-Path $Root)) { throw "Choose an empty installation root; $Root already exists" }
    if ($Action -eq 'ResumeInstall') {
        if (-not $ReleaseDirectory) { throw 'ResumeInstall requires -ReleaseDirectory for the staged release' }
        $ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
        $releasesRoot = [IO.Path]::GetFullPath((Join-Path $Root 'releases')).TrimEnd('\') + '\'
        if (-not $ReleaseDirectory.StartsWith($releasesRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Staged release must be under this installation releases directory' }
        if (-not (Test-Path -LiteralPath "$ReleaseDirectory\.venv\Scripts\python.exe")) { throw 'Staged virtual environment not found' }
        if (Test-Path $settingsFile) { throw 'Installation manifest exists; use normal service operations' }
    }
    if (-not $AllowedSubnet -or $AllowedSubnet -in @('Any','*','0.0.0.0/0')) { throw 'Supply the department client IPv4 subnet, e.g. 192.168.19.0/24' }
    $parts = $AllowedSubnet.Split('/')
    $address = $null
    if ($parts.Count -ne 2 -or -not [Net.IPAddress]::TryParse($parts[0],[ref]$address) -or $address.AddressFamily -ne 'InterNetwork' -or $parts[1] -notmatch '^([1-9]|[12][0-9]|3[0-2])$') { throw 'AllowedSubnet must be an IPv4 CIDR with prefix 1-32' }
    if (-not (Test-Path -LiteralPath $WinSWExe -PathType Leaf) -or $WinSWSha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'Supply the official WinSW v2.12.0 x64 binary and its SHA-256' }
    if ((Get-FileHash -LiteralPath $WinSWExe -Algorithm SHA256).Hash -ne $WinSWSha256) { throw 'WinSW SHA-256 mismatch' }
    if (Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue) { throw "Port $Port is occupied; stop the interactive app first" }
    foreach ($dir in @($Root,"$Root\service","$Root\releases","$Root\data","$Root\data\temp","$Root\logs","$Root\backups")) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    Grant-Tree $Root 'RX'
    Grant-Tree "$Root\data" 'M'
    Grant-Tree "$Root\logs" 'M'
    Copy-Item -LiteralPath $WinSWExe -Destination $wrapper
    Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $Root 'service\Manage-Service.ps1')
}
if ($Action -eq 'ResumeInstall') {
    $release = $ReleaseDirectory
    Grant-Tree $release 'RX'
} else {
    $release = Join-Path $Root ('releases\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $release | Out-Null
    foreach ($item in 'vision','jamboree','requirements.txt','README.md','docs') {
        Copy-Item -LiteralPath (Join-Path $SourceDirectory $item) -Destination $release -Recurse
    }
    Get-ChildItem $release -Directory -Filter __pycache__ -Recurse | Remove-Item -Recurse -Force
    Invoke-Native $PythonExe @('-m','venv',"$release\.venv")
    Invoke-Native "$release\.venv\Scripts\python.exe" @('-m','pip','install','-r',"$release\requirements.txt")
    Grant-Tree $release 'RX'
}
$releasePython = "$release\.venv\Scripts\python.exe"
Set-Location -LiteralPath $release
Invoke-Native $releasePython @('-m','pip','check')
Invoke-Native $releasePython @('-c','import vision.app, waitress, serial, keyring, filelock')
$newConfig = "$config.next"
Invoke-Native $releasePython @('-m','vision.windows_service','xml','--app-dir',$release,'--data-dir',"$Root\data",'--ffmpeg-dir',$FFmpegDirectory,'--log-dir',"$Root\logs",'--port',"$Port",'--output',$newConfig)
if ($Action -eq 'Update') {
    # Candidate is prepared before stopping the current service.
    $transaction = "$config.transaction"
    Copy-Item $config $transaction -Force
    Stop-Wrapper
    $backup = Join-Path $Root ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    try {
        Invoke-Native $releasePython @('-m','vision.windows_service','migrate','--source',"$Root\data",'--target',$backup)
        Copy-Item $newConfig $config -Force
        Start-Wrapper
        Copy-Item $transaction "$config.previous" -Force
    } catch {
        $failure = $_
        Stop-Wrapper
        Copy-Item $transaction $config -Force
        Start-Wrapper
        throw "Update failed; previous release restarted. $failure"
    }
} else {
    if ($ImportDataDirectory) {
        Invoke-Native $releasePython @('-m','vision.windows_service','migrate','--source',$ImportDataDirectory,'--target',"$Root\data")
        Grant-Tree "$Root\data" 'M'
    }
    Copy-Item $newConfig $config -Force
    Invoke-Native $wrapper @('install')
    # Scope access to the department subnet on managed/private networks.
    New-NetFirewallRule -Name 'JAMboreeVision-LAN' -DisplayName 'JAMboreeVision department LAN' -Direction Inbound -Action Allow -Protocol TCP -LocalPort $Port -RemoteAddress $AllowedSubnet -Profile Domain,Private | Out-Null
    Start-Wrapper
}
@{port=$Port;python=$PythonExe;ffmpeg=$FFmpegDirectory;activeRelease=$release;updatedAt=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $settingsFile -Encoding UTF8
Write-Host "Service ready: http://$($env:COMPUTERNAME):$Port"
Write-Host "Configuration: $Root\data; logs: $Root\logs"
