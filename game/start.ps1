#Requires -Version 7.4
param([string]$ConfigPath,[string]$SessionDirectory,[switch]$Launch,
    [switch]$WorldFusion,[string]$CameraLayout,[string]$FusionPolicy)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'guest-process.ps1')
if(-not $ConfigPath){$ConfigPath=Join-Path $projectRoot '.local\countercraft-machine.json'}
$config=Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
foreach($key in @('Cs2Root','Gradle','JavaHome','BackupSnapshot','World')){
    if(-not $config.$key -or $config.$key -isnot [string]){throw "Missing string config field: $key"}
}
if($config.World -notmatch '^[A-Za-z0-9 _-]+$'){throw 'World name contains unsupported characters.'}
if($WorldFusion){
    foreach($calibration in @($CameraLayout,$FusionPolicy)){
        if(-not $calibration -or -not (Test-Path -LiteralPath $calibration -PathType Leaf)){
            throw 'Experimental WorldFusion requires CameraLayout and FusionPolicy files.'
        }
        if((Get-Item -LiteralPath $calibration).Length -gt 65536){throw 'Fusion calibration exceeds 64KiB.'}
    }
}elseif($CameraLayout -or $FusionPolicy){throw 'Calibration arguments require -WorldFusion.'}
$javaExe=[IO.Path]::GetFullPath((Join-Path $config.JavaHome 'bin\java.exe'))
if(-not $SessionDirectory){$SessionDirectory=Join-Path $projectRoot ('.local\sessions\'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))}
$SessionDirectory=[IO.Path]::GetFullPath($SessionDirectory)
if(($SessionDirectory.TrimEnd('\')+'\').StartsWith(([IO.Path]::GetFullPath($config.Cs2Root).TrimEnd('\')+'\'),[StringComparison]::OrdinalIgnoreCase)){
    throw 'SessionDirectory must be outside the game installation.'
}
if(-not $Launch){
    [pscustomobject]@{Mode='Preview';GameFilesWritten=$false;Cs2Root=$config.Cs2Root;World=$config.World
        Guest='Isolated minecraft/run client; reuse only a verified repository dev client'
        SessionDirectory=$SessionDirectory;ConfigPath=[IO.Path]::GetFullPath($ConfigPath)
        WorldFusion=[bool]$WorldFusion
        Rendering=if($WorldFusion){'Experimental static world fusion; no shared collision/gameplay'}else{'Full Minecraft client passthrough'}
        Exit='Close CS2; restore loader; close Minecraft only if this launch started it.'}
    return
}
if(Get-Process -Name cs2 -ErrorAction SilentlyContinue){throw 'Close CS2 before starting a session.'}
foreach($path in @($config.Gradle,(Join-Path $config.JavaHome 'bin\java.exe'),$config.BackupSnapshot,
    (Join-Path $projectRoot "minecraft\run\saves\$($config.World)"))){
    if(-not (Test-Path -LiteralPath $path)){throw "Missing prerequisite: $path"}
}
if(Test-Path -LiteralPath $SessionDirectory){throw 'Use a fresh SessionDirectory.'}
New-Item -ItemType Directory -Path $SessionDirectory | Out-Null
if(Test-Path -LiteralPath (Join-Path $projectRoot 'manifest.json')){
    & python (Join-Path $projectRoot 'scripts\package-alpha.py') verify $projectRoot
    if($LASTEXITCODE -ne 0){throw 'Package verification failed; no game process was launched.'}
}
$guestReceiptFile=Join-Path $SessionDirectory 'guest.json'
$guestReceipt=[pscustomobject]@{Mode='Preparing';Owned=$false;ProcessId=0;StartedUtc='';WorkerId=0;World=$config.World;Failure=''}
$guestWorker=$null;$ownedGuest=$null
Push-Location $projectRoot
try {
    $listener=@(Get-NetTCPConnection -LocalPort 37122 -State Listen -ErrorAction SilentlyContinue)
    if($listener.Count -gt 1){throw 'More than one guest listener.'}
    if($listener.Count -eq 1){
        if(-not (Test-RepositoryGuest $listener[0].OwningProcess $javaExe $projectRoot)){throw 'Port 37122 belongs to an unverified client; refusing to reuse it.'}
        $guestReceipt.Mode='Reused';$guestReceipt.ProcessId=$listener[0].OwningProcess
    } else {
        # Serialize literal values to a worker script, never interpolate shell code.
        function Quote-Literal([string]$value){"'"+$value.Replace("'","''")+"'"}
        $worker=Join-Path $SessionDirectory 'start-guest.ps1'
        $parameters=@{Gradle=$config.Gradle;JavaHome=$config.JavaHome;Task='runClient';World=$config.World;Background=$true}
        if($config.NullAudio -eq $true){$parameters.NullAudio=$true}
        $literal=foreach($entry in $parameters.GetEnumerator()){
            (Quote-Literal $entry.Key)+'='+$(if($entry.Value -is [bool]){'$true'}else{Quote-Literal ([string]$entry.Value)})
        }
        $source='$ErrorActionPreference=''Stop'''+"`n"+'$guestArgs=@{'+($literal -join ';')+'}'+"`n"+
            '& '+(Quote-Literal (Join-Path $projectRoot 'scripts\build-minecraft.ps1'))+' @guestArgs'
        Set-Content -LiteralPath $worker -Value $source -Encoding utf8
        $shell=(Get-Process -Id $PID).Path
        $guestWorker=Start-Process -FilePath $shell -ArgumentList @('-NoProfile','-File',('"'+$worker+'"')) -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $SessionDirectory 'guest.stdout.log') -RedirectStandardError (Join-Path $SessionDirectory 'guest.stderr.log')
        $guestReceipt.Mode='Starting';$guestReceipt.Owned=$true;$guestReceipt.WorkerId=$guestWorker.Id
        $guestReceipt | ConvertTo-Json | Set-Content -LiteralPath $guestReceiptFile -Encoding utf8
        $deadline=[DateTime]::UtcNow.AddMinutes(3)
        do {
            if($guestWorker.HasExited){throw 'Minecraft worker exited; inspect guest stdout/stderr logs.'}
            $listener=@(Get-NetTCPConnection -LocalPort 37122 -State Listen -ErrorAction SilentlyContinue)
            if($listener.Count -eq 1){
                $candidateId=$listener[0].OwningProcess
                if(-not (Test-RepositoryGuest $candidateId $javaExe $projectRoot) -or -not (Test-GuestDescendant $candidateId $guestWorker.Id)){
                    throw 'New guest listener is not the owned repository client.'
                }
                $ownedGuest=Get-Process -Id $candidateId
                $guestReceipt.ProcessId=$candidateId;$guestReceipt.StartedUtc=$ownedGuest.StartTime.ToUniversalTime().ToString('o')
                $health=& python -m bridge.health 2>$null
                if($LASTEXITCODE -eq 0){$guestReceipt.Mode='Ready';break}
            }
            Start-Sleep -Seconds 1
        } while([DateTime]::UtcNow -lt $deadline)
        if($guestReceipt.Mode -ne 'Ready'){throw 'Guest world did not become ready within three minutes.'}
    }
    $guestReceipt | ConvertTo-Json | Set-Content -LiteralPath $guestReceiptFile -Encoding utf8
    $playArgs=@{Mode='Play';Cs2Root=$config.Cs2Root;BackupSnapshot=$config.BackupSnapshot;SessionDirectory=$SessionDirectory}
    if($WorldFusion){$playArgs.WorldFusion=$true;$playArgs.CameraLayout=[IO.Path]::GetFullPath($CameraLayout);$playArgs.FusionPolicy=[IO.Path]::GetFullPath($FusionPolicy)}
    foreach($key in @('Loader','NativeBuild','SteamExecutable')){
        if($config.$key){$playArgs[$key]=[string]$config.$key}
    }
    & (Join-Path $PSScriptRoot 'play.ps1') @playArgs
} catch {
    $guestReceipt.Failure=$_.Exception.Message;throw
} finally {
    # Startup may fail before the listener exists. Only adopt a verified client
    # descended from this exact worker, never another launcher or Java daemon.
    if(-not $ownedGuest -and $guestWorker){
        foreach($candidate in @(Get-CimInstance Win32_Process -Filter "Name='java.exe'")){
            if((Test-RepositoryGuest $candidate.ProcessId $javaExe $projectRoot) -and
                (Test-GuestDescendant $candidate.ProcessId $guestWorker.Id)){
                $ownedGuest=Get-Process -Id $candidate.ProcessId -ErrorAction SilentlyContinue
                if($ownedGuest -and $ownedGuest.StartTime -ge $guestWorker.StartTime){
                    $guestReceipt.ProcessId=$ownedGuest.Id;$guestReceipt.StartedUtc=$ownedGuest.StartTime.ToUniversalTime().ToString('o');break
                }
                $ownedGuest=$null
            }
        }
    }
    if($ownedGuest){
        try {
            if(-not $ownedGuest.HasExited -and (Test-RepositoryGuest $ownedGuest.Id $javaExe $projectRoot)){
                [void]$ownedGuest.CloseMainWindow()
                if(-not $ownedGuest.WaitForExit(10000)){$guestReceipt.Mode='GuestStillRunning'}else{$guestReceipt.Mode='Closed'}
            }else{$guestReceipt.Mode='Closed'}
        } catch {$guestReceipt.Failure=$_.Exception.Message}
        $ownedGuest.Dispose()
    }
    if($guestWorker){
        if(-not $guestWorker.HasExited -and -not $guestWorker.WaitForExit(10000)){
            if(-not $guestReceipt.Failure){$guestReceipt.Failure='Guest worker is still running; inspect the recorded WorkerId and logs.'}
        }
        $guestWorker.Dispose()
    }
    $guestReceipt | ConvertTo-Json | Set-Content -LiteralPath $guestReceiptFile -Encoding utf8
    Pop-Location
}
