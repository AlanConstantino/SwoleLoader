# Visible-list traversal check

Claim under test: traversing a viewport near the end of a large list should visit only visible cells, with the same visible indices and positions as the original renderer. This is not a Switch frame-rate or installation-throughput benchmark.

Run with `sh tests/nxloader/run_lists.sh --benchmark`. Both paths are C++20 release builds (`-O2`, no exceptions), timed in the same executable in five old/new pairs. Each side traverses 1,000 viewports near the end of a 100,000-item list; an opaque, non-inlined callback consumes every visible index and coordinate. The old path reproduces the former single-column drawing loop; the new path calls the helper used by rendering and touch hit-testing.

Host observation on 2026-11-12: Apple Silicon macOS, 18 logical CPUs, load averages 1.42 / 2.05 / 2.04. Other browser and development processes were running; the sides were interleaved. Reported timings exclude compilation and the separate profiling run.

```text
run  old_ms     new_ms
1    85.761666  0.031917
2    61.624834  0.028333
3    60.419125  0.028375
4    60.252834  0.028417
5    60.549917  0.028333

median total: old 60.549917 ms, new 0.028375 ms
range total:  old 60.252834–85.761666 ms, new 0.028333–0.031917 ms
loop visits:  old 99,997,001, new 10,000
visible callbacks on both sides: 10,000
visible-output checksum on both sides: 1,003,435,010
errors: 0
```

The limiter in the original traversal is CPU time spent walking and rejecting off-screen rows. An unreported, separate `--profile` run sampled with macOS `sample` attributed all 851 collected stacks to `Legacy(long long, float)`. The new traversal computes the first visible row directly and bounds work by the viewport. The test suite also checks 480 grid layouts against the original loop, horizontal layouts, touch-to-drawn-index identity, partial rows, fractional spacing, exact boundaries, and a billion-item synthetic list with only ten visited cells.

These results establish the reduced traversal work and preserved layout contract. They do not include filesystem reads, font rendering, NanoVG/GPU work, display presentation, controller input, or Switch hardware. Full UI latency and its remaining bottleneck must be measured on the console; no full-frame speedup is inferred from this microbenchmark.
