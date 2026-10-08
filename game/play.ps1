#Requires -Version 7.4
param(
    [ValidateSet('Preview','Play','Recover')][string]$Mode='Preview',
    [Parameter(Mandatory=$true)][string]$Cs2Root,
    [string]$BackupSnapshot,
    [string]$SessionDirectory,
    [string]$Loader,
    [string]$SteamExecutable
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(-not $SessionDirectory){$SessionDirectory=Join-Path $projectRoot '.local\play-session'}
$SessionDirectory=[IO.Path]::GetFullPath($SessionDirectory)
if(($SessionDirectory.TrimEnd('\')+'\').StartsWith(([IO.Path]::GetFullPath($Cs2Root).TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){
    throw 'SessionDirectory must be outside the game installation.'
}
$stateFile=Join-Path $SessionDirectory 'loader-state.json'
$receiptFile=Join-Path $SessionDirectory 'session.json'
$candidate=Join-Path $SessionDirectory 'candidate'
if(-not $Loader){$Loader=Join-Path $projectRoot '.local\reshade-runtime\ReShade64.dll'}
$scripts=Join-Path $projectRoot 'scripts'
if($Mode -eq 'Preview') {
    [pscustomobject]@{Mode='Preview';GameFilesWritten=$false;Cs2Root=[IO.Path]::GetFullPath($Cs2Root)
        Minecraft='Start the isolated Fabric client and enter an unpaused singleplayer world first.'
        LaunchRoute='Steam -insecure';Rendering='Full Minecraft client passthrough; no CS2 world fusion'
        BackupRequired=$true;BackupSnapshot=$BackupSnapshot;SessionDirectory=$SessionDirectory
        Recovery="./game/play.ps1 -Mode Recover -Cs2Root '$Cs2Root' -SessionDirectory '$SessionDirectory'"
        Exit='Close CS2; this supervisor restores its temporary loader automatically.'}
    return
}
if($Mode -eq 'Recover') {
    if(-not (Test-Path -LiteralPath $stateFile)){throw 'No recorded loader state in this session.'}
    $state=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    if($state.Mode -eq 'Restored'){[pscustomobject]@{Mode='AlreadyRestored';SessionDirectory=$SessionDirectory};return}
    & (Join-Path $scripts 'manage-cs2-loader.ps1') -Mode Restore -Cs2Root $Cs2Root -StateFile $stateFile
    if(Test-Path -LiteralPath $receiptFile){
        $receipt=Get-Content -LiteralPath $receiptFile -Raw | ConvertFrom-Json
        $receipt.Mode='Restored';$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptFile -Encoding utf8
    }
    return
}
if(Get-Process -Name cs2 -ErrorAction SilentlyContinue){throw 'Close the existing CS2 process before Play.'}
if(Test-Path -LiteralPath $stateFile){throw 'Session already exists. Recover it, then use a new SessionDirectory.'}
if(-not $BackupSnapshot -or -not (Test-Path -LiteralPath $BackupSnapshot -PathType Leaf)){throw 'Play requires a completed pre-loader backup ZIP.'}
if(-not (Test-Path -LiteralPath $Loader -PathType Leaf)){throw 'Fetch the pinned ReShade runtime first.'}
if(-not (Test-Path -LiteralPath (Join-Path $projectRoot '.local\native-build\CounterCraftProbe.addon64'))){throw 'Build the native adapter first.'}
Push-Location $projectRoot
try {
    # A short-lived health check cannot retain the exclusive Minecraft lease.
    $health=& python -m bridge.health
    if($LASTEXITCODE -ne 0){throw 'Minecraft readiness check failed. Start the isolated client and close menus.'}
    New-Item -ItemType Directory -Path $SessionDirectory -Force | Out-Null
    $receipt=[pscustomobject]@{Mode='Preparing';Cs2Root=[IO.Path]::GetFullPath($Cs2Root);StateFile=$stateFile
        ProcessId=0;ProcessStartedUtc='';CreatedUtc=[DateTime]::UtcNow.ToString('o');MinecraftHealth=($health | ConvertFrom-Json)
        Failure='';RestoreFailure=''}
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptFile -Encoding utf8
    try {
        & (Join-Path $scripts 'prepare-cs2-lab.ps1') -Cs2Root $Cs2Root -Destination $candidate | Out-Null
        & (Join-Path $scripts 'manage-cs2-loader.ps1') -Mode Install -Cs2Root $Cs2Root -Candidate $candidate -Loader $Loader -BackupSnapshot $BackupSnapshot -StateFile $stateFile -SteamLaunch | Out-Null
        $launch=@{StateFile=$stateFile;Gameplay=$true;Launch=$true}
        if($SteamExecutable){$launch.SteamExecutable=$SteamExecutable}
        $started=& (Join-Path $scripts 'launch-cs2-lab.ps1') @launch
        if(-not $started.ProcessIdentityVerified -or $started.ProcessId -lt 1){throw 'No verified Steam child.'}
        $owned=Get-Process -Id $started.ProcessId
        $receipt.ProcessId=$owned.Id;$receipt.ProcessStartedUtc=$owned.StartTime.ToUniversalTime().ToString('o');$receipt.Mode='Running'
        $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptFile -Encoding utf8
        Write-Host "CounterCraft offline session PID $($owned.Id). Close CS2 to finish and restore. F8 toggles the MC view/input."
        while(-not $owned.HasExited){Start-Sleep -Milliseconds 500;$owned.Refresh()}
        $owned.Dispose()
    } catch {
        $receipt.Failure=$_.Exception.Message;throw
    } finally {
        if(Test-Path -LiteralPath $stateFile){
            try {
                $recorded=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
                if($recorded.Mode -eq 'Installed') {
                    # A signalled process can still leave DLL image/file handles
                    # in teardown briefly. Every bounded retry reruns hash guards.
                    for($attempt=0;$attempt -lt 10;$attempt++) {
                        try {
                            & (Join-Path $scripts 'manage-cs2-loader.ps1') -Mode Restore -Cs2Root $Cs2Root -StateFile $stateFile | Out-Null
                            break
                        } catch {
                            if($attempt -eq 9 -or ($_.Exception.Message -notmatch 'denied|existing CS2 process')){throw}
                            Start-Sleep -Milliseconds 1000
                        }
                    }
                    $receipt.Mode='Restored'
                } else {$receipt.Mode='RecoveryRequired'}
            } catch {$receipt.Mode='RecoveryRequired';$receipt.RestoreFailure=$_.Exception.Message}
        } else {$receipt.Mode='NoInstallation'}
        $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptFile -Encoding utf8
        if($receipt.Mode -eq 'RecoveryRequired'){Write-Warning "Recovery required: $receiptFile. Close CS2 and run -Mode Recover with this exact SessionDirectory."}
    }
    $receipt
} finally {Pop-Location}
