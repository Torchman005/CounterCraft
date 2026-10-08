#Requires -Version 7.4
param(
    [Parameter(Mandatory=$true)][string]$StateFile,
    [ValidatePattern('^[A-Za-z0-9_]+$')][string]$Map='de_dust2',
    [string]$SteamExecutable,
    [switch]$HostProbe,
    [switch]$DepthCapture,
    [switch]$CameraRelay,
    [ValidateRange(40,180)][int]$StartupTimeoutSeconds=120,
    [switch]$Launch
)
$ErrorActionPreference = 'Stop'
$state = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
if ($state.Mode -ne 'Installed') { throw 'Loader state must be Installed.' }
$gameDirectory = Split-Path ([IO.Path]::GetFullPath($state.Target))
$cs2 = Join-Path $gameDirectory 'cs2.exe'
$candidate = [IO.Path]::GetFullPath($state.Candidate)
if ((Get-FileHash -LiteralPath $state.Target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $state.LoaderSha256) { throw 'Installed loader changed.' }
# Steam supplies its own -steam flag. Request only the fixed offline lab arguments.
$launchArguments = @('-insecure','-countercraft-lab','-countercraft-preview','-console','+sv_lan','1','+map',$Map)
if ($HostProbe -or $DepthCapture) { $launchArguments += '-countercraft-host-probe' }
if ($DepthCapture) { $launchArguments += '-countercraft-depth-capture' }
if ($CameraRelay) {
    if (-not (Test-Path -LiteralPath (Join-Path $candidate 'camera-layout.json') -PathType Leaf)) {
        throw 'Camera relay requires a private calibration in the candidate directory.'
    }
    $launchArguments += '-countercraft-camera-relay'
}
$steamArguments = @('-applaunch','730') + $launchArguments
$steamProcesses = @(Get-CimInstance Win32_Process -Filter "Name = 'steam.exe'")
if (-not $SteamExecutable -and $steamProcesses.Count -eq 1) { $SteamExecutable = $steamProcesses[0].ExecutablePath }
$bootstrapTarget = Join-Path $gameDirectory 'ReShade.ini'
$bootstrapReady = $null -ne $state.PSObject.Properties['BootstrapTarget'] -and $state.BootstrapTarget -eq $bootstrapTarget
if (-not $Launch) {
    [pscustomobject]@{ Mode='Preview'; Executable=$cs2; Arguments=$launchArguments; LaunchRoute='Steam'
        SteamExecutable=$SteamExecutable; SteamArguments=$steamArguments; BootstrapReady=$bootstrapReady; Candidate=$candidate }
    return
}
if (Get-Process -Name cs2 -ErrorAction SilentlyContinue) { throw 'A CS2 process is already running.' }
if ($steamProcesses.Count -ne 1 -or -not $SteamExecutable -or $SteamExecutable -ne $steamProcesses[0].ExecutablePath) {
    throw 'Start the installed Steam client first; require its exact executable. Authentication must be done by the user.'
}
if (-not $bootstrapReady -or -not (Test-Path -LiteralPath $bootstrapTarget) -or
    (Get-FileHash -LiteralPath $bootstrapTarget -Algorithm SHA256).Hash.ToLowerInvariant() -ne $state.BootstrapSha256) {
    throw 'Steam launch requires the hash-matched bootstrap installed with manage-cs2-loader.ps1 -SteamLaunch.'
}
# The existing Steam session owns child environment. Use ReShade's documented INI BasePath instead.
$receipt = [pscustomobject]@{
    ProcessId=0; Executable=$cs2; Arguments=$launchArguments; Candidate=$candidate; LaunchRoute='Steam'
    SteamExecutable=$SteamExecutable; SteamArguments=$steamArguments; CreatedUtc=[DateTime]::UtcNow.ToString('o')
    ProcessIdentityVerified=$false; Failure=''
}
$receiptFile = Join-Path $candidate ('steam-launch-' + [guid]::NewGuid().ToString('N') + '.json')
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receiptFile -Encoding utf8
try {
    Start-Process -FilePath $SteamExecutable -ArgumentList $steamArguments -WindowStyle Hidden | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    do {
        $started = @(Get-CimInstance Win32_Process -Filter "Name = 'cs2.exe'")
        if ($started.Count) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($started.Count -ne 1 -or $started[0].ExecutablePath -ne $cs2 -or
        $started[0].ParentProcessId -ne $steamProcesses[0].ProcessId) {
        throw 'Steam did not start the exact CS2 executable as its child.'
    }
    $observed = $started[0]
    $receipt.ProcessId = $observed.ProcessId
    $expectedSuffix = [regex]::Escape(($launchArguments -join ' '))
    # Steam's China launch route appends its region selector after app arguments.
    # Admit that exact suffix only; do not accept arbitrary user launch options.
    $expectedLine = '^"?' + [regex]::Escape($cs2) + '"?\s+(?:-steam\s+)?' + $expectedSuffix + '(?:\s+-perfectworld)?\s*$'
    if ($observed.CommandLine -notmatch $expectedLine) {
        throw 'Steam child arguments differ from the fixed offline lab arguments; inspect Steam launch options. Do not use this session.'
    }
    $owned = Get-Process -Id $observed.ProcessId
    if ($owned.WaitForExit(5000)) { throw 'Steam CS2 child exited during startup.' }
    $receipt.ProcessIdentityVerified = $true
    $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receiptFile -Encoding utf8
    $receipt # Verifies process identity only. Inspect the map/runtime separately.
} catch {
    $receipt.Failure = $_.Exception.Message
    $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receiptFile -Encoding utf8
    throw
}
