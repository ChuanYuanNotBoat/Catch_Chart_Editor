param(
    [string]$LogFile = "",
    [string]$LogDir = "",
    [string]$OutCsv = ""
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

function Get-Percentile {
    param(
        [double[]]$Values,
        [double]$P
    )

    if (-not $Values -or $Values.Count -eq 0) {
        return 0.0
    }
    $sorted = $Values | Sort-Object
    $pp = [Math]::Max(0.0, [Math]::Min(1.0, $P))
    $idx = [int][Math]::Floor($pp * ($sorted.Count - 1))
    return [double]$sorted[$idx]
}

function Get-Mean {
    param([double[]]$Values)
    if (-not $Values -or $Values.Count -eq 0) {
        return 0.0
    }
    $sum = 0.0
    foreach ($v in $Values) {
        $sum += [double]$v
    }
    return $sum / [double]$Values.Count
}

$resolved = Resolve-LogFilePath -GivenLogFile $LogFile -GivenLogDir $LogDir
Write-Host "Analyzing playback probe log: $resolved"

$pattern = '^(?<time>\d{2}:\d{2}:\d{2}\.\d{3}).*PERF_PLAYBACK\s+window_ms=(?<window>\d+)\s+fps_tick=(?<fps_tick>[0-9.]+)\s+fps_canvas=(?<fps_canvas>[0-9.]+)\s+fps_preview=(?<fps_preview>[0-9.]+)\s+jitter_p95_ms=(?<jitter>[0-9.]+)\s+pacing_std_ms=(?<pacing_std>[0-9.]+)\s+pacing_jerk_p95_ms=(?<pacing_jerk>[0-9.]+)\s+step_jerk_p95_ms=(?<step_jerk>[0-9.]+)\s+ui_gap_p95_ms=(?<ui_gap>[0-9.]+)(?:\s+scroll_velocity_change_p95_pct=[0-9.]+)?\s+jank_events=(?<jank>\d+)\s+step_jank_events=(?<step_jank>\d+)(?:\s+manual_jerk_marks=(?<marks>\d+))?\s+ui_hitch_events=(?<ui_hitch>\d+)\s+ui_stall_events=(?<ui_stall>\d+)\s+jitter_slow_pct=(?<jitter_slow>[0-9.]+)\s+canvas_slow_pct=(?<canvas_slow>[0-9.]+)\s+top=\[(?<top>.*?)\]\s+counters=\[(?<counters>.*)\]'
$markContextPattern = '^(?<time>\d{2}:\d{2}:\d{2}\.\d{3}).*PERF_PLAYBACK_MARK_CONTEXT\s+type=manual_jerk\s+mark_id=(?<mark_id>\d+)\s+lookback_ms=(?<lookback>\d+)\s+pulse_max_ms=(?<pulse_max>[0-9.]+)\s+pulse_p95_ms=(?<pulse_p95>[0-9.]+)\s+step_max_ms=(?<step_max>[0-9.]+)\s+step_p95_ms=(?<step_p95>[0-9.]+)\s+ui_gap_max_ms=(?<ui_gap_max>[0-9.]+)\s+canvas_max_ms=(?<canvas_max>[0-9.]+)\s+preview_max_ms=(?<preview_max>[0-9.]+)\s+live_jitter_p95_ms=(?<live_jitter>[0-9.]+)\s+live_step_jerk_p95_ms=(?<live_step_jerk>[0-9.]+)\s+live_ui_gap_p95_ms=(?<live_ui_gap>[0-9.]+)(?:\s+visual_scroll_step_max_px=(?<visual_scroll_step_max>[0-9.]+)\s+visual_scroll_jerk_max_px=(?<visual_scroll_jerk_max>[0-9.]+)\s+visual_playhead_drift_max_px=(?<visual_playhead_drift_max>[0-9.]+)\s+live_visual_scroll_jerk_p95_px=(?<live_visual_scroll_jerk>[0-9.]+)\s+live_visual_playhead_drift_p95_px=(?<live_visual_playhead_drift>[0-9.]+))?'

# New logs omit manual markers; keep the context parser for archived sessions.
$rows = New-Object System.Collections.Generic.List[object]
$markContexts = New-Object System.Collections.Generic.List[object]
$hotKeyCount = @{}
$counterSums = @{}

Get-Content -LiteralPath $resolved | ForEach-Object {
    $line = $_
    if ($line -notmatch $pattern) {
        return
    }

    $row = [PSCustomObject]@{
        time            = $Matches["time"]
        window_ms       = [int]$Matches["window"]
        fps_tick        = [double]$Matches["fps_tick"]
        fps_canvas      = [double]$Matches["fps_canvas"]
        fps_preview     = [double]$Matches["fps_preview"]
        jitter_p95_ms   = [double]$Matches["jitter"]
        pacing_std_ms   = [double]$Matches["pacing_std"]
        pacing_jerk_p95_ms = [double]$Matches["pacing_jerk"]
        step_jerk_p95_ms = [double]$Matches["step_jerk"]
        ui_gap_p95_ms   = [double]$Matches["ui_gap"]
        jank_events     = [int]$Matches["jank"]
        step_jank_events = [int]$Matches["step_jank"]
        manual_jerk_marks = $(if ($Matches.ContainsKey("marks")) { [int]$Matches["marks"] } else { 0 })
        ui_hitch_events = [int]$Matches["ui_hitch"]
        ui_stall_events = [int]$Matches["ui_stall"]
        jitter_slow_pct = [double]$Matches["jitter_slow"]
        canvas_slow_pct = [double]$Matches["canvas_slow"]
        top             = $Matches["top"]
        counters        = $Matches["counters"]
    }
    $rows.Add($row) | Out-Null

    $topEntries = $row.top -split ';'
    foreach ($entry in $topEntries) {
        $trimmed = $entry.Trim()
        if (-not $trimmed) {
            continue
        }
        if ($trimmed -match '^(?<key>[^()]+)\(') {
            $k = $Matches["key"].Trim()
            if (-not $hotKeyCount.ContainsKey($k)) {
                $hotKeyCount[$k] = 0
            }
            $hotKeyCount[$k] += 1
        }
    }

    $counterPairs = $row.counters -split ','
    foreach ($pair in $counterPairs) {
        $trimmed = $pair.Trim()
        if (-not $trimmed) {
            continue
        }
        if ($trimmed -match '^(?<k>[^=]+)=(?<v>-?\d+)$') {
            $k = $Matches["k"].Trim()
            $v = [int64]$Matches["v"]
            if (-not $counterSums.ContainsKey($k)) {
                $counterSums[$k] = [int64]0
            }
            $counterSums[$k] += $v
        }
    }
}

Get-Content -LiteralPath $resolved | ForEach-Object {
    $line = $_
    if ($line -notmatch $markContextPattern) {
        return
    }

    $visualScrollStepMax = if ($Matches.ContainsKey("visual_scroll_step_max") -and $Matches["visual_scroll_step_max"]) { [double]$Matches["visual_scroll_step_max"] } else { 0.0 }
    $visualScrollJerkMax = if ($Matches.ContainsKey("visual_scroll_jerk_max") -and $Matches["visual_scroll_jerk_max"]) { [double]$Matches["visual_scroll_jerk_max"] } else { 0.0 }
    $visualPlayheadDriftMax = if ($Matches.ContainsKey("visual_playhead_drift_max") -and $Matches["visual_playhead_drift_max"]) { [double]$Matches["visual_playhead_drift_max"] } else { 0.0 }
    $liveVisualScrollJerk = if ($Matches.ContainsKey("live_visual_scroll_jerk") -and $Matches["live_visual_scroll_jerk"]) { [double]$Matches["live_visual_scroll_jerk"] } else { 0.0 }
    $liveVisualPlayheadDrift = if ($Matches.ContainsKey("live_visual_playhead_drift") -and $Matches["live_visual_playhead_drift"]) { [double]$Matches["live_visual_playhead_drift"] } else { 0.0 }

    $markContexts.Add([PSCustomObject]@{
        time = $Matches["time"]
        mark_id = [int]$Matches["mark_id"]
        lookback_ms = [int]$Matches["lookback"]
        pulse_max_ms = [double]$Matches["pulse_max"]
        pulse_p95_ms = [double]$Matches["pulse_p95"]
        step_max_ms = [double]$Matches["step_max"]
        step_p95_ms = [double]$Matches["step_p95"]
        ui_gap_max_ms = [double]$Matches["ui_gap_max"]
        canvas_max_ms = [double]$Matches["canvas_max"]
        preview_max_ms = [double]$Matches["preview_max"]
        live_jitter_p95_ms = [double]$Matches["live_jitter"]
        live_step_jerk_p95_ms = [double]$Matches["live_step_jerk"]
        live_ui_gap_p95_ms = [double]$Matches["live_ui_gap"]
        visual_scroll_step_max_px = $visualScrollStepMax
        visual_scroll_jerk_max_px = $visualScrollJerkMax
        visual_playhead_drift_max_px = $visualPlayheadDriftMax
        live_visual_scroll_jerk_p95_px = $liveVisualScrollJerk
        live_visual_playhead_drift_p95_px = $liveVisualPlayheadDrift
    }) | Out-Null
}

if ($rows.Count -eq 0) {
    Write-Host "No PERF_PLAYBACK windows found in log. Enable probe and run a playback session first."
    return
}

$fpsCanvasValues = @($rows | ForEach-Object { $_.fps_canvas })
$fpsTickValues = @($rows | ForEach-Object { $_.fps_tick })
$jitterValues = @($rows | ForEach-Object { $_.jitter_p95_ms })
$pacingStdValues = @($rows | ForEach-Object { $_.pacing_std_ms })
$pacingJerkValues = @($rows | ForEach-Object { $_.pacing_jerk_p95_ms })
$stepJerkValues = @($rows | ForEach-Object { $_.step_jerk_p95_ms })
$uiGapValues = @($rows | ForEach-Object { $_.ui_gap_p95_ms })
$jankValues = @($rows | ForEach-Object { [double]$_.jank_events })
$stepJankValues = @($rows | ForEach-Object { [double]$_.step_jank_events })
$markValues = @($rows | ForEach-Object { [double]$_.manual_jerk_marks })
$uiHitchValues = @($rows | ForEach-Object { [double]$_.ui_hitch_events })
$uiStallValues = @($rows | ForEach-Object { [double]$_.ui_stall_events })
$canvasSlowValues = @($rows | ForEach-Object { $_.canvas_slow_pct })

Write-Host ""
Write-Host "=== Playback Probe Summary ==="
Write-Host ("Windows: {0}" -f $rows.Count)
Write-Host ("fps_canvas avg={0:N1} p50={1:N1} p95={2:N1} min={3:N1}" -f `
    (Get-Mean $fpsCanvasValues),
    (Get-Percentile $fpsCanvasValues 0.50),
    (Get-Percentile $fpsCanvasValues 0.95),
    (($fpsCanvasValues | Measure-Object -Minimum).Minimum))
Write-Host ("fps_tick   avg={0:N1} p50={1:N1} p95={2:N1} min={3:N1}" -f `
    (Get-Mean $fpsTickValues),
    (Get-Percentile $fpsTickValues 0.50),
    (Get-Percentile $fpsTickValues 0.95),
    (($fpsTickValues | Measure-Object -Minimum).Minimum))
Write-Host ("jitter_p95 avg={0:N2}ms p50={1:N2}ms p95={2:N2}ms max={3:N2}ms" -f `
    (Get-Mean $jitterValues),
    (Get-Percentile $jitterValues 0.50),
    (Get-Percentile $jitterValues 0.95),
    (($jitterValues | Measure-Object -Maximum).Maximum))
Write-Host ("pacing_std avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
    (Get-Mean $pacingStdValues),
    (Get-Percentile $pacingStdValues 0.95),
    (($pacingStdValues | Measure-Object -Maximum).Maximum))
Write-Host ("pacing_jerk_p95 avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
    (Get-Mean $pacingJerkValues),
    (Get-Percentile $pacingJerkValues 0.95),
    (($pacingJerkValues | Measure-Object -Maximum).Maximum))
Write-Host ("step_jerk_p95 avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
    (Get-Mean $stepJerkValues),
    (Get-Percentile $stepJerkValues 0.95),
    (($stepJerkValues | Measure-Object -Maximum).Maximum))
Write-Host ("ui_gap_p95 avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
    (Get-Mean $uiGapValues),
    (Get-Percentile $uiGapValues 0.95),
    (($uiGapValues | Measure-Object -Maximum).Maximum))
Write-Host ("jank_events avg={0:N1} p95={1:N1} max={2:N0}" -f `
    (Get-Mean $jankValues),
    (Get-Percentile $jankValues 0.95),
    (($jankValues | Measure-Object -Maximum).Maximum))
Write-Host ("step_jank_events avg={0:N1} p95={1:N1} max={2:N0}" -f `
    (Get-Mean $stepJankValues),
    (Get-Percentile $stepJankValues 0.95),
    (($stepJankValues | Measure-Object -Maximum).Maximum))
if (($markValues | Measure-Object -Maximum).Maximum -gt 0) {
    Write-Host ("manual_jerk_marks avg={0:N1} p95={1:N1} max={2:N0}" -f `
        (Get-Mean $markValues),
        (Get-Percentile $markValues 0.95),
        (($markValues | Measure-Object -Maximum).Maximum))
}
Write-Host ("ui_hitch_events avg={0:N1} p95={1:N1} max={2:N0}" -f `
    (Get-Mean $uiHitchValues),
    (Get-Percentile $uiHitchValues 0.95),
    (($uiHitchValues | Measure-Object -Maximum).Maximum))
Write-Host ("ui_stall_events avg={0:N1} p95={1:N1} max={2:N0}" -f `
    (Get-Mean $uiStallValues),
    (Get-Percentile $uiStallValues 0.95),
    (($uiStallValues | Measure-Object -Maximum).Maximum))
