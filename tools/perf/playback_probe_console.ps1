param(
    [string]$LogFile = "",
    [string]$LogDir = "",
    [switch]$Follow = $true,
    [switch]$Raw
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-LogFilePath {
    param(
        [string]$GivenLogFile,
        [string]$GivenLogDir
    )

    if ($GivenLogFile -and (Test-Path -LiteralPath $GivenLogFile)) {
        return (Resolve-Path -LiteralPath $GivenLogFile).Path
    }

    $candidateDirs = @()
    if ($GivenLogDir) {
        $candidateDirs += $GivenLogDir
    } else {
        $candidateDirs += (Join-Path (Get-Location).Path "logs")
        $candidateDirs += (Join-Path (Get-Location).Path "build_codex/Release/logs")
        $candidateDirs += (Join-Path (Get-Location).Path "build/logs")
    }

    foreach ($dir in $candidateDirs) {
        if (-not (Test-Path -LiteralPath $dir)) {
            continue
        }
        $latest = Get-ChildItem -LiteralPath $dir -Filter "CatchEditor_*.log" -File |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($latest) {
            return $latest.FullName
        }
    }

    throw "No CatchEditor log file found. Use -LogFile or -LogDir."
}

$resolved = Resolve-LogFilePath -GivenLogFile $LogFile -GivenLogDir $LogDir
Write-Host "Playback probe console attached to: $resolved"
Write-Host "Filtering lines containing PERF_PLAYBACK ..."

$stream = if ($Follow) {
    Get-Content -LiteralPath $resolved -Wait
} else {
    Get-Content -LiteralPath $resolved
}

$stream | ForEach-Object {
    $line = $_
    if ($line -notmatch "PERF_PLAYBACK") {
        return
    }

    if ($Raw) {
        Write-Host $line -ForegroundColor Cyan
        return
    }

    $pattern = '^(?<time>\d{2}:\d{2}:\d{2}\.\d{3}).*PERF_PLAYBACK\s+window_ms=(?<window>\d+)\s+fps_tick=(?<fps_tick>[0-9.]+)\s+fps_canvas=(?<fps_canvas>[0-9.]+)\s+fps_preview=(?<fps_preview>[0-9.]+)\s+jitter_p95_ms=(?<jitter>[0-9.]+)\s+pacing_std_ms=(?<pacing_std>[0-9.]+)\s+pacing_jerk_p95_ms=(?<pacing_jerk>[0-9.]+)\s+step_jerk_p95_ms=(?<step_jerk>[0-9.]+)\s+ui_gap_p95_ms=(?<ui_gap>[0-9.]+)(?:\s+scroll_velocity_change_p95_pct=[0-9.]+)?\s+jank_events=(?<jank>\d+)\s+step_jank_events=(?<step_jank>\d+)(?:\s+manual_jerk_marks=(?<marks>\d+))?\s+ui_hitch_events=(?<ui_hitch>\d+)\s+ui_stall_events=(?<ui_stall>\d+)\s+jitter_slow_pct=(?<jitter_slow>[0-9.]+)\s+canvas_slow_pct=(?<canvas_slow>[0-9.]+)\s+top=\[(?<top>.*?)\]\s+counters=\[(?<counters>.*)\]'
    if ($line -match $pattern) {
        $time = $Matches["time"]
        $fpsTick = [double]$Matches["fps_tick"]
        $fpsCanvas = [double]$Matches["fps_canvas"]
        $fpsPreview = [double]$Matches["fps_preview"]
        $jitter = [double]$Matches["jitter"]
        $pacingStd = [double]$Matches["pacing_std"]
        $pacingJerk = [double]$Matches["pacing_jerk"]
        $stepJerk = [double]$Matches["step_jerk"]
        $uiGap = [double]$Matches["ui_gap"]
        $jank = [int]$Matches["jank"]
        $stepJank = [int]$Matches["step_jank"]
        $marks = $(if ($Matches.ContainsKey("marks")) { [int]$Matches["marks"] } else { 0 })
        $uiHitch = [int]$Matches["ui_hitch"]
        $uiStall = [int]$Matches["ui_stall"]
        $canvasSlow = [double]$Matches["canvas_slow"]
        $top = $Matches["top"]

        $color = "Green"
        if ($fpsCanvas -lt 50 -or $jitter -gt 25 -or $canvasSlow -gt 20 -or $pacingJerk -gt 8 -or $stepJerk -gt 10 -or $jank -gt 15 -or $stepJank -gt 12 -or $uiGap -gt 32 -or $uiHitch -gt 8 -or $uiStall -gt 3) {
            $color = "Red"
        } elseif ($fpsCanvas -lt 57 -or $jitter -gt 20 -or $canvasSlow -gt 10 -or $pacingJerk -gt 5 -or $stepJerk -gt 6 -or $jank -gt 5 -or $stepJank -gt 4 -or $uiGap -gt 24 -or $uiHitch -gt 3 -or $uiStall -gt 1) {
            $color = "Yellow"
        }

        $msg = "[{0}] fps(c/t/p)={1:N1}/{2:N1}/{3:N1} jitter_p95={4:N2}ms pace_std={5:N2}ms jerk={6:N2}/{7:N2}ms ui_gap_p95={8:N2}ms jank={9}/{10} marks={11} ui_hitch={12} ui_stall={13} canvas_slow={14:N1}% top={15}" -f `
            $time, $fpsCanvas, $fpsTick, $fpsPreview, $jitter, $pacingStd, $pacingJerk, $stepJerk, $uiGap, $jank, $stepJank, $marks, $uiHitch, $uiStall, $canvasSlow, $top
        Write-Host ($msg -replace ' marks=0(?= )', '') -ForegroundColor $color
    } else {
        Write-Host $line -ForegroundColor Cyan
    }
}
