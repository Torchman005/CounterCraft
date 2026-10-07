param(
    [Parameter(Mandatory = $true)][string]$Gradle,
    [string]$JavaHome,
    [ValidateSet('build', 'runClient')][string]$Task = 'build',
    [switch]$NullAudio
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath $Gradle -PathType Leaf)) { throw "Gradle executable not found: $Gradle" }
$oldJava = [Environment]::GetEnvironmentVariable('JAVA_HOME', 'Process')
$oldAudioDrivers = [Environment]::GetEnvironmentVariable('ALSOFT_DRIVERS', 'Process')
try {
    if ($JavaHome) { $env:JAVA_HOME = $JavaHome }
    if ($NullAudio) { $env:ALSOFT_DRIVERS = 'null' }
    $gradleArguments = @('--no-daemon', '--console=plain', '--info',
        '-Dorg.gradle.internal.http.connectionTimeout=15000',
        '-Dorg.gradle.internal.http.socketTimeout=30000')
    # Java does not automatically inherit HTTP(S)_PROXY. Forward only the host
    # and port for this build; never print or pass embedded proxy credentials.
    foreach ($scheme in @('http', 'https')) {
        $value = [Environment]::GetEnvironmentVariable("$($scheme.ToUpper())_PROXY", 'Process')
        if ($value) {
            $proxy = [Uri]$value
            if ($proxy.Scheme -ne 'http' -or $proxy.UserInfo -or -not $proxy.Host -or $proxy.Port -lt 1) {
                throw 'Gradle proxy must be an HTTP URL without embedded credentials.'
            }
            $gradleArguments += "-D${scheme}.proxyHost=$($proxy.Host)", "-D${scheme}.proxyPort=$($proxy.Port)"
        }
    }
    $gradleArguments += '-g', (Join-Path $projectRoot '.local\gradle'),
        '-p', (Join-Path $projectRoot 'minecraft'), $Task
    & $Gradle @gradleArguments
    if ($LASTEXITCODE -ne 0) { throw "Minecraft build failed (exit $LASTEXITCODE)." }
} finally {
    [Environment]::SetEnvironmentVariable('JAVA_HOME', $oldJava, 'Process')
    [Environment]::SetEnvironmentVariable('ALSOFT_DRIVERS', $oldAudioDrivers, 'Process')
}
