# Releasing a new version

1. Bump the version in `CMakeLists.txt`: `project(KettleLAN VERSION 0.2.0 …)`.
2. Add a `## [0.2.0] – date` section at the top of [`CHANGELOG.md`](../CHANGELOG.md) describing what changed
   (new features, fixes, known limitations). This text becomes the description of the GitHub release –
   preview it with `scripts/release-notes.sh 0.2.0`. The release workflow stops if the entry is missing.
3. Make sure the tests pass: `ctest --test-dir build --output-on-failure`.
4. Commit and create a tag with the same version:

   ```sh
   git commit -am "Release 0.2.0"
   git tag v0.2.0
   git push origin main v0.2.0
   ```

5. GitHub Actions (the **Release** workflow) builds the .deb packages (Debian 12, Ubuntu 24.04) and the
   AppImage, runs the tests, computes SHA256 checksums, adds a build attestation and publishes the release
   on the **Releases** page with the description from `CHANGELOG.md`. Progress is visible in the **Actions** tab.

## One-time setup

- A private repository `maxiuz94/steamworks-sdk` with a release `sdk-165` containing the official
  `steamworks_sdk_165.zip` from Valve.
- A `SDK_REPO_TOKEN` secret in the `kettlelan` repository: a fine-grained token with **read-only** access
  (`Contents: Read-only`) to the `steamworks-sdk` repository **only**.
- When updating the SDK, update `cmake/steamworks-sdk.sha256`, the file name in `release.yml` and the release
  tag in the `steamworks-sdk` repository.

## Test build without publishing

**Actions** → **Release** → **Run workflow**. The packages appear as artifacts of the run, without a release.

## Fixing the description of a published release

```sh
scripts/release-notes.sh 0.1.0 > /tmp/notes.md
gh release edit v0.1.0 --notes-file /tmp/notes.md
```

## Translations

After changing user-visible texts in the code, refresh the translation files and translate new entries:

```sh
cmake --build build --target update_translations
```

Then edit `translations/kettlelan_pl.ts` (Qt Linguist or a text editor). English is the source language;
`kettlelan_en.ts` only holds plural forms.
