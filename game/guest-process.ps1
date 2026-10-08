function Test-RepositoryGuest([int]$processId, [string]$JavaExecutable, [string]$ProjectRoot) {
    $process=Get-CimInstance Win32_Process -Filter "ProcessId=$processId"
    if(-not $process -or $process.ExecutablePath -ne $JavaExecutable -or -not $process.CommandLine){return $false}
    $line=$process.CommandLine.Replace('/','\')
    return $line.Contains($ProjectRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -and
        $line -match '(?:^|\s)net\.fabricmc\.devlaunchinjector\.Main(?:\s|$)' -and
        $line -match '(?:^|\s|")-Dcountercraft.enabled=true(?:\s|"|$)'
}
function Test-GuestDescendant([int]$processId,[int]$ancestor) {
    for($level=0;$level -lt 16 -and $processId -gt 0;$level++){
        if($processId -eq $ancestor){return $true}
        $entry=Get-CimInstance Win32_Process -Filter "ProcessId=$processId"
        if(-not $entry){return $false};$processId=$entry.ParentProcessId
    }
    return $false
}
