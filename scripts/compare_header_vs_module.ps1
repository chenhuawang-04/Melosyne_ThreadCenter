param(
  [string]$BuildDir = "build/cmake-cppm-check",
  [uint32]$Workers = 0,
  [uint32]$Warmup = 20,
  [uint32]$Measure = 80,
  [uint32]$Repeats = 3,
  [uint32]$OuterRounds = 4,
  [ValidateSet("ABBA", "AB", "BA")]
  [string]$OrderMode = "ABBA",
  [uint32]$CoolDownMs = 150,
  [string[]]$Scenario = @(),
  [switch]$SkipTests,
  [switch]$SkipBench
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function New-DirectoryIfMissing {
  param([string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) {
    New-Item -ItemType Directory -Path $Path | Out-Null
  }
}

function Invoke-External {
  param(
    [string]$ExePath,
    [string[]]$Arguments,
    [string]$LogPath
  )

  Write-Host ">> $ExePath $($Arguments -join ' ')"
  & $ExePath @Arguments 2>&1 | Tee-Object -FilePath $LogPath
  if ($LASTEXITCODE -ne 0) {
    throw "Command failed with exit code ${LASTEXITCODE}: $ExePath"
  }
}

function Get-ScenarioMap {
  param([object]$JsonObject)
  $map = @{}
  foreach ($entry in $JsonObject.results) {
    $map[[string]$entry.scenario] = $entry
  }
  return $map
}

function Get-ExpandedScenarios {
  param([string[]]$ScenarioInput)
  $expanded = New-Object System.Collections.Generic.List[string]
  foreach ($item in $ScenarioInput) {
    if ([string]::IsNullOrWhiteSpace($item)) {
      continue
    }
    foreach ($piece in $item.Split(",")) {
      $value = $piece.Trim()
      if (-not [string]::IsNullOrWhiteSpace($value)) {
        $expanded.Add($value)
      }
    }
  }
  return $expanded
}

function Get-RoundOrder {
  param(
    [string]$OrderMode_,
    [uint32]$RoundIndex_
  )

  switch ($OrderMode_) {
    "ABBA" {
      $slot = $RoundIndex_ % 4
      if ($slot -eq 0 -or $slot -eq 3) { return @("header", "module") }
      return @("module", "header")
    }
    "AB" { return @("header", "module") }
    "BA" { return @("module", "header") }
    default { throw "Unsupported OrderMode: $OrderMode_" }
  }
}

function Get-PercentDelta {
  param(
    [double]$Base,
    [double]$Value
  )
  if ([math]::Abs($Base) -lt 1e-9) {
    return 0.0
  }
  return (($Value - $Base) / $Base) * 100.0
}

function Get-Stats {
  param([double[]]$Values)

  if ($Values.Length -eq 0) {
    return [pscustomobject]@{
      count = 0
      mean = 0.0
      median = 0.0
      min = 0.0
      max = 0.0
      p95 = 0.0
      stddev = 0.0
      cv_percent = 0.0
    }
  }

  $sorted = @($Values | Sort-Object)
  $count = $sorted.Length
  $sum = 0.0
  foreach ($v in $sorted) { $sum += $v }
  $mean = $sum / $count
  $median = if (($count % 2) -eq 0) {
    ($sorted[$count / 2 - 1] + $sorted[$count / 2]) * 0.5
  } else {
    $sorted[[int]($count / 2)]
  }
  $p95Index = [Math]::Min($count - 1, [int][Math]::Floor($count * 0.95))
  $variance = 0.0
  foreach ($v in $sorted) {
    $delta = $v - $mean
    $variance += $delta * $delta
  }
  $variance /= $count
  $stddev = [Math]::Sqrt($variance)
  $cv = if ([Math]::Abs($mean) -lt 1e-9) { 0.0 } else { ($stddev / $mean) * 100.0 }

  return [pscustomobject]@{
    count = $count
    mean = $mean
    median = $median
    min = $sorted[0]
    max = $sorted[$count - 1]
    p95 = $sorted[$p95Index]
    stddev = $stddev
    cv_percent = $cv
  }
}

function Get-SignificanceTag {
  param([double]$AbsDeltaPercent)
  if ($AbsDeltaPercent -lt 3.0) { return "NOISE(<3%)" }
  if ($AbsDeltaPercent -lt 8.0) { return "SMALL(3-8%)" }
  if ($AbsDeltaPercent -lt 15.0) { return "MEDIUM(8-15%)" }
  return "LARGE(>=15%)"
}

function Get-StabilityTag {
  param([double]$CvPercent)
  if ($CvPercent -lt 5.0) { return "STABLE" }
  if ($CvPercent -lt 12.0) { return "MODERATE" }
  return "NOISY"
}

function Get-ExeHash {
  param([string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) { return "" }
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-MachineInfo {
  $cpu = $null
  $os = $null
  try {
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
  }
  catch {}
  try {
    $os = Get-CimInstance Win32_OperatingSystem | Select-Object -First 1
  }
  catch {}

  $logicalCores = if ($cpu -ne $null) { [int]$cpu.NumberOfLogicalProcessors } else { [int]$env:NUMBER_OF_PROCESSORS }
  $physicalCores = if ($cpu -ne $null) { [int]$cpu.NumberOfCores } else { 0 }
  $cpuName = if ($cpu -ne $null) { [string]$cpu.Name } else { "unknown" }
  $memoryGb = if ($os -ne $null) {
    [Math]::Round(([double]$os.TotalVisibleMemorySize / 1024.0 / 1024.0), 2)
  }
  else {
    0.0
  }
  $osCaption = if ($os -ne $null) { [string]$os.Caption } else { [System.Environment]::OSVersion.ToString() }
  $osVersion = if ($os -ne $null) { [string]$os.Version } else { "" }

  return [pscustomobject]@{
    machine_name = $env:COMPUTERNAME
    cpu_name = $cpuName
    logical_cores = $logicalCores
    physical_cores = $physicalCores
    total_memory_gb = $memoryGb
    os_caption = $osCaption
    os_version = $osVersion
    powershell_version = $PSVersionTable.PSVersion.ToString()
  }
}

$resolvedBuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outputDir = Join-Path $resolvedBuildDir "compare_report_$timestamp"
New-DirectoryIfMissing -Path $outputDir

$headerTestExe = Join-Path $resolvedBuildDir "thread_center_tests.exe"
$moduleTestExe = Join-Path $resolvedBuildDir "thread_center_tests_module.exe"
$headerBenchExe = Join-Path $resolvedBuildDir "thread_center_vs_taskflow_bench.exe"
$moduleBenchExe = Join-Path $resolvedBuildDir "thread_center_vs_taskflow_bench_module.exe"

$expandedScenarios = Get-ExpandedScenarios -ScenarioInput $Scenario

$metadata = [pscustomobject]@{
  generated_at = (Get-Date).ToString("o")
  machine = Get-MachineInfo
  build_dir = $resolvedBuildDir
  config = [pscustomobject]@{
    workers = $Workers
    warmup = $Warmup
    measure = $Measure
    repeats = $Repeats
    outer_rounds = $OuterRounds
    order_mode = $OrderMode
    cooldown_ms = $CoolDownMs
    scenarios = @($expandedScenarios)
    skip_tests = [bool]$SkipTests
    skip_bench = [bool]$SkipBench
  }
  binaries = [pscustomobject]@{
    thread_center_tests = $headerTestExe
    thread_center_tests_module = $moduleTestExe
    bench_header = $headerBenchExe
    bench_module = $moduleBenchExe
    bench_header_sha256 = Get-ExeHash -Path $headerBenchExe
    bench_module_sha256 = Get-ExeHash -Path $moduleBenchExe
  }
}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDir "metadata.json") -Encoding UTF8

if (-not $SkipTests) {
  if (-not (Test-Path -LiteralPath $headerTestExe)) {
    throw "Missing executable: $headerTestExe"
  }
  if (-not (Test-Path -LiteralPath $moduleTestExe)) {
    throw "Missing executable: $moduleTestExe"
  }

  Invoke-External -ExePath $headerTestExe -Arguments @() -LogPath (Join-Path $outputDir "tests_header.log")
  Invoke-External -ExePath $moduleTestExe -Arguments @() -LogPath (Join-Path $outputDir "tests_module.log")
}

if ($SkipBench) {
  Write-Host "Skip benchmark comparison (-SkipBench)."
  Write-Host "Report directory: $outputDir"
  exit 0
}

if (-not (Test-Path -LiteralPath $headerBenchExe)) {
  throw "Missing executable: $headerBenchExe"
}
if (-not (Test-Path -LiteralPath $moduleBenchExe)) {
  throw "Missing executable: $moduleBenchExe"
}
if ($OuterRounds -lt 1) {
  throw "OuterRounds must be >= 1."
}

$allRoundRows = New-Object System.Collections.Generic.List[object]
$allRunRows = New-Object System.Collections.Generic.List[object]

$commonBenchArgs = @("--warmup", "$Warmup", "--measure", "$Measure", "--repeats", "$Repeats")
if ($Workers -gt 0) {
  $commonBenchArgs += @("--workers", "$Workers")
}
foreach ($scenarioName in $expandedScenarios) {
  $commonBenchArgs += @("--scenario", $scenarioName)
}

for ($roundIndex = 0; $roundIndex -lt $OuterRounds; ++$roundIndex) {
  $roundOrder = Get-RoundOrder -OrderMode_ $OrderMode -RoundIndex_ $roundIndex
  Write-Host ""
  Write-Host "===== Outer round $($roundIndex + 1)/$OuterRounds order=$($roundOrder -join '->') ====="

  foreach ($implName in $roundOrder) {
    $exePath = if ($implName -eq "header") { $headerBenchExe } else { $moduleBenchExe }
    $csvPath = Join-Path $outputDir ("bench_{0}_round{1:D2}.csv" -f $implName, ($roundIndex + 1))
    $jsonPath = Join-Path $outputDir ("bench_{0}_round{1:D2}.json" -f $implName, ($roundIndex + 1))
    $logPath = Join-Path $outputDir ("bench_{0}_round{1:D2}.log" -f $implName, ($roundIndex + 1))

    $args = @($commonBenchArgs + @("--csv", $csvPath, "--json", $jsonPath))
    Invoke-External -ExePath $exePath -Arguments $args -LogPath $logPath

    $runObj = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    $allRunRows.Add([pscustomobject]@{
        round = $roundIndex + 1
        impl = $implName
        json_path = $jsonPath
        csv_path = $csvPath
        workers = [int]$runObj.config.workers
        warmup_iterations = [int]$runObj.config.warmup_iterations
        measure_iterations = [int]$runObj.config.measure_iterations
        repeats = [int]$runObj.config.repeats
      }) | Out-Null

    foreach ($entry in $runObj.results) {
      $allRoundRows.Add([pscustomobject]@{
          round = $roundIndex + 1
          impl = $implName
          scenario = [string]$entry.scenario
          samples = [int]$entry.samples
          tc_mean_us = [double]$entry.thread_center.mean_us
          tc_median_us = [double]$entry.thread_center.median_us
          tc_p95_us = [double]$entry.thread_center.p95_us
          tc_stddev_us = [double]$entry.thread_center.stddev_us
          tf_mean_us = [double]$entry.taskflow.mean_us
          tf_median_us = [double]$entry.taskflow.median_us
          tf_p95_us = [double]$entry.taskflow.p95_us
          tf_stddev_us = [double]$entry.taskflow.stddev_us
          ratio_tc_over_tf = [double]$entry.ratio_thread_center_over_taskflow
        }) | Out-Null
    }

    if ($CoolDownMs -gt 0) {
      Start-Sleep -Milliseconds $CoolDownMs
    }
  }
}

$roundCsvPath = Join-Path $outputDir "compare_round_details.csv"
$allRoundRows | Export-Csv -LiteralPath $roundCsvPath -NoTypeInformation -Encoding UTF8
$allRoundRows | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDir "compare_round_details.json") -Encoding UTF8
$allRunRows | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDir "run_manifest.json") -Encoding UTF8

$scenarioNames = @($allRoundRows | Select-Object -ExpandProperty scenario -Unique | Sort-Object)
$summaryRows = New-Object System.Collections.Generic.List[object]

foreach ($scenarioName in $scenarioNames) {
  $headerRows = @($allRoundRows | Where-Object { $_.scenario -eq $scenarioName -and $_.impl -eq "header" })
  $moduleRows = @($allRoundRows | Where-Object { $_.scenario -eq $scenarioName -and $_.impl -eq "module" })
  if ($headerRows.Count -eq 0 -or $moduleRows.Count -eq 0) {
    continue
  }

  $headerTcStats = Get-Stats -Values ([double[]]($headerRows | ForEach-Object { $_.tc_mean_us }))
  $moduleTcStats = Get-Stats -Values ([double[]]($moduleRows | ForEach-Object { $_.tc_mean_us }))
  $headerTfStats = Get-Stats -Values ([double[]]($headerRows | ForEach-Object { $_.tf_mean_us }))
  $moduleTfStats = Get-Stats -Values ([double[]]($moduleRows | ForEach-Object { $_.tf_mean_us }))
  $headerRatioStats = Get-Stats -Values ([double[]]($headerRows | ForEach-Object { $_.ratio_tc_over_tf }))
  $moduleRatioStats = Get-Stats -Values ([double[]]($moduleRows | ForEach-Object { $_.ratio_tc_over_tf }))

  $tcDelta = Get-PercentDelta -Base $headerTcStats.mean -Value $moduleTcStats.mean
  $ratioDelta = Get-PercentDelta -Base $headerRatioStats.mean -Value $moduleRatioStats.mean

  $tfDrift = Get-PercentDelta -Base (($headerTfStats.mean + $moduleTfStats.mean) * 0.5) -Value $moduleTfStats.mean
  $tfDriftAbs = [Math]::Abs($tfDrift)
  $stabilityTag = "{0}/{1}" -f `
    (Get-StabilityTag -CvPercent $headerTcStats.cv_percent), `
    (Get-StabilityTag -CvPercent $moduleTcStats.cv_percent)

  $summaryRows.Add([pscustomobject]@{
      scenario = $scenarioName
      rounds = [int]([Math]::Min($headerRows.Count, $moduleRows.Count))
      header_tc_mean_us = [math]::Round($headerTcStats.mean, 3)
      module_tc_mean_us = [math]::Round($moduleTcStats.mean, 3)
      tc_delta_percent = [math]::Round($tcDelta, 3)
      tc_significance = Get-SignificanceTag -AbsDeltaPercent ([Math]::Abs($tcDelta))
      header_tc_cv_percent = [math]::Round($headerTcStats.cv_percent, 3)
      module_tc_cv_percent = [math]::Round($moduleTcStats.cv_percent, 3)
      stability = $stabilityTag
      header_ratio_mean = [math]::Round($headerRatioStats.mean, 4)
      module_ratio_mean = [math]::Round($moduleRatioStats.mean, 4)
      ratio_delta_percent = [math]::Round($ratioDelta, 3)
      ratio_significance = Get-SignificanceTag -AbsDeltaPercent ([Math]::Abs($ratioDelta))
      taskflow_baseline_drift_percent = [math]::Round($tfDriftAbs, 3)
      taskflow_baseline_note = if ($tfDriftAbs -gt 5.0) { "CHECK_ENV(>5%)" } else { "OK" }
    }) | Out-Null
}

$summaryCsvPath = Join-Path $outputDir "compare_summary.csv"
$summaryJsonPath = Join-Path $outputDir "compare_summary.json"
$summaryRows | Export-Csv -LiteralPath $summaryCsvPath -NoTypeInformation -Encoding UTF8
$summaryRows | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

$report = [pscustomobject]@{
  metadata_path = (Join-Path $outputDir "metadata.json")
  summary_path = $summaryJsonPath
  round_details_path = (Join-Path $outputDir "compare_round_details.json")
  generated_at = (Get-Date).ToString("o")
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputDir "report_manifest.json") -Encoding UTF8

Write-Host ""
Write-Host "===== Header vs Module (Rigorous Aggregate) ====="
$summaryRows |
  Sort-Object @{ Expression = { [Math]::Abs($_.tc_delta_percent) }; Descending = $true } |
  Format-Table -AutoSize scenario,
  rounds,
  header_tc_mean_us,
  module_tc_mean_us,
  tc_delta_percent,
  tc_significance,
  header_ratio_mean,
  module_ratio_mean,
  ratio_delta_percent,
  ratio_significance,
  taskflow_baseline_drift_percent,
  taskflow_baseline_note,
  stability

Write-Host ""
Write-Host "Report directory: $outputDir"
Write-Host "  - metadata.json"
Write-Host "  - run_manifest.json"
Write-Host "  - compare_round_details.json/csv"
Write-Host "  - compare_summary.json/csv"
Write-Host "  - report_manifest.json"
Write-Host "  - bench_{header|module}_roundXX.json/csv/log"
Write-Host "  - tests_header.log/tests_module.log (unless -SkipTests)"
