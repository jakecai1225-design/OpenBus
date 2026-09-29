# Sync source plugins/ → build/bin/plugins/ (workaround when openbus.exe is locked).
# Prefer rebuilding after PluginManager::resolvePluginsDir (loads source live).
# Usage: powershell -File scripts/sync_plugins_to_build.ps1

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Src = Join-Path $Root "plugins"
$Dst = Join-Path $Root "build\bin\plugins"

if (-not (Test-Path $Src)) { throw "missing $Src" }
New-Item -ItemType Directory -Force -Path $Dst | Out-Null

Get-ChildItem $Src -Directory | ForEach-Object {
    $name = $_.Name
    Write-Host "sync $name"
    & robocopy $_.FullName (Join-Path $Dst $name) /E /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $name (code $LASTEXITCODE)" }
}

Write-Host "OK → $Dst"
Write-Host "Restart OpenBus and reactivate the suite to pick up changes."
