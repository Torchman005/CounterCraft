# Run in a regular PowerShell session after reviewing this file.
# Installs only the named Codex plugin. It does not install a game loader.
param([string]$MarketplaceSource = 'rehan-remade/universal-modder')
$ErrorActionPreference = 'Stop'
$pluginSelector = 'universal-modder@universal-modder'
function Get-PluginState {
    $result = & codex plugin list --json
    if ($LASTEXITCODE -ne 0) { throw 'Plugin verification failed.' }
    return $result | ConvertFrom-Json
}

$state = Get-PluginState
$installed = @($state.installed | Where-Object { $_.pluginId -eq $pluginSelector -and $_.installed -and $_.enabled })
if ($installed.Count -eq 1) {
    $installed[0] | ConvertTo-Json -Depth 8
    return
}

$gitOverrideNames = @('GIT_CONFIG_COUNT', 'GIT_CONFIG_KEY_0', 'GIT_CONFIG_VALUE_0', 'GIT_CONFIG_KEY_1', 'GIT_CONFIG_VALUE_1')
$originalValues = @{}
foreach ($name in $gitOverrideNames) {
    $originalValues[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    # Use direct Git transport for this retry without changing the user's Git config.
    $env:GIT_CONFIG_COUNT = '2'
    $env:GIT_CONFIG_KEY_0 = 'http.proxy'
    $env:GIT_CONFIG_VALUE_0 = ''
    $env:GIT_CONFIG_KEY_1 = 'https.proxy'
    $env:GIT_CONFIG_VALUE_1 = ''
    $marketplaceJson = & codex plugin marketplace list --json
    if ($LASTEXITCODE -ne 0) { throw 'Marketplace inspection failed.' }
    $marketplaces = $marketplaceJson | ConvertFrom-Json
    if (-not ($marketplaces.marketplaces | Where-Object { $_.name -eq 'universal-modder' })) {
        & codex plugin marketplace add $MarketplaceSource
        if ($LASTEXITCODE -ne 0) { throw 'Marketplace registration failed.' }
    }
    & codex plugin add $pluginSelector --json
    if ($LASTEXITCODE -ne 0) { throw 'Plugin installation failed.' }
    $state = Get-PluginState
    $installed = @($state.installed | Where-Object { $_.pluginId -eq $pluginSelector -and $_.installed -and $_.enabled })
    if ($installed.Count -ne 1) { throw 'The requested plugin is not installed and enabled.' }
    $installed[0] | ConvertTo-Json -Depth 8
} finally {
    foreach ($name in $gitOverrideNames) {
        [Environment]::SetEnvironmentVariable($name, $originalValues[$name], 'Process')
    }
}
