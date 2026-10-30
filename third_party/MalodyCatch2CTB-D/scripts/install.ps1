param(
    [Parameter(Mandatory=$true)][string]$CceExeDir,
    [string]$Compiler = 'ldc2'
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $CceExeDir 'CatchChartEditor.exe') -PathType Leaf)) {
    throw 'CceExeDir must contain CatchChartEditor.exe'
}
$Root = Split-Path -Parent $PSScriptRoot
Push-Location $Root
try {
    dub build --compiler=$Compiler --build=release
    if ($LASTEXITCODE -ne 0) { throw "dub build failed ($LASTEXITCODE)" }
    $dest = Join-Path $CceExeDir 'plugins\malodycatch2ctb-d'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item -Force (Join-Path $Root 'malodycatch2ctb-d.exe') (Join-Path $dest 'malodycatch2ctb-d.exe')
    Copy-Item -Force (Join-Path $Root 'plugin\malodycatch2ctb-d.plugin.json') (Join-Path $dest 'malodycatch2ctb-d.plugin.json')
    Copy-Item -Force (Join-Path $Root 'LICENSE') (Join-Path $dest 'LICENSE')
    Copy-Item -Force (Join-Path $Root 'README.md') (Join-Path $dest 'README.md')
    Write-Host "Installed CCE process plugin to $dest"
} finally { Pop-Location }
