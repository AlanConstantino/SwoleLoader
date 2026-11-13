# How SwoleLoader works

The details behind the [README](../README.md): what the browser and installer do to stay fast with large libraries, how sizes and free space are estimated, and what the host tests cover.

## Performance

Installation throughput and responsive browsing of large libraries are the main design requirements. Directory enumeration, filtering and sorting run on a worker; changing folders supersedes an unfinished load. Drawing and controller handlers perform no package-size I/O. Selecting entries queues size calculations on a separate worker; unselected folders are not recursively indexed. The list visits only visible cells, independent of how far down a large library you scroll. Alphabetical browsing skips size/date lookups; choosing size sort requests file sizes on the worker. Folder counts and timestamps are omitted from the browser rows to avoid extra drive reads.

Selections belong to a session-wide install list keyed by source and path. Browsing folders, changing sort order and switching panes or mounts preserve them. Parent/child selections count each package once; removing a package inside a selected folder records an exclusion. Completed estimates survive navigation. Exclusion edits can reuse a bounded package catalog instead of rereading the drive; an explicit retry refreshes it. Bulk additions survive navigation and submit at most 32 entries per frame. Each source worker delivers at most 32 changed summaries per poll; only visible rows render.

Selected rows show source-file bytes independently of estimated installed bytes. Folder sizes include supported packages recursively, including updates/DLC, and arrive incrementally. NSZ/XCZ sizing first attempts the original content header through the installer's existing header reader, with bounded compression-metadata parsing as a fallback. No payload decompression or full-file hashing is performed for sizing. Transient open/read errors receive one retry; failed estimates can be refreshed and are not permanent cache entries. Partially known sizes are retained.

The top-right storage bars preview used space, selected content, reserve and remaining space for the active destination. Unknown or pending estimates are marked incomplete; their remaining-space figure assumes all known content is new and excludes additional unknown requirements. The detailed footer totals are optional under **Y → Advanced Options → Install options → Show selection size details**, off by default.

The browser accepts up to 100,000 entries in a directory and reports an error above that limit. Sort changes return you to the browser while the new order loads.

Block-compressed NSZ installation advances through its block table once instead of searching from the beginning for each block. Ordinary NSP content does not allocate decompression buffers. The existing pipelined reads/decompression/writes and integrity checks remain in place. Actual install throughput still depends on the USB connection, compression, target storage and console configuration; host tests do not establish a Switch speedup.

NSZ output reuses its buffers and fills the existing 4 MiB write window without overflow copies. The reader's small NCZ header lookahead no longer reserves another 4 MiB per content file. Content is still flushed after writing; metadata parsing reuses that flush, and files that do not need a filesystem path avoid that service call. Buffer limits, verification settings and file-based emuMMC pacing are unchanged.

The displayed transfer rate is reconstructed content output per second, calculated from actual elapsed time. For NSZ this can exceed compressed input throughput. It is a current-content rate, not whole-game installation throughput. Batch reports now include whole-package/batch elapsed time, source/output byte counts, time in read/write calls, processing time excluding queue waits, each worker's queue waits, write pacing, and reused/skipped content counts. These measurements do not add disk writes during each chunk; the report is saved once after the batch.

Use `/config/swoleloader/last-batch.txt` to investigate a bottleneck. Stage times overlap because workers run concurrently and must not be added together. Long processor input waits can indicate an input constraint; long processor output waits can indicate destination pressure. Scheduler delays and setup/flush/commit time also matter, so one counter alone does not prove the cause. Output bytes count accepted content-write calls even if a later operation fails; the package result is authoritative. Compare complete installs of equivalent content, excluding skipped runs, and repeat before claiming a speedup.

## Game grouping

Game grouping follows the folders you select, without reading package payloads or guessing game identities. Select the individual game folders (or use **Select all visible** in their parent folder) for one group per game. Selecting a whole library folder still recursively includes everything, displayed under that selected folder. Loose files use their containing folder, combining conventional Base/Updates/DLC subfolders under their parent game folder.

## Scanning and installing

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

## Tests

```sh
sh tests/nxloader/run.sh
```

Host tests exercise the scanner against in-memory directories and temporary real directories: nesting, multiple roots, duplicate selections, mixed-case extensions, Unicode, metadata files, symlink cycles, errors, cancellation, and bounds. They do not call Switch installation services.

Additional tests check bounded visible-list drawing/touch traversal and monotonic NCZ block lookup. [Traversal benchmark details](../tests/nxloader/list_benchmark.md) document a host microbenchmark, not complete UI or installation performance.

Real-zstd output tests check reconstructed bytes across output boundaries, fragmented input, raw/compressed blocks, recycled buffers, errors and cancellation. Transfer-rate tests cover delayed/unequal intervals, idle time, transfer resets, offset rewinds and unknown ETA. Diagnostic-report tests keep compressed input, accepted output, elapsed time and skip counters distinct. The NCZ tests require a host zstd development library (`pkg-config libzstd`).

Game-folder review tests cover updates/DLC, arbitrary nested folder names, overlapping selections, empty folders, loose files, mounted/relative paths, cancellation, and 20,000 packages across 10,000 selected game folders. Review grouping and row construction run on the scan worker, and the review takes ownership of the prepared rows.

Size tests cover bounded sparse NSP/NSZ/XCI/XCZ metadata, expansion, malformed input and cancellation. Worker tests cover 10,000 selections, bounded polling, cache reuse/eviction, stale-result rejection, pause/resume and retiring blocked I/O. Capacity tests cover reserves, shortage, unknown/overflow handling and storage-bar bounds. Selection-cart tests cover persistent multi-source paths, parent/child deduplication, exclusions, review removal/undo, completed-package protection, partial read errors and 10,000 queued selections. The parser tests exercise the original-header fallback and preservation of known portions. Footer tests check long labels stay on-screen with disjoint touch targets. Host tests do not establish console responsiveness or installation throughput.

The NRO can be cross-compiled and structurally checked here, but actual USB discovery, controller interaction, installation, cancellation cleanup, and throughput must be verified on a Switch. No console performance improvement is claimed from host tests. Start hardware verification with one small package, then two folders, then a larger batch; test a cancelled scan and a disconnected drive before relying on unattended installation.
