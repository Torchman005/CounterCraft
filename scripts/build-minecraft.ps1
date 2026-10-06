param([Parameter(Mandatory = $true)][string]$Gradle, [string]$JavaHome)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath $Gradle -PathType Leaf)) { throw "Gradle executable not found: $Gradle" }
$oldJava = [Environment]::GetEnvironmentVariable('JAVA_HOME', 'Process')
try {
    if ($JavaHome) { $env:JAVA_HOME = $JavaHome }
    & $Gradle --no-daemon --console=plain --info `
        '-Dorg.gradle.internal.http.connectionTimeout=15000' `
        '-Dorg.gradle.internal.http.socketTimeout=30000' `
        -g (Join-Path $projectRoot '.local\gradle') -p (Join-Path $projectRoot 'minecraft') build
    if ($LASTEXITCODE -ne 0) { throw "Minecraft build failed (exit $LASTEXITCODE)." }
} finally {
    [Environment]::SetEnvironmentVariable('JAVA_HOME', $oldJava, 'Process')
}
