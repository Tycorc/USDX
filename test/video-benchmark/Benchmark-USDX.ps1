<#
.SYNOPSIS
Measures USDX video playback cost: the same song in FFmpeg, MPV software and MPV hardware mode.
.DESCRIPTION
Starts the game with a fixed config, sings the benchmark song through the menus by keystroke,
samples CPU, GPU engines and memory for -Seconds of the song, then closes the game.
Each folder under -SongRoot holds one copy of the same song with a different video; -Videos
selects folders by name and defaults to all of them.
Results go to results\<machine>-<time>\results.csv beside this script.
.EXAMPLE
.\Benchmark-USDX.ps1 -GameDirectory C:\USDX -Videos 1080p-h264 -Modes ffmpeg,mpv-auto
#>
param(
  [string]$GameDirectory,
  [string]$SongRoot = (Join-Path $PSScriptRoot 'songs'),
  [string[]]$Videos,
  [string[]]$Modes = @('ffmpeg','mpv-no','mpv-auto'),
  [int]$Seconds = 60,
  [int]$SampleSeconds = 5,
  [int]$Repeat = 1
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# powershell -File (the .cmd launcher) passes "a,b" as one string.
$Videos = @($Videos -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (-not $Videos) {
  $Videos = @(Get-ChildItem -LiteralPath $SongRoot -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'song.txt') } | Sort-Object Name | ForEach-Object { $_.Name })
}
if (-not $Videos) { throw "No song folders with song.txt under $SongRoot" }
$Modes = @($Modes -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$badMode = $Modes | Where-Object { $_ -notin 'ffmpeg', 'mpv-no', 'mpv-auto' }
if ($badMode) { throw "Unknown mode: $($badMode -join ', '). Use ffmpeg, mpv-no, mpv-auto." }

if (-not $GameDirectory) { $GameDirectory = Join-Path $PSScriptRoot 'game' }
if (-not $GameDirectory -or -not (Test-Path -LiteralPath (Join-Path $GameDirectory 'ultrastardx.exe'))) {
  throw 'ultrastardx.exe not found. Put the game in .\game or pass -GameDirectory.'
}
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UsdxBench {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
}
'@

function Send-Key([Diagnostics.Process]$Target, [int]$Key) {
  $scan = [UsdxBench]::MapVirtualKey($Key, 0)
  [void][UsdxBench]::PostMessage($Target.MainWindowHandle, 0x100, [IntPtr]$Key, [IntPtr](1 -bor ($scan -shl 16)))
  Start-Sleep -Milliseconds 100
  [void][UsdxBench]::PostMessage($Target.MainWindowHandle, 0x101, [IntPtr]$Key, [IntPtr](3221225473 -bor ($scan -shl 16)))
}

# The game holds its logs open, so read them shared.
function Read-Shared([string]$File) {
  if (-not (Test-Path -LiteralPath $File)) { return '' }
  $stream = [IO.File]::Open($File, 'Open', 'Read', 'ReadWrite')
  try { (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
}

function Get-EngineLoad([int]$ProcessId) {
  $load = @{}
  Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -Filter "Name LIKE 'pid_$($ProcessId)[_]%'" -OperationTimeoutSec 5 |
    ForEach-Object {
      $type = ($_.Name -split 'engtype_')[-1]
      $load[$type] = [double]($load[$type]) + [double]$_.UtilizationPercentage
    }
  $load
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$resultDir = Join-Path $PSScriptRoot "results\$env:COMPUTERNAME-$stamp"
New-Item -ItemType Directory -Path $resultDir | Out-Null
$battery = @(Get-CimInstance Win32_Battery)
$system = [ordered]@{
  machine = $env:COMPUTERNAME
  started = (Get-Date).ToString('o')
  os = (Get-CimInstance Win32_OperatingSystem | ForEach-Object { "$($_.Caption) $($_.Version)" })
  cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
  logical_cores = [Environment]::ProcessorCount
  gpus = @(Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name), driver $($_.DriverVersion)" })
  on_battery = [bool]($battery | Where-Object { $_.BatteryStatus -eq 1 })
  power_plan = ((powercfg /getactivescheme) -replace '^.*\(|\)\s*$', '')
  game = $GameDirectory
  seconds = $Seconds
}
$system | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $resultDir 'system.json') -Encoding UTF8
Write-Host "System: $($system.cpu) | $($system.gpus -join ' | ') | battery: $($system.on_battery)"
if ($system.on_battery) { Write-Warning 'Running on battery. Plug in for comparable numbers.' }

$scratch = Join-Path $env:TEMP "USDX-bench-$stamp"
Write-Host "Copying game to $scratch"
Copy-Item -LiteralPath $GameDirectory -Destination $scratch -Recurse

$rows = @()
try {
  foreach ($round in 1..$Repeat) {
    foreach ($video in $Videos) {
      $songDir = Join-Path $SongRoot $video
      if (-not (Test-Path -LiteralPath (Join-Path $songDir 'song.txt'))) { throw "Missing benchmark song: $songDir" }
      foreach ($mode in $Modes) {
        $backend, $decoder = if ($mode -eq 'ffmpeg') { 'ffmpeg', 'no' } else { 'mpv', ($mode -split '-')[1] }
        $runName = "$video-$mode-$round"
        $runDir = Join-Path $resultDir $runName
        New-Item -ItemType Directory -Path $runDir | Out-Null
        Get-ChildItem -LiteralPath $scratch -Filter '*.log' | Remove-Item
        @"
[Graphics]
VideoBackend=$backend
VideoHardwareDecoding=$decoder
FullScreen=Off
Resolution=1280x720
Screens=1
MaxFramerate=60
[Themes]
Theme=Modern
Skin=Blue
[Sound]
BackgroundMusic=Off
[Game]
Language=English
"@ | Set-Content -LiteralPath (Join-Path $scratch 'config.ini') -Encoding ASCII

        Write-Host "[$runName] starting"
        # SDL shows the window without activating it, so typing elsewhere cannot reach the game.
        $env:SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN = '1'
        $stdout = Join-Path $runDir 'stdout.log'
        $arguments = @('-debug', '-window', '-screens', '1', '-configfile', 'config.ini', '-songpath', ('"' + $songDir + '"'))
        $process = Start-Process -FilePath (Join-Path $scratch 'ultrastardx.exe') -ArgumentList $arguments -WorkingDirectory $scratch `
          -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $runDir 'stderr.log') -PassThru
        $row = [ordered]@{video = $video; mode = $mode; round = $round; decoder_used = ''; seconds = 0; cpu_seconds = 0
          cpu_one_core_pct = 0; cpu_all_cores_pct = 0; process_cpu_seconds = 0; gpu_3d_pct = 0; gpu_decode_pct = 0
          peak_working_set_mb = 0; song_pos_at_start_s = $null; gpu_samples_missing = 0; valid = $false; note = ''}
        try {
          # Keep a Windows process handle until post-exit CPU accounting is read.
          # Without it, TotalProcessorTime can disappear after the game closes.
          $null = $process.Handle
          $errorLog = Join-Path $scratch 'Error.log'
          $wait = [Diagnostics.Stopwatch]::StartNew()
          do {
            Start-Sleep -Milliseconds 500
            $process.Refresh()
            if ($process.HasExited) { throw 'game exited during startup' }
          } until (((Read-Shared $errorLog).Contains('Finish [Initialize3D]') -and $process.MainWindowHandle -ne [IntPtr]::Zero) -or $wait.Elapsed.TotalSeconds -gt 180)
          if ($wait.Elapsed.TotalSeconds -gt 180) { throw 'game did not finish starting within 180 s' }
          Start-Sleep -Seconds 3
          # Title, main menu Sing, song list, missing-microphone notice (fresh config assigns none).
          foreach ($delay in 2, 5, 3, 3) { Send-Key $process 13; Start-Sleep -Seconds $delay }

          $cpuStart = $process.TotalProcessorTime.TotalSeconds
          $windowStart = Get-Date
          $clock = [Diagnostics.Stopwatch]::StartNew()
          $engineSamples = @(); $peak = 0
          while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
            Start-Sleep -Seconds $SampleSeconds
            $process.Refresh()
            if ($process.HasExited) { throw 'game exited during the song' }
            $peak = [Math]::Max($peak, $process.WorkingSet64)
            try { $engineSamples += ,(Get-EngineLoad $process.Id) } catch { $row.gpu_samples_missing++ }
          }
          $row.seconds = [Math]::Round($clock.Elapsed.TotalSeconds, 1)
          $row.cpu_seconds = [Math]::Round($process.TotalProcessorTime.TotalSeconds - $cpuStart, 2)
          $row.cpu_one_core_pct = [Math]::Round(100 * $row.cpu_seconds / $row.seconds, 1)
          $row.cpu_all_cores_pct = [Math]::Round($row.cpu_one_core_pct / $system.logical_cores, 1)
          $row.peak_working_set_mb = [Math]::Round($peak / 1MB)
          if ($engineSamples.Count) {
            $row.gpu_3d_pct = [Math]::Round(($engineSamples | ForEach-Object { [double]$_['3D'] } | Measure-Object -Average).Average, 1)
            $row.gpu_decode_pct = [Math]::Round(($engineSamples | ForEach-Object { [double]$_['VideoDecode'] } | Measure-Object -Average).Average, 1)
          }
          $log = Read-Shared $errorLog
          [void]$process.CloseMainWindow()
          if (-not $process.WaitForExit(15000)) { $process.Kill(); [void]$process.WaitForExit(5000) }
          $row.process_cpu_seconds = [Math]::Round($process.TotalProcessorTime.TotalSeconds, 2)

          $log += Read-Shared $stdout
          $mpvDecoders = @([regex]::Matches($log, 'Hardware decoder: ([\w-]+)') | ForEach-Object { $_.Groups[1].Value })
          $ffmpegCodecs = @([regex]::Matches($log, 'Using (\S+) codec \[TVideoPlayback_ffmpeg.Open\]') | ForEach-Object { $_.Groups[1].Value })
          $row.decoder_used = if ($backend -eq 'mpv' -and $mpvDecoders.Count) { "mpv:$($mpvDecoders[-1])" }
            elseif ($ffmpegCodecs.Count) { "ffmpeg:$($ffmpegCodecs[-1])" } else { 'none' }
          $audioOpens = @([regex]::Matches($log, '(?m)^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) INFO:\s+Using decoder \S+ for "[^"]*audio\.ogg"') |
            ForEach-Object { [datetime]::ParseExact($_.Groups[1].Value, 'yyyy-MM-dd HH:mm:ss.fff', $null) } | Where-Object { $_ -le $windowStart })
          if ($audioOpens.Count) { $row.song_pos_at_start_s = [Math]::Round(($windowStart - $audioOpens[-1]).TotalSeconds, 1) }
          $problems = @()
          if ($null -eq $row.song_pos_at_start_s -or $row.song_pos_at_start_s -gt 30) { $problems += 'window did not start inside the song' }
          if (-not $log.Contains('audio.ogg')) { $problems += 'song audio not opened' }
          if ($backend -eq 'mpv' -and -not $mpvDecoders.Count) {
            $problems += if ($log.Contains('Using fallback FFmpeg_Video')) { 'MPV failed, game fell back to FFmpeg' } else { 'MPV did not play the video' }
          }
          if ($backend -eq 'ffmpeg' -and -not $ffmpegCodecs.Count) { $problems += 'FFmpeg did not play the video' }
          if ($log.Contains('TScreenSingController.Finish')) { $problems += 'song ended inside the window' }
          if ($problems) { $row.note = $problems -join '; ' } else { $row.valid = $true }
        } catch {
          $row.note = $_.Exception.Message
        } finally {
          try {
            $process.Refresh()
            if (-not $process.HasExited) { $process.Kill(); [void]$process.WaitForExit(5000) }
            Get-ChildItem -LiteralPath $scratch -Filter '*.log' | Copy-Item -Destination $runDir
          } finally {
            $process.Dispose()
          }
        }
        Write-Host ("[$runName] {0} cpu {1}% of one core, gpu 3d {2}% decode {3}%, valid {4} {5}" -f `
          $row.decoder_used, $row.cpu_one_core_pct, $row.gpu_3d_pct, $row.gpu_decode_pct, $row.valid, $row.note)
        $rows += [pscustomobject]$row
        $rows | Export-Csv -LiteralPath (Join-Path $resultDir 'results.csv') -NoTypeInformation -Encoding UTF8
      }
    }
  }
} finally {
  Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
$rows | Format-Table video, mode, decoder_used, cpu_one_core_pct, cpu_all_cores_pct, gpu_3d_pct, gpu_decode_pct, peak_working_set_mb, song_pos_at_start_s, valid -AutoSize
Write-Host "Results: $resultDir"