Write-Host ("canvas_slow_pct avg={0:N1}% p95={1:N1}% max={2:N1}%" -f `
    (Get-Mean $canvasSlowValues),
    (Get-Percentile $canvasSlowValues 0.95),
    (($canvasSlowValues | Measure-Object -Maximum).Maximum))

Write-Host ""
Write-Host "=== Worst Windows (fps_canvas asc) ==="
$rows | Sort-Object fps_canvas, jitter_p95_ms | Select-Object -First 5 | ForEach-Object {
    Write-Host ("[{0}] fps_canvas={1:N1} fps_tick={2:N1} jitter_p95={3:N2}ms canvas_slow={4:N1}% top={5}" -f `
        $_.time, $_.fps_canvas, $_.fps_tick, $_.jitter_p95_ms, $_.canvas_slow_pct, $_.top)
}

Write-Host ""
Write-Host "=== Hotspot Frequency (Top Buckets) ==="
$hotKeyCount.GetEnumerator() |
    Sort-Object Value -Descending |
    Select-Object -First 10 |
    ForEach-Object {
        Write-Host ("{0} => {1}" -f $_.Key, $_.Value)
    }

if ($counterSums.Count -gt 0) {
    Write-Host ""
    Write-Host "=== Counter Totals ==="
    $counterSums.GetEnumerator() |
        Sort-Object Name |
        ForEach-Object {
            Write-Host ("{0} = {1}" -f $_.Key, $_.Value)
        }
}

