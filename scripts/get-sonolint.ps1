# Finds the sonolint executable for the Action: the one given in the `binary` input, or
# the release for this runner, downloaded with gh and checked against SHA256SUMS and
# against the attestation of the workflow that built it.
# Writes its path as the step output `exe`.
$ErrorActionPreference = 'Stop'

# To the host and not to the output, which an assignment would swallow.
function Fail ([string] $text) { Write-Host "::error::$text"; exit 1 }

# Advanced: an executable given directly, as Sonolint's own CI does with what it just built.
if ($env:INPUT_BINARY) {
    if (-not (Test-Path $env:INPUT_BINARY)) { Fail "binary: $env:INPUT_BINARY doesn't exist" }
    $exe = (Resolve-Path $env:INPUT_BINARY).Path
    if (-not $IsWindows) { chmod +x $exe }   # artifacts lose the executable bit
    Add-Content $env:GITHUB_OUTPUT "exe=$exe"
    exit 0
}

$platform = if ($env:RUNNER_OS -eq 'Windows' -and $env:RUNNER_ARCH -eq 'X64') { 'windows-x64' }
            elseif ($env:RUNNER_OS -eq 'macOS') { 'macos-universal' }
            else { Fail "Sonolint has binaries for Windows x64 and macOS only, not for $env:RUNNER_OS $env:RUNNER_ARCH" }

# The version: the input, or else the ref the Action is pinned to. In a composite action
# github.action_ref can be empty, and then the last folder of the Action's path is the ref.
$version = $env:INPUT_VERSION
if (-not $version) { $version = $env:ACTION_REF }
if (-not $version) { $version = Split-Path -Leaf $env:ACTION_PATH }
if ($version -notmatch '^v\d') { Fail "can't tell which Sonolint to download from '$version'; set the version input, such as v0.1.0" }

$repository = if ($env:ACTION_REPOSITORY) { $env:ACTION_REPOSITORY } else { 'Remaori/Sonolint' }

# A major tag (v0) means the newest published release of that major version.
if ($version -match '^v\d+$') {
    $tags = gh release list --repo $repository --exclude-drafts --exclude-pre-releases --limit 100 --json tagName --jq '.[].tagName'
    if ($LASTEXITCODE -ne 0) { Fail "couldn't list the releases of $repository" }
    $newest = @($tags) | Where-Object { $_ -like "$version.*" } | Select-Object -First 1
    if (-not $newest) { Fail "$repository has no published $version.x release yet" }
    $version = $newest
}

$folder = Join-Path $env:RUNNER_TEMP "sonolint-$version"
$zip = "sonolint-$version-$platform.zip"
New-Item -ItemType Directory -Force $folder | Out-Null

Write-Output "Downloading $zip from $repository"
gh release download $version --repo $repository --pattern $zip --pattern SHA256SUMS --dir $folder --clobber
if ($LASTEXITCODE -ne 0) { Fail "couldn't download $zip from the $version release of $repository" }

$line = Get-Content (Join-Path $folder 'SHA256SUMS') | Where-Object { ($_ -split '\s+')[-1].TrimStart('*') -eq $zip }
$expected = if ($line) { ($line -split '\s+')[0].ToLowerInvariant() } else { '' }
$actual = (Get-FileHash -Algorithm SHA256 (Join-Path $folder $zip)).Hash.ToLowerInvariant()
if (-not $expected) { Fail "SHA256SUMS of $version has no line for $zip" }
if ($expected -ne $actual) { Fail "$zip doesn't match its checksum: expected $expected, got $actual" }

# SHA256SUMS comes from the same release, so it can't tell a replaced zip. From v0.1.0 on
# every zip has an attestation that release.yml in this repository built it; the first
# pre-releases came before that and are the only ones let through without one. Any
# failure stops here, a network one too: nothing unproven is run.
$unattested = @('v0.1.0-rc.1', 'v0.1.0-rc.2', 'v0.1.0-rc.3')
if ($unattested -notcontains $version) {
    gh attestation verify (Join-Path $folder $zip) --repo $repository --signer-workflow "$repository/.github/workflows/release.yml"
    if ($LASTEXITCODE -ne 0) { Fail "couldn't verify that release.yml in $repository built $zip, so it wasn't run" }
    Write-Output "Verified that release.yml in $repository built $zip"
}

Expand-Archive -Path (Join-Path $folder $zip) -DestinationPath $folder -Force
$exe = Join-Path $folder ($IsWindows ? 'sonolint.exe' : 'sonolint')
if (-not $IsWindows) { chmod +x $exe }

Write-Output "Sonolint $version for $platform is ready"
Add-Content $env:GITHUB_OUTPUT "exe=$exe"
exit 0
