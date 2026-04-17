param(
  [string]$BuildDir = "build/cmake-cppm-check",
  [uint32]$Workers = 0,
  [uint32]$Warmup = 20,
  [uint32]$Measure = 80,
  [uint32]$Repeats = 3,
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
    $map[$entry.scenario] = $entry
  }
  return $map
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

$resolvedBuildDir = Resolve-Path -LiteralPath $BuildDir
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outputDir = Join-Path $resolvedBuildDir "compare_report_$timestamp"
New-DirectoryIfMissing -Path $outputDir

$headerTestExe = Join-Path $resolvedBuildDir "thread_center_tests.exe"
$moduleTestExe = Join-Path $resolvedBuildDir "thread_center_tests_module.exe"
$headerBenchExe = Join-Path $resolvedBuildDir "thread_center_vs_taskflow_bench.exe"
$moduleBenchExe = Join-Path $resolvedBuildDir "thread_center_vs_taskflow_bench_module.exe"

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

if (-not $SkipBench) {
  if (-not (Test-Path -LiteralPath $headerBenchExe)) {
    throw "Missing executable: $headerBenchExe"
  }
  if (-not (Test-Path -LiteralPath $moduleBenchExe)) {
    throw "Missing executable: $moduleBenchExe"
  }

  $headerCsv = Join-Path $outputDir "bench_header.csv"
  $headerJson = Join-Path $outputDir "bench_header.json"
  $moduleCsv = Join-Path $outputDir "bench_module.csv"
  $moduleJson = Join-Path $outputDir "bench_module.json"

  $benchArgs = @("--warmup", "$Warmup", "--measure", "$Measure", "--repeats", "$Repeats")
  if ($Workers -gt 0) {
    $benchArgs += @("--workers", "$Workers")
  }
  foreach ($scenarioName in $Scenario) {
    $benchArgs += @("--scenario", $scenarioName)
  }

  $headerArgs = @($benchArgs + @("--csv", $headerCsv, "--json", $headerJson))
  $moduleArgs = @($benchArgs + @("--csv", $moduleCsv, "--json", $moduleJson))

  Invoke-External -ExePath $headerBenchExe `
    -Arguments $headerArgs `
    -LogPath (Join-Path $outputDir "bench_header.log")
  Invoke-External -ExePath $moduleBenchExe `
    -Arguments $moduleArgs `
    -LogPath (Join-Path $outputDir "bench_module.log")

  $headerObj = Get-Content -LiteralPath $headerJson -Raw | ConvertFrom-Json
  $moduleObj = Get-Content -LiteralPath $moduleJson -Raw | ConvertFrom-Json

  $headerMap = Get-ScenarioMap -JsonObject $headerObj
  $moduleMap = Get-ScenarioMap -JsonObject $moduleObj

  $scenarioNames = New-Object System.Collections.Generic.List[string]
  foreach ($key in $headerMap.Keys) {
    $scenarioNames.Add([string]$key)
  }
  foreach ($key in $moduleMap.Keys) {
    if (-not $scenarioNames.Contains([string]$key)) {
      $scenarioNames.Add([string]$key)
    }
  }
  $scenarioNames.Sort()

  $summaryRows = @()
  foreach ($scenarioName in $scenarioNames) {
    if (-not $headerMap.ContainsKey($scenarioName)) { continue }
    if (-not $moduleMap.ContainsKey($scenarioName)) { continue }

    $headerEntry = $headerMap[$scenarioName]
    $moduleEntry = $moduleMap[$scenarioName]

    $headerTcMean = [double]$headerEntry.thread_center.mean_us
    $moduleTcMean = [double]$moduleEntry.thread_center.mean_us
    $headerTfMean = [double]$headerEntry.taskflow.mean_us
    $moduleTfMean = [double]$moduleEntry.taskflow.mean_us
    $headerRatio = [double]$headerEntry.ratio_thread_center_over_taskflow
    $moduleRatio = [double]$moduleEntry.ratio_thread_center_over_taskflow

    $summaryRows += [pscustomobject]@{
      scenario = $scenarioName
      header_tc_mean_us = [math]::Round($headerTcMean, 3)
      module_tc_mean_us = [math]::Round($moduleTcMean, 3)
      tc_delta_percent = [math]::Round((Get-PercentDelta -Base $headerTcMean -Value $moduleTcMean), 3)
      header_tf_mean_us = [math]::Round($headerTfMean, 3)
      module_tf_mean_us = [math]::Round($moduleTfMean, 3)
      tf_delta_percent = [math]::Round((Get-PercentDelta -Base $headerTfMean -Value $moduleTfMean), 3)
      header_ratio = [math]::Round($headerRatio, 4)
      module_ratio = [math]::Round($moduleRatio, 4)
      ratio_delta_percent = [math]::Round((Get-PercentDelta -Base $headerRatio -Value $moduleRatio), 3)
    }
  }

  $summaryJsonPath = Join-Path $outputDir "compare_summary.json"
  $summaryRows | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryJsonPath -Encoding UTF8

  $summaryCsvPath = Join-Path $outputDir "compare_summary.csv"
  $summaryRows | Export-Csv -LiteralPath $summaryCsvPath -NoTypeInformation -Encoding UTF8

  Write-Host ""
  Write-Host "===== Header vs Module Benchmark Delta ====="
  $summaryRows | Format-Table -AutoSize
  Write-Host ""
  Write-Host "Report directory: $outputDir"
  Write-Host "  - compare_summary.json"
  Write-Host "  - compare_summary.csv"
  Write-Host "  - bench_header.json/csv/log"
  Write-Host "  - bench_module.json/csv/log"
  Write-Host "  - tests_header.log/tests_module.log (unless -SkipTests)"
}
else {
  Write-Host "Skip benchmark comparison (-SkipBench)."
  Write-Host "Report directory: $outputDir"
}