if ($markContexts.Count -gt 0) {
    $markPulseMaxValues = @($markContexts | ForEach-Object { $_.pulse_max_ms })
    $markStepMaxValues = @($markContexts | ForEach-Object { $_.step_max_ms })
    $markUiGapMaxValues = @($markContexts | ForEach-Object { $_.ui_gap_max_ms })
    $markCanvasMaxValues = @($markContexts | ForEach-Object { $_.canvas_max_ms })
    $markVisualScrollStepMaxValues = @($markContexts | ForEach-Object { $_.visual_scroll_step_max_px })
    $markVisualScrollJerkMaxValues = @($markContexts | ForEach-Object { $_.visual_scroll_jerk_max_px })
    $markVisualDriftMaxValues = @($markContexts | ForEach-Object { $_.visual_playhead_drift_max_px })

    Write-Host ""
    Write-Host "=== Manual Mark Contexts ==="
    Write-Host ("Marks: {0}" -f $markContexts.Count)
    Write-Host ("pulse_max avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
        (Get-Mean $markPulseMaxValues),
        (Get-Percentile $markPulseMaxValues 0.95),
        (($markPulseMaxValues | Measure-Object -Maximum).Maximum))
    Write-Host ("step_max  avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
        (Get-Mean $markStepMaxValues),
        (Get-Percentile $markStepMaxValues 0.95),
        (($markStepMaxValues | Measure-Object -Maximum).Maximum))
    Write-Host ("ui_gap_max avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
        (Get-Mean $markUiGapMaxValues),
        (Get-Percentile $markUiGapMaxValues 0.95),
        (($markUiGapMaxValues | Measure-Object -Maximum).Maximum))
    Write-Host ("canvas_max avg={0:N2}ms p95={1:N2}ms max={2:N2}ms" -f `
        (Get-Mean $markCanvasMaxValues),
        (Get-Percentile $markCanvasMaxValues 0.95),
        (($markCanvasMaxValues | Measure-Object -Maximum).Maximum))
    Write-Host ("visual_scroll_step_max avg={0:N2}px p95={1:N2}px max={2:N2}px" -f `
        (Get-Mean $markVisualScrollStepMaxValues),
        (Get-Percentile $markVisualScrollStepMaxValues 0.95),
        (($markVisualScrollStepMaxValues | Measure-Object -Maximum).Maximum))
    Write-Host ("visual_scroll_jerk_max avg={0:N2}px p95={1:N2}px max={2:N2}px" -f `
        (Get-Mean $markVisualScrollJerkMaxValues),
        (Get-Percentile $markVisualScrollJerkMaxValues 0.95),
        (($markVisualScrollJerkMaxValues | Measure-Object -Maximum).Maximum))
    Write-Host ("visual_playhead_drift_max avg={0:N2}px p95={1:N2}px max={2:N2}px" -f `
        (Get-Mean $markVisualDriftMaxValues),
        (Get-Percentile $markVisualDriftMaxValues 0.95),
        (($markVisualDriftMaxValues | Measure-Object -Maximum).Maximum))

    Write-Host ""
    Write-Host "Top manual marks by step_max/ui_gap_max:"
    $markContexts |
        Sort-Object @{Expression = "visual_scroll_jerk_max_px"; Descending = $true}, @{Expression = "step_max_ms"; Descending = $true} |
        Select-Object -First 8 |
        ForEach-Object {
            Write-Host ("[{0}] mark_id={1} visual_scroll_jerk_max={2:N2}px visual_scroll_step_max={3:N2}px step_max={4:N2}ms ui_gap_max={5:N2}ms canvas_max={6:N2}ms" -f `
                $_.time, $_.mark_id, $_.visual_scroll_jerk_max_px, $_.visual_scroll_step_max_px, $_.step_max_ms, $_.ui_gap_max_ms, $_.canvas_max_ms)
        }
}

if ($OutCsv) {
    $csvPath = $OutCsv
    $dir = Split-Path -Parent $csvPath
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
    $rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
    Write-Host ""
    Write-Host "CSV exported: $csvPath"
}
