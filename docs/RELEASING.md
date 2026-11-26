# Releasing a new version of Sonolint

A checklist, in order. Nothing is public until you publish: a tag only creates a
**draft** release that nobody else can see.

## 1. Version and notes

- In `CMakeLists.txt`, set `project(Sonolint VERSION x.y.z ...)`.
- In [`CHANGELOG.md`](../CHANGELOG.md), move the notes under **Unreleased** to a new
  `## x.y.z` section and leave "Unreleased" with "Nothing yet.". That section becomes the
  text of the release, for pre-releases of that version too.
- Commit both and get them onto `main`.

## 2. Tag

For a pre-release to try first, add `-rc.1`, `-rc.2` and so on to the tag; the version
in `CMakeLists.txt` stays `x.y.z`. The tag goes on `origin/main`, whatever the branch
you are on.

```bash
git fetch origin &&
git tag vx.y.z origin/main &&
git push origin vx.y.z
```

Wait for the **Release** workflow in the Actions tab. It checks the tag against
`CMakeLists.txt` and the notes in `CHANGELOG.md`, builds and tests Sonolint on Windows
and macOS, and creates the draft with `sonolint-vx.y.z-windows-x64.zip`,
`sonolint-vx.y.z-macos-universal.zip` and `SHA256SUMS`. A tag with `-rc` makes a
pre-release. If something doesn't match, there is no draft and the log says why.

## 3. Check the draft

1. On GitHub, open **Releases** and then the draft.
2. Download the zip for your system, unzip it and run `sonolint version`: it shows the
   version and the commit of the tag. On macOS, run
   `xattr -d com.apple.quarantine sonolint` first.
3. Run it on a plugin you know, for example
   `sonolint list-params --plugin Heft.vst3`.
4. Check its attestation, which the Action also checks before running it:
   `gh attestation verify <the zip> --repo Remaori/Sonolint --signer-workflow Remaori/Sonolint/.github/workflows/release.yml`.

## 4. Publish, or start over

- **It works:** edit the draft and press **Publish release**. A pre-release stays marked
  as one.
- **It doesn't:** delete the draft (Releases → the draft → Delete) and the tag, fix the
  problem and go back to step 1.

  ```bash
  git push --delete origin vx.y.z
  git tag -d vx.y.z
  ```

## 5. Move the major tag (final releases only)

In the Actions tab, run **Move the major tag**. It points `v0` at the newest published
release, pre-releases left out, so workflows that use `Remaori/Sonolint@v0` get it. A
pre-release is used by naming it: `Remaori/Sonolint@vx.y.z-rc.1`.
