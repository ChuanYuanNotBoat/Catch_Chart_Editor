# Builds, tests, and packages Catch Chart Editor with Inno Setup.
#
# Usage (run from anywhere):
#   powershell -ExecutionPolicy Bypass -File tools\packaging\build_and_package.ps1 `
#     [-Configuration Release] [-BuildDir <dir>] [-OutputDir <dir>]
#     [-SkipBuild] [-SkipTests] [-SkipPackage] [-DryRun]
#
# The version is read from CMakeLists.txt and cross checked against
# src/main.cpp, so a release can only be packaged when both agree.

param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$SkipPackage,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if (-not $BuildDir)  { $BuildDir  = Join-Path $repoRoot "build" }
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot "dist" }
$buildDir  = [IO.Path]::GetFullPath($BuildDir)
$outputDir = [IO.Path]::GetFullPath($OutputDir)

# ---- 1. Version consistency -------------------------------------------------
$cmakeText = [IO.File]::ReadAllText((Join-Path $repoRoot "CMakeLists.txt"))
if ($cmakeText -notmatch 'project\(CatchChartEditor\s+VERSION\s+(\d+\.\d+\.\d+)') {
    throw "Failed to read project version from CMakeLists.txt"
}
$projectVersion = $Matches[1]

$mainText = [IO.File]::ReadAllText((Join-Path $repoRoot "src\main.cpp"))
if ($mainText -notmatch 'setApplicationVersion\("Beta v(\d+\.\d+\.\d+)"\)') {
    throw "Failed to read application version from src/main.cpp"
}
$appVersion = $Matches[1]

if ($projectVersion -ne $appVersion) {
    throw "Version mismatch: CMakeLists.txt=$projectVersion but src/main.cpp=$appVersion"
}
$displayVersion = "Beta v$projectVersion"
$installerName  = "CatchChartEditor_" + ($displayVersion -replace ' ', '_') + "_Setup.exe"
Write-Host "Project version: $projectVersion ($displayVersion)"
Write-Host "Repo root:       $repoRoot"
Write-Host "Build dir:       $buildDir"
Write-Host "Output dir:      $outputDir"

if ($DryRun) {
    Write-Host "[DRY] configuration, build, tests, and packaging skipped"
    return
}

# ---- 2. Configure and build --------------------------------------------------
if (-not $SkipBuild) {
    cmake -S $repoRoot -B $buildDir -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
    cmake --build $buildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

$buildOutput = Join-Path $buildDir $Configuration
$exe = Join-Path $buildOutput "CatchChartEditor.exe"
if (-not (Test-Path $exe)) { throw "Missing $exe; did the build run for '$Configuration'?" }

# ---- 3. Verify the runtime version -------------------------------------------
$versionOutput = (& $exe --version 2>&1 | Out-String)
if ($versionOutput -notmatch [regex]::Escape($displayVersion)) {
    throw "Executable version check failed: output does not contain '$displayVersion'. Got: $($versionOutput.Trim())"
}
Write-Host "Executable reports: $($versionOutput.Trim())"

# ---- 4. Refresh the docs copy without stale leftovers -------------------------
# The CMake POST_BUILD step prunes first, but re-do it here so a package made
# from an already up-to-date build tree can never contain removed documents.
$docsDest = Join-Path $buildOutput "docs"
if (Test-Path $docsDest) { Remove-Item -Recurse -Force $docsDest }
Copy-Item -Recurse -Force (Join-Path $repoRoot "docs") $docsDest

# ---- 5. Tests ------------------------------------------------------------------
if (-not $SkipTests) {
    ctest --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
}

# ---- 6. Packaging completeness check -------------------------------------------
$required = @(
    "platforms", "multimedia", "imageformats", "tls", "networkinformation",
    "iconengines", "styles", "generic", "skins\default", "resources",
    "licenses\QtAdvancedDockingSystem", "licenses\libogg", "licenses\libvorbis",
    "notices\AutoTimingCore", "docs\version.md", "docs\history.md"
)
foreach ($rel in $required) {
    if (-not (Test-Path (Join-Path $buildOutput $rel))) {
        throw "Missing packaged item: $rel"
    }
}
Write-Host "Packaging completeness check passed."

# Nothing that ships to users may look like a test binary, symbol, or redist.
$forbidden = Get-ChildItem $buildOutput -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^(CatchChartEditorTests|CoordMapperTests|DockingLayoutTests|MetaEditPanelTests|ImageConverterTests|BpmMeasurementTests|AutoTiming\w*Tests|autotiming_\w+_tests|autotiming_probe|vc_redist)\.' -or $_.Extension -in '.pdb', '.ilk' }
if ($forbidden) {
    Write-Host "The following files exist but will be excluded by setup.iss:" -ForegroundColor Yellow
    $forbidden | ForEach-Object { Write-Host "  excluded: $($_.FullName.Substring($buildOutput.Length + 1))" }
}

# ---- 7. Inno Setup --------------------------------------------------------------
if (-not $SkipPackage) {
    $isccCandidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 5\ISCC.exe"
    )
    $iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw "ISCC.exe not found; install Inno Setup 6 or add it to PATH." }

    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    & $iscc "/DMyAppVersion=$projectVersion" "/DMyDisplayVersion=$displayVersion" "/DMyBuildDir=$buildOutput" "/DMyOutputDir=$outputDir" (Join-Path $PSScriptRoot "setup.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed" }

    $installer = Join-Path $outputDir $installerName
    if (-not (Test-Path $installer)) { throw "Expected installer not found: $installer" }
    Write-Host ""
    Write-Host "===== Done =====" -ForegroundColor Green
    Write-Host "Installer: $installer ($([math]::Round((Get-Item $installer).Length / 1MB, 1)) MB)"
}
