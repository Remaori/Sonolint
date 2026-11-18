# Puts report.md on the pull request as one comment that later runs edit, found by the
# hidden marker on its first line. Never fails the job: a token that can't comment (a
# pull request from a fork gets a read-only one) only produces a warning.
param(
    [Parameter(Mandatory)] [string] $Report,
    [Parameter(Mandatory)] [string] $Repository,
    [Parameter(Mandatory)] [string] $PullRequest
)

$marker = '<!-- sonolint:report -->'

function Warn ([string] $text)
{
    Write-Output "::warning::Sonolint couldn't comment on the pull request: $text"
    exit 0
}

if (-not (Test-Path $Report)) { Warn "there is no report.md" }

# --slurp puts every page in one array of arrays, so no filter has to be quoted for jq.
$output = gh api --paginate --slurp "repos/$Repository/issues/$PullRequest/comments" 2>&1
if ($LASTEXITCODE -ne 0) { Warn ($output -join ' ') }

$pages = (@($output | Where-Object { $_ -is [string] }) -join "`n") | ConvertFrom-Json
$earlier = $pages | ForEach-Object { $_ } | Where-Object { $_.body -and $_.body.StartsWith($marker) } | Select-Object -Last 1

if ($earlier) {
    $answer = gh api --method PATCH "repos/$Repository/issues/comments/$($earlier.id)" -F "body=@$Report" 2>&1
    $done = 'updated'
}
else {
    $answer = gh api --method POST "repos/$Repository/issues/$PullRequest/comments" -F "body=@$Report" 2>&1
    $done = 'posted'
}

if ($LASTEXITCODE -ne 0) { Warn ($answer -join ' ') }
Write-Output "Sonolint report $done on pull request #$PullRequest"
exit 0
