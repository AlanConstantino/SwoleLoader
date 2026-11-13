# Releases and over-the-air updates

## How updates work

1. At launch, and from **Y → Check for updates**, the app asks GitHub for the newest published release of the repository in `APP_UPDATE_REPO` ([app.cmake](app.cmake)). Drafts and pre-releases are ignored. The launch check can be turned off in **Advanced Options → Check for updates on launch**.
2. If the release tag is a newer version than the running app, the menu shows **Update available**, with the release notes as the changelog. Nothing is installed until the user picks **Update Now**.
3. The app downloads the release's `.nro` and its `.sig` signature. It installs the file only when:
   - the signature matches the public key built into the app, and
   - the version inside the `.nro` is the version in the release tag.
4. The app backs up the running `.nro` and replaces it, wherever it is installed. It then reads the new file back and restores the backup on any mismatch. Finally it restarts.

The signature is the protection that matters. The app's HTTPS connections do not check certificates, so a download that someone on the network has tampered with is caught only by the signature check.

## One-time setup, once the repository exists

1. Create the GitHub repository. It must be public, because the app checks for releases without logging in. Push this project to it.
2. Put the repository link in `app.cmake`. These forms all work: `https://github.com/owner/repo`, `github.com/owner/repo`, `owner/repo`.
   ```cmake
   set(APP_UPDATE_REPO "https://github.com/owner/repo")
   ```
3. Give the release workflow the signing key, as a secret named `UPDATE_SIGNING_KEY`:
   ```sh
   gh secret set UPDATE_SIGNING_KEY < ~/.config/nxloader-update/signing-key.pem
   ```
   Alternatively, paste the file's contents under **Settings → Secrets and variables → Actions** on GitHub.
4. Build that version and install it by hand once. Copies built before the link was set do not know where to look, so they cannot update themselves.

## Publishing a version

1. Raise `APP_VERSION` in `app.cmake`, for example to `0.1.7`. Commit and push.
2. Tag the commit with the same version and push the tag:
   ```sh
   git tag v0.1.7
   git push origin v0.1.7
   ```
3. The [release workflow](.github/workflows/release.yml) checks that the tag matches `APP_VERSION`. It then builds and signs the app and creates a **draft** release with four files:
   - `swoleloader.nro`
   - `swoleloader.nro.sig`
   - `swoleloader-0.1.7.zip`
   - `swoleloader-0.1.7-symbols.zip`, which turns that version's crash reports into source lines (see the README)
4. Edit the draft's notes. They are what the console shows as the changelog. Then publish the draft. Consoles see the update at their next check.

The notes can carry several languages. Add `## [en]`, `## [fr]` and so on as section headings, and the console shows the section for its language, falling back to English.

To release without the workflow, run `./build_release.sh` locally. It signs with the local key. Then upload the four files from `dist/` to a GitHub release whose tag is `v` plus `APP_VERSION`.

Each release must contain exactly one `.nro` and its `.sig`. The `.sig` has to be built from that exact `.nro`, so rebuilding a file means signing it again.

## The signing key

- **Private key:** `~/.config/nxloader-update/signing-key.pem`, plus the `UPDATE_SIGNING_KEY` secret. Never commit it, and keep a backup, for example in a password manager.
- **Public key:** [assets/embed/update_public_key.der](assets/embed/update_public_key.der). It is committed and built into the app.

`tools/update-keygen.sh` created the pair. If the public key file goes missing, run the script again to rewrite it from the private key; the script never replaces an existing private key.

Changing the key is a reset. Releases signed with a new key are refused by every copy built with the old one, so each of those installs has to be updated by hand once. Do this only if the key is lost or leaked: move the old key aside, run the script, commit the new public key, and update the secret.

## Renaming the app

Everything is in `app.cmake`.

| Setting | What happens when it changes |
|---|---|
| `APP_NAME`, `APP_AUTHOR` | Shown in the Homebrew Menu and the app. Change freely. |
| `APP_VERSION` | Must increase for each release. Tags must match it. |
| `APP_NRO` | The release file name. Existing installs still update: when no file has the name they expect, they take the release's only `.nro`. |
| `APP_FOLDER` | Used by new installs and the ZIP. Existing installs keep updating at their current location. |
| `APP_UPDATE_REPO` | GitHub redirects renamed repositories, so installed copies follow a rename. If you move to a different repository, publish one last release in the old repository that was built with the new link. |

The icon is [assets/icon.jpg](assets/icon.jpg). None of these changes affect the signing key.
