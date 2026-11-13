# SwoleLoader

💪 Never skip install day. A Nintendo Switch homebrew app that bulk-installs NSP, NSZ, XCI and XCZ files from a USB drive in one go, with list-wide progress, battery protection and automatic updates.

SwoleLoader is a fork of [Sphaira](https://github.com/ITotalJustice/sphaira), built on its USB filesystem support and YATI installer. See [UPSTREAM.md](UPSTREAM.md).

## Performance priorities

Installation throughput and responsive browsing of large libraries are the main design requirements. Directory enumeration, filtering and sorting run on a worker; changing folders supersedes an unfinished load. Drawing and controller handlers perform no package-size I/O. Selecting entries queues size calculations on a separate worker; unselected folders are not recursively indexed. The list visits only visible cells, independent of how far down a large library you scroll. Alphabetical browsing skips size/date lookups; choosing size sort requests file sizes on the worker. Folder counts and timestamps are omitted from the browser rows to avoid extra drive reads.

Selections belong to a session-wide install list keyed by source and path. Browsing folders, changing sort order and switching panes or mounts preserve them. Parent/child selections count each package once; removing a package inside a selected folder records an exclusion. Completed estimates survive navigation. Exclusion edits can reuse a bounded package catalog instead of rereading the drive; an explicit retry refreshes it. Bulk additions survive navigation and submit at most 32 entries per frame. Each source worker delivers at most 32 changed summaries per poll; only visible rows render.

Selected rows show source-file bytes independently of estimated installed bytes. Folder sizes include supported packages recursively, including updates/DLC, and arrive incrementally. NSZ/XCZ sizing first attempts the original content header through the installer's existing header reader, with bounded compression-metadata parsing as a fallback. No payload decompression or full-file hashing is performed for sizing. Transient open/read errors receive one retry; failed estimates can be refreshed and are not permanent cache entries. Partially known sizes are retained.

The top-right storage bars preview used space, selected content, reserve and remaining space for the active destination. Unknown or pending estimates are marked incomplete; their remaining-space figure assumes all known content is new and excludes additional unknown requirements. The detailed footer totals are optional under **Y → Advanced Options → Install options → Show selection size details**, off by default.

The browser accepts up to 100,000 entries in a directory and reports an error above that limit. Sort changes return you to the browser while the new order loads.

Block-compressed NSZ installation advances through its block table once instead of searching from the beginning for each block. Ordinary NSP content does not allocate decompression buffers. The existing pipelined reads/decompression/writes and integrity checks remain in place. Actual install throughput still depends on the USB connection, compression, target storage and console configuration; host tests do not establish a Switch speedup.

NSZ output reuses its buffers and fills the existing 4 MiB write window without overflow copies. The reader's small NCZ header lookahead no longer reserves another 4 MiB per content file. Content is still flushed after writing; metadata parsing reuses that flush, and files that do not need a filesystem path avoid that service call. Buffer limits, verification settings and file-based emuMMC pacing are unchanged.

The displayed transfer rate is reconstructed content output per second, calculated from actual elapsed time. For NSZ this can exceed compressed input throughput. It is a current-content rate, not whole-game installation throughput. Batch reports now include whole-package/batch elapsed time, source/output byte counts, time in read/write calls, processing time excluding queue waits, each worker's queue waits, write pacing, and reused/skipped content counts. These measurements do not add disk writes during each chunk; the report is saved once after the batch.

Use `/config/swoleloader/last-batch.txt` to investigate a bottleneck. Stage times overlap because workers run concurrently and must not be added together. Long processor input waits can indicate an input constraint; long processor output waits can indicate destination pressure. Scheduler delays and setup/flush/commit time also matter, so one counter alone does not prove the cause. Output bytes count accepted content-write calls even if a later operation fails; the package result is authoritative. Compare complete installs of equivalent content, excluding skipped runs, and repeat before claiming a speedup.

## Load it on the Switch

1. Copy `swoleloader.nro` to `/switch/swoleloader/swoleloader.nro` on the Switch's microSD card, or extract the release ZIP at the card's root. The homebrew-menu entry is **SwoleLoader**, author **AlanConstantino**. Builds from before the rename were called NXLoader-GPT and lived in `/switch/nxloader-gpt/`; delete that folder once SwoleLoader is installed. SwoleLoader copies the old settings from `/config/nxloader/config.ini` on its first launch.
2. Start the homebrew menu in full application mode and launch **SwoleLoader**. On a standard Atmosphere setup this means holding **R** while opening an installed title and selecting a user. The console must already run CFW.
3. Connect the external drive to the Switch Lite's USB-C port using a connection that supports USB host data. The inspected `Sandisk_2TB` drive uses exFAT, which is supported.
4. In the file browser, press **X → Mount** and choose the USB drive. Open the folder containing the game folders with **A**.
5. Highlight each desired game folder and click **R3**, **ZL**, or **ZR** to select it. Press again to deselect. Continue browsing and selecting in other folders; earlier choices remain selected. The active storage bar previews remaining space. **X → Select all visible** queues the listing, and **X → Clear selection** clears the whole install selection. A minus marker denotes a partially selected folder.
6. Press **X → Scan and install selection**. Subfolders are included. If installation is disabled, enable it for the current sysMMC/emuMMC environment using the app's prompt, then choose the action again.
7. Review packages grouped by game, with source/installed sizes at the right. **X** (also R3/ZL/ZR) removes the highlighted package or whole game from this installation; it does not delete source files. **Y** undoes removals, **L** retries sizing (or rescans an unreadable folder), and **A** shows details. Removed rows stay visible and disabled. Choose **Install selected — stop on first error** or **Install selected — continue after errors**. Unknown installed sizes are warnings and can be attempted; known capacity shortages and unavailable destination space remain actionable in this same review. Change the install destination under **Y → Advanced Options → Install options → Install location** before opening review.
8. While several packages install, **X** switches the progress box between the current game and the whole list. The list view shows the bytes installed out of the total, a progress bar and the time left for everything selected, based on the average speed since the batch started. Press **X** again to return to the current game.
9. **B** opens the cancellation prompt during scanning or installation. After a batch, return to the same editable review: failed/unfinished packages remain selected, completed packages cannot be queued again by Undo. Remove problem items or retry the remaining selection. Details, including unknown-size reasons for attempted packages, are saved at `/config/swoleloader/last-batch.txt`. The report is saved after every package, so it survives a batch that is cut off.

**+** returns to the homebrew menu at `/hbmenu.nro`. If the homebrew menu is missing or cannot be launched, the app shows an error and stays open. The app opens directly to the file browser. Pressing **A** on an NSP, NSZ, XCI, XCZ or MSP file opens the install review for it, or for the current selection if there is one.

Sphaira's other features are removed: the appstore and other download menus, network mounts, FTP/MTP, NXlink, forwarders and launching homebrew or emulators, the album, game and save management, dumping, themes and background music. SwoleLoader only installs packages, updates itself, and keeps the file browser's file operations (copy, move, rename, delete, zip, hash). USB drive options are under **Y → USB drives**.

Game grouping follows the folders you select, without reading package payloads or guessing game identities. Select the individual game folders (or use **Select all visible** in their parent folder) for one group per game. Selecting a whole library folder still recursively includes everything, displayed under that selected folder. Loose files use their containing folder, combining conventional Base/Updates/DLC subfolders under their parent game folder.

## What this version does

- Recursively discovers NSP/NSZ, plus the installer's existing XCI/XCZ/MSP formats. Extensions are case insensitive.
- Selects multiple sibling folders, including a single top-level folder containing an entire collection. A selection belongs to the currently displayed directory.
- Scans on a worker thread without reading package payloads or copying them onto the microSD first.
- Deduplicates overlapping paths and presents the queue in lexical path order. This does not infer base/update/DLC dependencies from filenames or deduplicate different copies of the same title. Sphaira's existing installed-content checks remain enabled by default.
- Installs one package at a time through YATI, retaining its buffered reads, NSZ decompression, content writes, and integrity checks.
- Skips hidden subdirectories, AppleDouble `._` files, and directory entries identified as symlinks. An explicitly selected hidden folder can still be scanned.
- Rejects incomplete scans on read errors, cancellation, overlong paths, or resource limits instead of installing a partial discovery result. Limits: 32,768 packages and 16,384 directories per scan.
- Defaults USB storage to write protected. The scanner and batch installer read source packages; the report and configuration are written to microSD.

`Completed` means the installer returned success; existing-content skips also count as completed. Cancellation does not uninstall packages already completed. A batch is not persisted for restart/resume. The report records attempted packages and an unfinished count.

Storage estimates assume all selected content is new, so existing-content skips can make actual usage lower. Capacity checks reserve 64 MiB plus 64 KiB per content file for filesystem/database overhead. The review refreshes package metadata instead of trusting cached selection estimates, then checks free space again before the batch and each package. Background size work is cancelled and drained before scanning/installing, so it does not compete with installation reads/writes. These are conservative estimates, not an integrity check or a guarantee against later media errors. MSP and unsupported/malformed size metadata remain unknown but do not by themselves block an attempt. The installer still applies its normal validation and handles actual allocation errors. A failed folder scan appears as a removable/retryable issue while readable packages remain available.

## Build

A running Podman or Docker engine is required. The wrapper chooses Podman when installed; `CONTAINER_ENGINE=docker` overrides it. It builds a pinned devkitA64/libnx/libgloss image and retains compilation cache between runs.

```sh
./tools/nxloader-build.sh Dev
./tools/nxloader-build.sh Release
```

Outputs are `build/Dev/swoleloader.nro` or `build/Release/swoleloader.nro`, and a matching `switch/swoleloader/swoleloader.nro` directory inside that build. `NXLOADER_BUILD_JOBS=4` is the default. The first build fetches dependencies and needs network access.

Use `./build_release.sh` to build Release into `dist/`: the `.nro`, its update signature, a ZIP for installing by hand, and a ZIP of debug symbols for crash reports. The app's name, author, version, install folder and update repository are set in [app.cmake](app.cmake).

## Battery and power loss

In handheld mode a USB drive takes the console's only USB-C port, so it can't charge during a long batch. Install docked, with the drive in the dock, or use a USB-C hub with charging passthrough. A flash drive or SSD draws much less power than a hard drive.

- **Battery guard** (**Install options → Stop when battery is low**, on by default). The progress box shows the battery. On battery, a batch asks before starting below 30%, starts no new game at 15% or below, and cancels the current game at 5%, which removes its partly written data. Games already installed are kept, and the rest stay in Review. On the dock or a full-power charger it does nothing. A weaker charger counts as battery, since the console can still drain on one.
- **Screen off during installs** (**Advanced Options**, 1 minute by default). During installs and other long tasks, in handheld mode, the screen turns off after that long without input. Any button or touch turns it back on, and the task keeps running. **Y** still turns it off at once. While the screen is off the app stops drawing.
- **Boost only for compressed games** (**Advanced Options**, on by default). The CPU is boosted only while NSZ/XCZ content is unpacked, which is CPU bound. Plain NSP/XCI installs and copies are limited by the drive. To compare speeds, turn it off to boost every transfer as before, and compare the times in `last-batch.txt`.

If an install is cut off, by power loss or a crash, its partly written data is removed at the next launch. Each install lists the temporary files it creates in `/config/swoleloader/interrupted-install.txt` and removes the list when it ends, so a list left behind means an install was interrupted. Only those files are deleted, never system downloads. A notification says how much space came back.

Games finished before the cutoff are installed. Select the same folders and install again: with **Skip if already installed** on, the finished ones are skipped quickly, and the interrupted game starts over.

## Logs and crash reports

Logging is on by default (**Y → Advanced Options → Logging**). Files go to `/config/swoleloader/logs/` on the microSD card:

- `log.txt`: this session. It starts with the app version, launch mode, memory and free space.
- `previous.txt`: the session before. After a crash or freeze, this is the one to read. A session that passes 4 MiB also moves its older half here.
- `crash.txt`: the most recent crash. It is written even with logging off.

The log records install batches, errors and notifications, along with the existing detail from the installer, USB, network and updates. Lines are written in batches about once a second, and failures are written immediately, so logging no longer slows installs the way per-line writes did. **Y → Advanced Options → View logs** opens any of the three on the console. A crash report shows its addresses as `nro+0x…`. To turn them into functions and source lines, use the symbols file from the same version:

```sh
tools/symbolize.sh swoleloader-0.1.6.elf crash.txt
```

Each release includes `<folder>-<version>-symbols.zip`, which holds that `.elf`. For a local build it is `build/Release/sphaira/sphaira.elf`. A crash report only matches the exact build that produced it.

## Updates

The app can update itself from GitHub releases: **Y → Check for updates**, plus an automatic check at launch (**Advanced Options → Check for updates on launch**). It only installs releases signed with the project's key and never installs without asking. Updates stay off until `APP_UPDATE_REPO` in [app.cmake](app.cmake) names the release repository. [RELEASING.md](RELEASING.md) covers setup, publishing a version, the signing key and renaming the app.

## Verification

```sh
sh tests/nxloader/run.sh
```

Host tests exercise the scanner against in-memory directories and temporary real directories: nesting, multiple roots, duplicate selections, mixed-case extensions, Unicode, metadata files, symlink cycles, errors, cancellation, and bounds. They do not call Switch installation services.

Additional tests check bounded visible-list drawing/touch traversal and monotonic NCZ block lookup. [Traversal benchmark details](tests/nxloader/list_benchmark.md) document a host microbenchmark, not complete UI or installation performance.

Real-zstd output tests check reconstructed bytes across output boundaries, fragmented input, raw/compressed blocks, recycled buffers, errors and cancellation. Transfer-rate tests cover delayed/unequal intervals, idle time, transfer resets, offset rewinds and unknown ETA. Diagnostic-report tests keep compressed input, accepted output, elapsed time and skip counters distinct. The NCZ tests require a host zstd development library (`pkg-config libzstd`).

Game-folder review tests cover updates/DLC, arbitrary nested folder names, overlapping selections, empty folders, loose files, mounted/relative paths, cancellation, and 20,000 packages across 10,000 selected game folders. Review grouping and row construction run on the scan worker, and the review takes ownership of the prepared rows.

Size tests cover bounded sparse NSP/NSZ/XCI/XCZ metadata, expansion, malformed input and cancellation. Worker tests cover 10,000 selections, bounded polling, cache reuse/eviction, stale-result rejection, pause/resume and retiring blocked I/O. Capacity tests cover reserves, shortage, unknown/overflow handling and storage-bar bounds. Selection-cart tests cover persistent multi-source paths, parent/child deduplication, exclusions, review removal/undo, completed-package protection, partial read errors and 10,000 queued selections. The parser tests exercise the original-header fallback and preservation of known portions. Footer tests check long labels stay on-screen with disjoint touch targets. Host tests do not establish console responsiveness or installation throughput.

The NRO can be cross-compiled and structurally checked here, but actual USB discovery, controller interaction, installation, cancellation cleanup, and throughput must be verified on a Switch. No console performance improvement is claimed from host tests. Start hardware verification with one small package, then two folders, then a larger batch; test a cancelled scan and a disconnected drive before relying on unattended installation.

## Source and license

Based on [NaGaa95/sphaira](https://github.com/NaGaa95/sphaira) commit `8430e420f0ea4ea800c6689f6797c38e97aafbc0` (Sphaira 1.0.9). The GPLv3 license is retained in [LICENSE](LICENSE); original documentation and contributor credits are retained in [UPSTREAM.md](UPSTREAM.md).

SwoleLoader keeps its settings, logs, translation overrides and caches in `/config/swoleloader/`, so it doesn't touch a Sphaira install on the same card. Its updater only checks the repository in `app.cmake` and only installs releases signed with this project's key, so a Sphaira release cannot replace the custom build. Some inherited UI labels still use the Sphaira name. SwoleLoader's additions are in `sphaira/include/nxloader/`, `sphaira/source/nxloader/`, and the file-browser integration.
