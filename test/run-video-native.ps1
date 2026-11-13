# Run from the extracted Windows native video artifact. No installation needed.
# Supply the preserved runtime directory, containing libmpv-2.dll and licenses/libmpv.
# Example: powershell -NoProfile -File test/run-video-native.ps1 -MpvRuntimeDirectory C:\previous-test\game
[CmdletBinding()]
param(
  [ValidateSet('gl', 'gl2', 'gles2', 'gles3')][string[]]$Renderers = @('gl', 'gl2', 'gles2', 'gles3'),
  [string]$ExpectedCommit = '',
  [string]$OutputDirectory = '',
  [string]$MpvRuntimeDirectory = '',
  [ValidateScript({ $_ -eq 0 -or ($_ -ge 10 -and $_ -le 120) })][int]$PacingSeconds = 0,
  [switch]$SoftwareOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$bundle = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.schema -ne 1) { throw 'Unknown native bundle manifest version' }
if ($ExpectedCommit -and $manifest.source_sha -ne $ExpectedCommit) { throw 'Unexpected source commit' }
if (-not $MpvRuntimeDirectory) {
  throw 'Supply -MpvRuntimeDirectory with the preserved libmpv-2.dll and licenses/libmpv directory. The original rolling-release download is no longer available.'
}
foreach ($entry in $manifest.files.PSObject.Properties) {
  $path = [IO.Path]::GetFullPath((Join-Path $bundle $entry.Name))
  if (-not $path.StartsWith($bundle + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Manifest path leaves the bundle'
  }
  if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value) {
    throw "Bundle file changed: $($entry.Name)"
  }
}
$mpvSource = (Resolve-Path -LiteralPath $MpvRuntimeDirectory).Path
foreach ($entry in $manifest.external_libmpv.files.PSObject.Properties) {
  $path = [IO.Path]::GetFullPath((Join-Path $mpvSource $entry.Name))
  if (-not $path.StartsWith($mpvSource.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'External runtime path leaves its directory'
  }
  if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value) {
    throw "External libmpv runtime differs from the preserved input: $($entry.Name)"
  }
}
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $bundle ('results-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory' }
$output = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
Copy-Item -LiteralPath (Join-Path $bundle 'manifest.json') -Destination $output
$runtime = Join-Path $output 'runtime'
Copy-Item -LiteralPath (Join-Path $bundle 'runtime') -Destination $runtime -Recurse
$missingRuntime = Join-Path $output 'missing-runtime'
Copy-Item -LiteralPath (Join-Path $bundle 'runtime') -Destination $missingRuntime -Recurse
foreach ($name in @('libmpv-2.dll', 'mpv-2.dll')) {
  if (Test-Path -LiteralPath (Join-Path $missingRuntime $name)) { throw 'The bundle must not contain libmpv' }
}
foreach ($entry in $manifest.external_libmpv.files.PSObject.Properties) {
  $destination = Join-Path $runtime $entry.Name
  New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $mpvSource $entry.Name) -Destination $destination
  if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $entry.Value) {
    throw "Copied runtime input changed: $($entry.Name)"
  }
}
$result = [ordered]@{
  source_sha = $manifest.source_sha
  started_utc = [DateTime]::UtcNow.ToString('o')
  passed = $false
  hardware_required = -not [bool]$SoftwareOnly
  pacing_seconds = $PacingSeconds
  libmpv_source = Join-Path $mpvSource 'libmpv-2.dll'
  libmpv_sha256 = $manifest.external_libmpv.files.'libmpv-2.dll'
  libmpv_provenance = $manifest.external_libmpv
  renderers = $Renderers
  os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber
  gpus = @(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,Status)
  runs = @()
}
$python = Join-Path $bundle 'python/python.exe'
$runner = Join-Path $PSScriptRoot 'run-video-smoke.py'
$fixture = Join-Path $bundle 'fixtures/quadrants.mp4'
function Invoke-VideoTest([string]$Name, [string]$Executable, [string]$Decoder, [string[]]$SelectedRenderers, [bool]$RequireHardware, [int]$PacingDuration = 0, [string]$RuntimeDirectory = $runtime, [bool]$MissingMpv = $false) {
  $arguments = @('-B', $runner, '--prebuilt', (Join-Path $RuntimeDirectory $Executable),
                 '--fixture', $fixture, '--output-dir', (Join-Path $output $Name),
                 '--hwdec', $Decoder, '--renderers') + $SelectedRenderers
  if ($RequireHardware) { $arguments += '--require-hardware' }
  if ($PacingDuration) { $arguments += @('--pacing-seconds', [string]$PacingDuration) }
  if ($MissingMpv) { $arguments += '--expect-missing-mpv' }
  Write-Host "Running $Name for source $($manifest.source_sha)"
  # The shared Python runner owns semantic checks and hidden-console handling.
  $previousPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    & $python @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output ($Name + '.log'))
    $code = $LASTEXITCODE
  } finally { $ErrorActionPreference = $previousPreference }
  $result.runs += [ordered]@{name=$Name; exit_code=$code; hardware_required=$RequireHardware}
  if ($code -ne 0) { throw "Native video test failed: $Name (exit $code). See $output" }
}
try {
  & $python -B (Join-Path $bundle 'tools/check_windows_runtime.py') $runtime
  if ($LASTEXITCODE -ne 0) { throw 'Native runtime dependencies failed to load in isolation' }
  Invoke-VideoTest -Name 'missing-mpv' -Executable 'VideoSmoke.exe' -Decoder 'no' -SelectedRenderers @('gl') -RequireHardware $false -RuntimeDirectory $missingRuntime -MissingMpv $true
  Invoke-VideoTest 'without-mpv' 'VideoSmoke-without.exe' 'no' $Renderers $false
  Invoke-VideoTest 'mpv-software' 'VideoSmoke.exe' 'no' $Renderers $false
  if (-not $SoftwareOnly) {
    Invoke-VideoTest 'mpv-hardware' 'VideoSmoke.exe' 'auto' @('gl') $true $PacingSeconds
  } elseif ($PacingSeconds) {
    Invoke-VideoTest 'mpv-pacing' 'VideoSmoke.exe' 'no' @('gl') $false $PacingSeconds
  }
  $result.passed = $true
} finally {
  $result.finished_utc = [DateTime]::UtcNow.ToString('o')
  $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
}
Write-Host "PASS: native video tests. Results: $output"
