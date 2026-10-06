# Run in a regular PowerShell session after reviewing this file.
# Installs only the named Codex plugin. It does not install a game loader.
$ErrorActionPreference = 'Stop'
$gitOverrideNames = @('GIT_CONFIG_COUNT', 'GIT_CONFIG_KEY_0', 'GIT_CONFIG_VALUE_0', 'GIT_CONFIG_KEY_1', 'GIT_CONFIG_VALUE_1')
$originalValues = @{}
foreach ($name in $gitOverrideNames) {
    $originalValues[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    # The user's configured proxy is unavailable; override it only for this process.
    $env:GIT_CONFIG_COUNT = '2'
    $env:GIT_CONFIG_KEY_0 = 'http.proxy'
    $env:GIT_CONFIG_VALUE_0 = ''
    $env:GIT_CONFIG_KEY_1 = 'https.proxy'
    $env:GIT_CONFIG_VALUE_1 = ''
    & codex plugin marketplace add rehan-remade/universal-modder
    if ($LASTEXITCODE -ne 0) { throw 'Marketplace registration failed.' }
    & codex plugin add universal-modder@universal-modder --json
    if ($LASTEXITCODE -ne 0) { throw 'Plugin installation failed.' }
    & codex plugin list --json
    if ($LASTEXITCODE -ne 0) { throw 'Plugin verification failed.' }
} finally {
    foreach ($name in $gitOverrideNames) {
        [Environment]::SetEnvironmentVariable($name, $originalValues[$name], 'Process')
    }
}
