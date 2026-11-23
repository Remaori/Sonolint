# 0022. Details of the Action and the release

Status: accepted (2026-11-18)

Decisions taken while writing `action.yml`, its scripts and the release workflows.
`docs/ACTION.md` and `docs/RELEASING.md` describe the result; this records why.

- **The Action can run a given executable (`binary`).** The Action downloads a published
  release, so on its own it could never test the code of the pull request it runs in,
  and before the first release it couldn't run at all. Sonolint's CI builds the binary
  and the test plugin, keeps them as artifacts, and the self-test hands them to the
  Action. In that mode nothing is downloaded and no checksum is checked; the input is
  documented as advanced.
- **`mode: baseline` makes the baseline on a CI runner.** Values must come from the same
  kind of runner they are compared on, and someone developing on a Mac can't make the
  Windows section otherwise. The Action rewrites this platform's section of the
  committed file and puts the result in the artifact; a person downloads it and commits
  it, so Sonolint still never changes the baseline in the repository by itself.
- **The runner's image goes in through `--environment`.** The binary reads nothing that
  belongs to GitHub; the Action passes `ImageOS` and `ImageVersion` as
  `runner_image_os` and `runner_image_version`.
- **The Action always asks for `json` in `--format`:** its outputs come from
  `results.json`.
- **`regressions` counts scenarios that regressed or changed:** both fail the job.
- **An `id` tells each run apart: its artifact and its comment.** By default it is the
  plugin's file name and the platform (`heft-windows-x64`), so neither a matrix of
  runners nor two plugins in one repository collide; the artifact is
  `sonolint-report-<id>` or `sonolint-baseline-<id>`, since artifact names are unique
  within a workflow run, and the comment's marker carries it. The id comes from the
  plugin's path and the runner, before anything runs, so it exists even when Sonolint
  fails. Before 0.1.0 the artifact was named after the job and the OS, which repeated
  itself (`sonolint-baseline-sonolint-baseline-Windows`), and every job of a matrix
  edited the same comment, so the last one hid the others.
- **Any failure to comment is a warning,** not only a 403: the report is still in the
  job summary and the artifact, and the job's result is Sonolint's.
- **The comment to edit is found by the marker at the start of its body**
  (`<!-- sonolint:report <id> -->`, the binary's marker with the id added), whoever wrote
  it, so a token of a person works as well as the default one.
- **A pull request from a fork is tested with a read-only token** in a second self-test
  job: a fork gets exactly that, and no second account is needed.
- **Pre-releases are tags with a suffix** (`v0.1.0-rc.1`). The version in
  `CMakeLists.txt` stays `0.1.0`, the notes come from the `## 0.1.0` section, and the
  release is marked as a pre-release. The release workflow only reacts to `v*.*.*`, so
  moving `v0` doesn't start it. `@v0` resolves to the newest release that isn't a
  pre-release.
- **External actions are pinned by commit,** with the version in a comment, in the
  workflows and inside `action.yml`: a tag can be moved, a commit can't. Dependabot
  updates both. The examples in the documentation keep the tags, which are easier to read.
- **`actionlint` checks the workflows in CI,** downloaded at a fixed version and checksum,
  with the `shellcheck` of the runner. It also checks how the workflows use `action.yml`.
- **The macOS zip is made with `zip`,** which keeps the executable bit; PowerShell's
  `Compress-Archive` would drop it. The Action sets the bit again anyway, since
  artifacts lose it too.
