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

   Record the results, and file anything that fails as a fix task.
2. **Clean up CI.** Pushing to `main` currently starts four inherited workflows.
   - Delete `python-usb-export.yml`, `python-usb-install.yml` and `webusb-build.yml`, which tries to deploy Sphaira's WebUSB site. **Owner** should confirm, unless the PC-side USB tools are kept.
   - `build_presets.yml` builds against unpinned latest libnx and newlib, while local and release builds use the pinned image. Switch it to `tools/nxloader-build.sh` with `CONTAINER_ENGINE=docker`, the way `release.yml` does.
   - Add a job that runs `tests/nxloader/run.sh` with cmake, openssl and mbedtls 2.28 available.
3. **Prove the release workflow.** Raise `APP_VERSION`, push a matching `v` tag, and check the draft release:
   - it has exactly four files;
   - the `.sig` matches the `.nro`;
   - publishing it makes it visible to the app's update check once the repo is public.
4. **Stop sharing folders with Sphaira.** Themes, i18n overrides, network mounts, core-launch files and the custom logo still use `/config/sphaira/…`, and a GitHub cache uses `/switch/sphaira/cache/…`. Find them with `grep -rn "/config/sphaira\|/switch/sphaira" sphaira/source sphaira/include`.
   - Move each one to `APP_CONFIG_DIR`, or drop the feature as part of P1.
   - A real Sphaira install on the same card must not be affected.

### P1: trim to what SwoleLoader is

Much of Sphaira is still compiled in and reachable. That adds size, network-facing code (curl doesn't verify TLS) and Sphaira branding. Candidates to remove or disable:

- appstore, themezer and the GitHub downloader (`appstore.cpp`, `themezer.cpp`, `ghdl.cpp`);
- Ownfoil (`ownfoil*.cpp`);
- the album and album-share web server (`album_*.cpp`, `album_server.cpp`);
- the FTP and MTP servers (`ENABLE_FTPSRV`, `ENABLE_LIBHAZE`);
- network mounts (the `ENABLE_DEVOPTAB_*` options);
- the web browser (`web.cpp`), the IR camera (`irs_menu.cpp`), saves (`save_menu.cpp`), USB DVD (`ENABLE_LIBUSBDVD`) and background music.

**Owner** decides the final list. Gamecard install (`gc_menu.cpp`), USB/network install streams, the game menu and the homebrew launcher are judgment calls.

How to go about it:
- Prefer CMake options or removing sources, and remove each one's menu entry too.
- Check the `.nro` gets smaller and every remaining menu still opens.

Also, after trimming, clear the remaining Sphaira branding users can see:
- error names `SphairaError_*`, shown in error boxes;
- a `"sphaira v"` string;
- i18n strings that mention Sphaira;
- the inherited screenshots in `assets/screenshots/`, which show Sphaira features.

### P2: gaps and improvements

- **Split packages.** FAT32 can't hold files over 4 GB, so large XCI and NSP files on FAT32 drives come split into `game.xc0`, `game.xc1`, … or a `game.xci`/`game.nsp` folder of `00`, `01`, … parts. Neither form is recognized yet. Support them in the scanner (`package_scan.hpp`), the size reader and the install source, with host tests.
- **Battery guard for single installs.** Installs outside the batch (file browser, USB stream) don't use the battery guard yet.
- **Measure the compressed-only boost.** Install the same plain NSP with **Boost only for compressed games** on and off, and compare the times in `last-batch.txt`. Change the default if boosting is clearly faster.
- **Power cut in the registering window.** In the second or two after content is registered and before the application record is pushed, a power cut can leave a game without its icon. Reinstalling fixes it. Consider detecting it, or document it.
- **Theme music.** Background music keeps playing during installs and drains the battery. Consider pausing it while the screen is off.

### P3: docs, legal, release

- **README for users.** Rewrite it around install steps, controls, troubleshooting and where the logs are. Move developer detail to a separate document.
- **Upstream credit.** Replace `UPSTREAM.md` with a short credit to Sphaira and its author, plus a dated summary of what this fork changed. GPL-3.0 section 5(a) asks for prominent notice of modifications.
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
