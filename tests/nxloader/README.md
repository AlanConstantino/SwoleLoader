# SwoleLoader host tests

The footer, package-size parser, capacity math and selection-size worker have
focused runners `run_footer.sh`, `run_package_size.sh`, `run_capacity.sh` and
`run_selection_size.sh`. `run_cart.sh` covers persistent selections across sources,
parent/child overlaps, exclusions and undo, review edits and completed-package
protection, partial folder failures, and 10,000 queued selections. They cover bounded layout/metadata work, expansion,
unknown/error states, capacity shortages, 10,000 selections, cache lifecycle and
nonblocking cancellation/retirement. These run in the main suite as well.

The suite includes real-zstd NCZ output tests (host `pkg-config libzstd` required),
elapsed-time transfer-rate/ETA tests, and install timing/report tests. These verify
correctness; they do not measure the Switch's install throughput.

`batch_review_test.cpp` checks game-folder grouping for nested content, arbitrary
subfolder names, overlapping roots, empty selections, loose packages, path roots,
cancellation, and 20,000 packages across 10,000 selected game folders.

Run `sh tests/nxloader/run.sh` with a C++20 compiler. These host tests use no Switch
libraries and compile with exceptions disabled, matching the application build.
They cover recursive and overlapping folder selection, mount-path normalization,
case-insensitive package extensions, cancellation, directory read failures,
memory/path limits, invalid entries, hidden folders, and skipped symlinks.

The same runner also executes:

- `run_browser.sh`: the actual background directory worker, with host filesystem/thread adapters. Covers 100,000 entries, blocked reads, navigation supersession, cancellation, reordering with retained selections, metadata caching, limits, and errors.
- `run_lists.sh`: visible-cell traversal against legacy layout results, touch indexing, clipping, fractional positions, and large lists. Add `--benchmark` for the isolated traversal measurement described in [list_benchmark.md](list_benchmark.md).
- `run_ncz.sh`: sequential NCZ block lookup across boundaries, partial reads, missing blocks, large offsets, and a 100,000-block workload.

- `run_self_update.sh`: release selection for over-the-air updates (version tags, renamed `.nro` files, missing signatures, sizes, links) and the `app.cmake` checks (repository links, versions, names). The signature test also needs openssl and mbedtls 2.x: set `MBEDTLS_DIR` to an mbedtls source tree with `library/libmbedcrypto.a` built. Without it, that part is skipped.

These tests do not drive the rendered Switch UI, USB driver, or title installation services.

The portable scanner accepts a streaming directory-reader callback. Return an
empty string for success, an error description for failure, and stop reading
immediately when the supplied `emit` callback returns false. File paths are only
returned after the entire scan succeeds, sorted lexically. No package is opened
or installed by the scanner. Extensions supported are NSP, NSZ, XCI, XCZ and MSP.
Hidden descendant directories, AppleDouble `._` files, and `EntryKind::Other`
entries are skipped. Explicitly selected hidden roots are still scanned.

`ScanOptions` limits distinct packages, distinct directories, and path length in
bytes. Reaching a limit is an error, not a truncated successful result. The UI
must pass its actual filesystem path capacity as `max_path_length`.

For a read-only inventory of selected folders, compile the optional harness:

```sh
c++ -std=c++20 -fno-exceptions -I sphaira/include \
    tests/nxloader/scan_directory.cpp -o /tmp/nxloader-scan
/tmp/nxloader-scan '/Volumes/Your Drive/Folder A' '/Volumes/Your Drive/Folder B'
```

The harness does not follow symlinks or write to the scanned folders. It prints
package paths to stdout and a count to stderr. These tests validate discovery and
queue input, not USB mounting or Switch installation.
