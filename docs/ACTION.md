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

## Inputs

| Input | Default | What it does |
| --- | --- | --- |
| `plugin` | required | Path to the `.vst3` |
| `config` | `sonolint.yml` | Path to the config |
| `baseline` | `sonolint/baseline.json` | Path to the baseline |
| `version` | the ref the Action is pinned to | The Sonolint release to download, such as `v0.1.0`. With `@v0`, the newest published `v0.x` release, pre-releases left out |
| `fail-on` | `regressions` | What fails the job: `regressions`, `assertions` or `none` ([CONFIG.md](CONFIG.md#running)) |
| `comment` | `true` | Comment the report on the pull request |
| `mode` | `run` | `run` measures and compares; `baseline` writes a new baseline (below) |
| `github-token` | `${{ github.token }}` | Downloads the release and posts the comment |
| `binary` | none | Advanced, see below |

## Outputs

| Output | What it holds |
| --- | --- |
| `status` | `pass` or `fail` |
| `regressions` | How many scenarios regressed or changed |
| `report-dir` | The folder with `results.json`, `report.md`, `report.html` and `junit.xml` |

## What it does

1. Downloads `sonolint-<version>-<platform>.zip` and `SHA256SUMS` from the release with
   `gh`, checks the zip against its checksum and unzips it.
2. Runs `sonolint run` with every report, the link to this run for the footer, the
   commit, and the image of the runner for `results.json`. In a pull request, GitHub
   checks out its merge into the base branch, so the footer says
   `Commit abc1234 (merged into main)`: the commit is the head of the pull request.
3. Adds `report.md` to the job summary.
4. Uploads the reports as the artifact `sonolint-report-<job>-<OS>`.
5. In a pull request, puts `report.md` in a comment, and edits that same comment on the
   next push instead of adding another. A token that can't comment, as in a pull request
   from a fork, only gives a warning.
6. Fails the job with Sonolint's exit code, if it isn't 0.

## Making the baseline

The baseline holds one section per platform, and its values must come from the same
kind of runner the comparison runs on: the same `runs-on`, since another machine, CPU
or image can move the last decimals. On a machine of your own you can run
`sonolint baseline update` directly; for a CI runner, use `mode: baseline` in a workflow
you start by hand:

```yaml
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
uploads the result as `baseline.json` in the artifact `sonolint-baseline-<job>-<OS>`.
Download it, check the diff and commit it. Run it again whenever a change of sound is
intended. Nothing is compared and nothing is commented in this mode.

## Advanced: `binary`

`binary` takes the path of a `sonolint` executable to use instead of a release. Nothing
is downloaded and no checksum is verified, so use it only with an executable your own
workflow built. Sonolint's CI uses it to test the Action of each pull request with the
binary that pull request just built.
