# The GitHub Action

`Remaori/Sonolint` runs Sonolint in a GitHub workflow: it downloads the release for the
runner, measures the plugin, comments the report on the pull request and fails the job
when something regressed. It runs on Windows x64 and macOS runners.

## Use it

Add a job after the one that builds your plugin:

```yaml
  sonolint:
    needs: build
    runs-on: windows-latest
    permissions:
      contents: read
      pull-requests: write     # to comment on the pull request
    steps:
      - uses: actions/checkout@v7
      - uses: actions/download-artifact@v8
        with:
          name: MyPlugin-VST3
          path: build/vst3
      - uses: Remaori/Sonolint@v0
        with:
          plugin: build/vst3/MyPlugin.vst3
```

Run it on `pull_request`. Don't use `pull_request_target`: it would run the code of a
fork with a token that can write to your repository.

To measure on both platforms, use a matrix (`runs-on: ${{ matrix.os }}` with
`os: [windows-latest, macos-latest]`), as long as your build makes the `.vst3` for both.
Each job keeps its own comment, and so does each plugin when a repository tests several.

## Inputs

| Input | Default | What it does |
| --- | --- | --- |
| `plugin` | required | Path to the `.vst3` |
| `config` | `sonolint.yml` | Path to the config |
| `baseline` | `sonolint/baseline.json` | Path to the baseline |
| `version` | the ref the Action is pinned to | The Sonolint release to download, such as `v0.1.0`. With `@v0`, the newest published `v0.x` release, pre-releases left out. Needed when you pin the Action by commit, which doesn't say a version |
| `fail-on` | `regressions` | What fails the job: `regressions`, `assertions` or `none` ([CONFIG.md](CONFIG.md#running)) |
| `comment` | `true` | Comment the report on the pull request |
| `mode` | `run` | `run` measures and compares; `baseline` writes a new baseline (below) |
| `github-token` | `${{ github.token }}` | Downloads the release and posts the comment |
| `id` | the plugin's file name and the platform, such as `heft-windows-x64` | Tells this run apart from the others in the workflow: it names the artifact and the comment. Set it when two jobs test the same plugin on the same platform |
| `binary` | none | Advanced, see below |

## Outputs

| Output | What it holds |
| --- | --- |
| `status` | `pass` or `fail` |
| `regressions` | How many scenarios regressed or changed |
| `report-dir` | The folder with `results.json`, `report.md`, `report.html` and `junit.xml` |

## What it does

1. Downloads `sonolint-<version>-<platform>.zip` and `SHA256SUMS` from the release with
   `gh`, checks the zip against its checksum and, with `gh attestation verify`, that
   `release.yml` in this repository built it. Without that proof nothing is run and the
   job fails, even if the reason is a network problem. Only the first pre-releases,
   `v0.1.0-rc.1` to `v0.1.0-rc.3`, came before attestations and are let through.
2. Runs `sonolint run` with every report, the link to this run for the footer, the
   commit, and the image of the runner for `results.json`. In a pull request, GitHub
   checks out its merge into the base branch, so the footer says
   `Commit abc1234 (merged into main)`: the commit is the head of the pull request.
3. Adds `report.md` to the job summary.
4. Uploads the reports as the artifact `sonolint-report-<id>`, such as `sonolint-report-heft-windows-x64`.
5. In a pull request, puts `report.md` in a comment, and edits that same comment on the
   next push instead of adding another. Each `id` has its own comment. A token that
   can't comment, as in a pull request from a fork, only gives a warning.
6. Fails the job with Sonolint's exit code, if it isn't 0.

If Sonolint doesn't finish (a wrong config, a plugin that can't be loaded, a crash or a
hang), the comment and the job summary say that instead of keeping an earlier result.

## Making the baseline

The baseline holds one section per platform, and its values must come from the same
kind of runner the comparison runs on: the same `runs-on`, since another machine, CPU
or image can move the last decimals. On a machine of your own you can run
`sonolint baseline update` directly; for a CI runner, use `mode: baseline` in a workflow
you start by hand:

```yaml
# .github/workflows/sonolint-baseline.yml
on:
  workflow_dispatch:

jobs:
  baseline:
    runs-on: windows-latest     # the same runner as the job that compares
    steps:
      - uses: actions/checkout@v7
      # ... build the plugin or download it, as in the comparing job
      - uses: Remaori/Sonolint@v0
        with:
          plugin: build/vst3/MyPlugin.vst3
          mode: baseline
```

It starts from the baseline in the repository, rewrites this platform's section and
uploads the result as `baseline.json` in the artifact `sonolint-baseline-<id>`.
Download it, check the diff and commit it. Nothing is compared and nothing is
commented in this mode.

## Accepting a change of sound

When a pull request changes the sound on purpose, Sonolint reports what moved and the
job fails. To accept the change, make a new baseline from the pull request's branch,
on the runner that compares, and commit it to that branch. In the Actions tab, run the
baseline workflow on the branch and download its artifact; or, with the
[GitHub CLI](https://cli.github.com/):

```bash
gh workflow run sonolint-baseline.yml --ref my-branch
```

A few seconds later, when the run has started, wait for it and take its file. The old
one goes first, because `gh` doesn't overwrite files:

```bash
run=$(gh run list --workflow sonolint-baseline.yml --branch my-branch --limit 1 --json databaseId --jq '.[0].databaseId') &&
gh run watch "$run" &&
rm sonolint/baseline.json &&
gh run download "$run" --name sonolint-baseline-myplugin-windows-x64 --dir sonolint &&
git diff sonolint/baseline.json
```

Commit `sonolint/baseline.json` and push. The job passes again, and the diff of the
baseline shows the reviewer which values changed and by how much. `gh workflow run`
needs the workflow on the default branch, but runs the version on `--ref`.

- **From a fork,** the branch isn't in your repository, so `--ref` can't name it. The
  contributor can run the workflow in their fork, once Actions are enabled there, and
  commit the file the same way. A maintainer can also push the branch to the
  repository and run it there, after reviewing the code, since it then runs with the
  repository's token.
- **On your own machine,** `sonolint baseline update` rewrites only the section of the
  platform it runs on (`macos-arm64` on an Apple Silicon Mac), so it can't accept a
  change that a Windows runner measured.

## Advanced: `binary`

`binary` takes the path of a `sonolint` executable to use instead of a release. Nothing
is downloaded and no checksum is verified, so use it only with an executable your own
workflow built. Sonolint's CI uses it to test the Action of each pull request with the
binary that pull request just built.
