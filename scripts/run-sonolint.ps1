# Runs sonolint for the Action and writes the step outputs: code (its exit code, so the
# Action can upload and comment before failing), status, regressions, report-dir and id.
$ErrorActionPreference = 'Stop'

# Tells this run's artifact and comment apart from the other jobs of the workflow: by
# default the plugin's file name and the platform, so a matrix or two plugins don't collide.
$id = $env:INPUT_ID
if (-not $id) {
    $platform = if ($IsWindows) { 'windows-x64' } elseif ($env:RUNNER_ARCH -eq 'ARM64') { 'macos-arm64' } else { 'macos-x64' }
    $id = [IO.Path]::GetFileNameWithoutExtension($env:INPUT_PLUGIN.TrimEnd('/', '\')) + "-$platform"
}
$id = ($id.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
if (-not $id) { throw "the id needs letters or digits" }
Add-Content $env:GITHUB_OUTPUT "id=$id"

$out = Join-Path $env:GITHUB_WORKSPACE 'sonolint-out'
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Add-Content $env:GITHUB_OUTPUT "report-dir=$out"

if ($env:INPUT_MODE -eq 'baseline') {
    # Start from the committed file, so the other platforms' values are kept.
    $baseline = Join-Path $out 'baseline.json'
    if (Test-Path $env:INPUT_BASELINE) { Copy-Item $env:INPUT_BASELINE $baseline }

    & $env:SONOLINT_EXE baseline update --config $env:INPUT_CONFIG --plugin $env:INPUT_PLUGIN --baseline $baseline
    $code = $LASTEXITCODE

    Add-Content $env:GITHUB_STEP_SUMMARY ("## Sonolint baseline`n`nThe new baseline is baseline.json in the artifact of this run. " +
        "Download it and commit it as $env:INPUT_BASELINE.`n")
}
elseif ($env:INPUT_MODE -eq 'run') {
    $arguments = @('run', '--config', $env:INPUT_CONFIG, '--plugin', $env:INPUT_PLUGIN, '--baseline', $env:INPUT_BASELINE,
                   '--out', $out, '--fail-on', $env:INPUT_FAIL_ON, '--format', 'md,html,json,junit',   # the outputs come from results.json
                   '--artifact-url', "$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID",
                   '--commit', $env:COMMIT_SHA)

    # In a pull request, the checkout is its merge into the base branch: that is what gets measured.
    if ($env:BASE_REF)      { $arguments += @('--merged-into', $env:BASE_REF) }
    if ($env:ImageOS)       { $arguments += @('--environment', "runner_image_os=$env:ImageOS") }
    if ($env:ImageVersion)  { $arguments += @('--environment', "runner_image_version=$env:ImageVersion") }

    & $env:SONOLINT_EXE @arguments
    $code = $LASTEXITCODE

    $report = Join-Path $out 'report.md'
    if (Test-Path $report) { Get-Content -Raw $report | Add-Content $env:GITHUB_STEP_SUMMARY }

    $results = Join-Path $out 'results.json'
    if (Test-Path $results) {
        $summary = (Get-Content -Raw $results | ConvertFrom-Json).summary
        Add-Content $env:GITHUB_OUTPUT "regressions=$($summary.regressed + $summary.changed)"
    }
}
else {
    Write-Output "::error::mode must be run or baseline, not '$env:INPUT_MODE'"
    $code = 2
}

Add-Content $env:GITHUB_OUTPUT "code=$code"
Add-Content $env:GITHUB_OUTPUT "status=$(if ($code -eq 0) { 'pass' } else { 'fail' })"
exit 0   # the runner would otherwise fail this step with sonolint's exit code
