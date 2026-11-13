# SwoleLoader: road to a production release

This is the handoff for whoever picks this project up next, human or agent. It covers:

- what the app is;
- how to build and test it;
- what is done and what is still unverified;
- the remaining work, in priority order.

Items marked **Owner** need a decision or action from the repository owner (AlanConstantino). Don't do those unprompted.

Last updated 2026-11-13. At that point:
- the app is at version 0.1.6;
- it builds, signs and passes every host test;
- nothing added since about 0.1.5 has run on a Switch.

## 1. What this is

SwoleLoader is a Nintendo Switch homebrew app (`.nro`). It installs many NSP/NSZ/XCI/XCZ files from a USB drive in one batch.

It is a fork of [Sphaira](https://github.com/ITotalJustice/sphaira). The app was earlier named NXLoader-GPT, and the old working copy at `~/Code/nxloader-gpt` is retired, so don't edit it. `~/Code/nxloader` is an unrelated, smaller app.

**License:** GPL-3.0, inherited from Sphaira. The source must stay public once the app is distributed, and Sphaira must be credited.

### Layout

| Path | What it holds |
|---|---|
| `app.cmake` | App name, author, version, install folder, `.nro` name, update repo. Renames happen here only. |
| `cmake/app_config.cmake` | Checks and normalizes `app.cmake`. |
| `sphaira/` | The app's sources. The directory keeps its upstream name. |
| `sphaira/include/nxloader/`, `sphaira/source/nxloader/` | Most of this fork's own code: the batch installer, review, sizing, the update and signature check, battery and progress logic. Header-only files are host-testable. |
| `sphaira/source/app.cpp` | Settings and Advanced Options UI, startup, the main loop, battery and screen helpers. |
| `sphaira/source/ui/progress_box.cpp` | Progress UI: the X list view, battery display, auto screen-off. |
| `sphaira/source/yati/yati.cpp` | The installer core: compressed-only CPU boost, the placeholder record, cleanup of interrupted installs. |
| `sphaira/source/log.cpp`, `sphaira/source/crash_handler.cpp` | Logging and crash reports. |
| `sphaira/source/ui/menus/main_menu.cpp` | The over-the-air updater. |
| `tests/nxloader/` | Host tests. `run.sh` runs all of them. |
| `tools/nxloader-build.sh`, `tools/nxloader-builder/` | The pinned container build. |
| `tools/symbolize.sh` | Turns crash addresses into source lines. |
| `tools/update-keygen.sh` | Creates the update signing key pair. |
| `build_release.sh` | Builds the Release preset and writes the signed release to `dist/`. |
| `.github/workflows/ci.yml` | Builds with the pinned container and runs the host tests on pull requests that change code, and by hand. Commits pushed straight to `main` don't start it. The toolchain image, compiler output and mbedtls are cached between runs. |
| `.github/workflows/release.yml` | Builds a draft GitHub release when a `v*` tag is pushed. |
| `README.md`, `RELEASING.md` | User docs and the release process. Keep both current. |
| `UPSTREAM.md` | A copy of Sphaira's README. To be replaced, see P3. |

Internal names still say `nxloader`: code namespaces, folders, the build script, the `NXLOADER_*` environment variables and `~/.config/nxloader-update`. This is deliberate, since users never see them. Renaming them is optional churn.

## 2. Build, test, debug

Building needs Podman or Docker. Everything else runs on macOS or Linux.

```sh
./tools/nxloader-build.sh Dev       # build/Dev/swoleloader.nro
./build_release.sh                  # dist/: .nro, .nro.sig, install ZIP, symbols ZIP
MBEDTLS_DIR=/path/to/mbedtls-2.28.10 sh tests/nxloader/run.sh
tools/symbolize.sh <symbols .elf> crash.txt
```

- **First build:** it creates the builder image and fetches dependencies, so it needs network access and takes several minutes. Later builds use ccache.
- **Signature test:** the update-signature test needs mbedtls 2.x. Unpack the 2.28.10 source, run `make -C library`, and point `MBEDTLS_DIR` at the source tree. Without it, that one test is skipped and the rest still run.
- **Symbolizing:** `addr2line` can't read this build's line tables. `symbolize.sh` uses `llvm-symbolizer` or gdb instead.
- **Known warnings:** the linker warns that the size of `__nx_exception_stack` changed. That's expected, because the crash handler replaces libnx's 1 KiB stack with 32 KiB. The `-Xassembler` LTO warning is also harmless.

**Conventions:**
- Match the surrounding Sphaira style.
- Builds use no exceptions.
- Put logic that can be tested in a header under `include/nxloader/`, with a test in `tests/nxloader/` hooked into `run.sh`.
- New Sphaira error codes are appended to the end of the enum in `defines.hpp`, with a name in `error_box.cpp`.
- A new setting needs a `LoadFrom` line in `app.cpp`.
- `ProgressBox::EnableBatchView` and `EnableBatteryGuard` must be called before the box is pushed. The worker thread may only use the thread-safe calls.

On the console:
- settings, logs and reports go in `/config/swoleloader/`;
- the app installs to `/switch/swoleloader/swoleloader.nro`;
- **Advanced Options → View logs** shows the logs.

## 3. What exists today

| Feature | Status |
|---|---|
| Multi-folder USB selection, review screen, batch install, stop-on-error or continue-after-errors | Used on hardware up to 0.1.5 |
| Install sizes and space checks, `last-batch.txt` report | Used up to 0.1.5. The report is now saved after every package, which is untested. |
| X toggles the progress view between the current game and the whole list | Untested on hardware |
| Signed over-the-air updates from GitHub Releases | Untested end to end; the signature check is host-tested |
| Logging (batched, rotates to `previous.txt`) and crash reports (`crash.txt`, backtrace) | Untested on hardware; the symbolizer is verified on the host |
| Battery guard: warns below 30%, stops between games at 15%, cancels at 5% | Untested on hardware |
| Auto screen-off after a minute idle in handheld mode, with no redrawing while off | Untested on hardware |
| CPU boost only while unpacking NSZ/XCZ | Untested; the speed effect hasn't been measured |
| Cleanup at launch of data left by an install that was cut off | Untested on hardware |
| Rename from NXLoader-GPT, with one-time settings copy from `/config/nxloader/config.ini` | Untested on hardware |

### Secrets and accounts

- **Signing key:** `~/.config/nxloader-update/signing-key.pem` on the owner's Mac, mode 600, never committed. Its public half, `assets/embed/update_public_key.der`, is built into the app.
  - The `UPDATE_SIGNING_KEY` repo secret was set on 2026-11-13.
  - Never regenerate or replace the key. Every installed copy would then refuse updates and need a manual reinstall. See RELEASING.md.
  - **Owner:** confirm the key is backed up, for example in a password manager.
- **Signature is the only update protection:** the app's curl turns TLS certificate checks off, so don't weaken the check.
- **Private repo:** the app checks for updates without logging in, so it gets a 404 and reports no releases until the repo is public. **Owner:** make it public before relying on updates.

## 4. Remaining work

### P0: needed before anyone else installs it

1. **Hardware test pass.** The owner runs this; an agent can prepare builds and read logs. Tag a pre-release such as 0.2.0 and go through:
   - **Formats:** NSP, NSZ, XCI and XCZ, covering base games, updates and DLC.
   - **Drives:** FAT32, exFAT and NTFS, on a flash drive and an SSD.
   - **Setups:** sysMMC and emuMMC (file-based and partition-based), installing to microSD and to system memory.
   - **Batches:** about 20 packages. Try both error modes, cancel partway, and use the X list view.
   - **Edge cases:** run out of space, unplug the drive partway, use the battery guard at low charge, let the screen turn off and wake it with a button.
   - **Power loss:** hold the power button partway through an install. Relaunch, check the "Removed … left by an interrupted install" notification, then reinstall.
   - **Updates:** publish the next version and update to it from inside the app.
   - **Crash reports:** make a crash, then check that `crash.txt` symbolizes.
   - **After the P1 trim:** open every remaining menu, use each file operation once (copy, move, rename, delete, zip, unzip, hash), check the sound effects and the theme, change the USB drive options under **Y → USB drives**, and check that a Sphaira install on the same card keeps its settings. Create a forwarder for a small `.nro`, launch it from the HOME screen, then delete it from System Settings.

   Record the results, and file anything that fails as a fix task.
2. **Clean up CI.** Done 2026-11-13.
   - The inherited `python-usb-export.yml`, `python-usb-install.yml` and `webusb-build.yml` are deleted, and P1 removed the PC-side USB scripts they tested.
   - `ci.yml` replaces `build_presets.yml`. It builds with `tools/nxloader-build.sh` and runs `tests/nxloader/run.sh` on Ubuntu with GCC and a checksum-verified mbedtls 2.28.10, so the signature test runs. It first ran on PR #1, where it passed.
3. **Prove the release workflow.** Raise `APP_VERSION`, push a matching `v` tag, and check the draft release:
   - it has exactly four files;
   - the `.sig` matches the `.nro`;
   - publishing it makes it visible to the app's update check once the repo is public.
4. **Stop sharing folders with Sphaira.** Done 2026-11-13.
   - Every `/config/sphaira/…` and `/switch/sphaira/cache/…` path now uses `APP_CONFIG_DIR` or `APP_CACHE_DIR` (`/config/swoleloader/cache`). Most of those features were then removed in P1.
   - Startup no longer deletes Sphaira's themezer cache and old etag file.
   - Nothing is migrated from `/config/sphaira/`.
   - `/atmosphere/.sphaira_msp_*` is still used for MSP staging. Each install uses a fresh name and removes it afterward, so it doesn't clash with Sphaira.

### P1: trim to what SwoleLoader is

Done 2026-11-13. The owner's scope: SwoleLoader installs packages and updates itself, nothing else. Untested on hardware; see the last item of P0.1.

- **Removed:** the appstore, themezer, the GitHub downloader, Ownfoil, the album and its web server, FTP and MTP, NXlink, network and other virtual mounts (HTTP, FTP, SFTP, SMB, NFS, WebDAV, game, save, system partitions, archives), USB DVD, the web browser, the IR camera, the game, save, storage and gamecard menus, dumping and NSZ export, the PC USB and network install streams and their PC scripts in `tools/`, launching homebrew and emulators from the file browser, the background install queue, theme selection and custom themes, background music and the music player, and the `Lite` preset.
- **Kept:** the file browser with its file operations (copy, move, rename, delete, zip, unzip, hash, edit as text, image viewer), USB drives (FAT32, exFAT, NTFS), the batch installer, MSP installs, the over-the-air updater, language selection, logs, crash reports, the battery guard and screen-off, and the UI sound effects.
- **Forwarders, restored after the trim:** **X → Install Forwarder** on a `.nro` on the microSD card, with the forwarder editor, its SteamGridDB icon search and file picker, and the `hbl/` loader the forwarder embeds. This is how the fork had it before P1, except that launching emulator cores through the loader stayed out with the rest of the emulator support. The owner also asked about PC installs and dumping, but before P1 nothing in the fork could open those screens, so they were left out rather than wired up untested.
- **Changed:**
  - **A** on a package file opens the install review instead of browsing inside the file.
  - USB drive options moved from **Y → Network → HDD** to **Y → USB drives**.
  - `build_release.sh` now clears the build's romfs copy first, because the build never deletes files from it.
- **Branding:**
  - Error codes show as `AppError_*`, and the error box links to this repository's issues instead of Sphaira's.
  - Translation entries that no code can look up were pruned, about 850 per language, including every one that mentioned Sphaira.
- **Size:** the Release `.nro` went from 4.76 MB to 2.5 MB with forwarders back.
- **Left as is:** the installer core (`yati.cpp`) apart from two calls into removed menus; the unused stream-install path inside it; `Result_*` codes of removed features in `defines.hpp`, kept so the remaining codes keep their numbers; and the inherited screenshots in `assets/screenshots/`, which belong to the P3 rewrite of `UPSTREAM.md`.

### P2: gaps and improvements

- **Split packages.** FAT32 can't hold files over 4 GB, so large XCI and NSP files on FAT32 drives come split into `game.xc0`, `game.xc1`, … or a `game.xci`/`game.nsp` folder of `00`, `01`, … parts. Neither form is recognized yet. Support them in the scanner (`package_scan.hpp`), the size reader and the install source, with host tests.
- **Measure the compressed-only boost.** Install the same plain NSP with **Boost only for compressed games** on and off, and compare the times in `last-batch.txt`. Change the default if boosting is clearly faster.
- **Power cut in the registering window.** In the second or two after content is registered and before the application record is pushed, a power cut can leave a game without its icon. Reinstalling fixes it. Consider detecting it, or document it.

### P3: docs, legal, release

- **README for users.** Rewrite it around install steps, controls, troubleshooting and where the logs are. Move developer detail to a separate document.
- **Upstream credit.** Replace `UPSTREAM.md` with a short credit to Sphaira and its author, plus a dated summary of what this fork changed. GPL-3.0 section 5(a) asks for prominent notice of modifications. Delete `assets/screenshots/` with it, since those show Sphaira features.
- **Bug-report template.** Add `.github/ISSUE_TEMPLATE/bug.yml` asking for:
  - CFW and firmware versions, and sysMMC or emuMMC;
  - drive type and format;
  - `log.txt`, `previous.txt`, `crash.txt` and `last-batch.txt`.
- **Changelog.** Keep a `CHANGELOG.md`, and use it for the release notes the app shows when it offers an update.
- **Takedown risk.** Never ship or link keys, firmware or games, and keep the docs neutral.
  - The icon draws on Joy-Con designs. **Owner** accepts or changes that.
  - Keep "Nintendo" and "Switch" out of the app name.
- **Version 1.0.0** once the P0 hardware pass is clean and P1 is done.

## 5. Gotchas

- `~/Code/nxloader-gpt` is the pre-rename copy of this project, and `~/Code/nxloader` is a different app. Work only in this repo.
- The release workflow fails if the tag doesn't equal `v` plus `APP_VERSION`.
- Draft and pre-release GitHub releases are invisible to the in-app updater.
- Each release must contain exactly one `.nro` and its `.sig`. Rebuilding the `.nro` means signing it again (`build_release.sh` does this).
- The updater takes the release's only `.nro` when the name differs. That way older installs keep updating after a rename.
- With the screen off in handheld mode, the main loop skips drawing and sleeps 50 ms per frame. Input still wakes it. Docked, it always draws.
- `interrupted-install.txt` lists the placeholders of the install in progress. It is removed when an install ends normally, so finding it at launch means an install was cut off.
- `App::GetBattery()` opens the `psm` service the first time it's used. `psmExit()` runs at app exit.
