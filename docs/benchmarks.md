# Benchmarks

Measured numbers that guide technical decisions. Each section says what was measured, on which
machine, and with which build. Numbers are refreshed when the setup changes noticeably.

## OpenCASCADE 8.0.1 (M0.2)

Measured on 2026-10-16. Machine: MacBook with Apple M3 (8 cores, 8 GB RAM), macOS 27.0,
Xcode 27.0, Emscripten 6.0.12. OCCT artifact `occt-8.0.1-r1`.

### Build time from source

With OCCT's precompiled headers and all 8 cores. Building without precompiled headers was
projected at roughly 40 minutes per target.

| Target | Time |
|---|---|
| macOS (arm64) | 6 min |
| iOS device (arm64) | 6 min |
| iOS simulator (arm64) | 4 min |
| WebAssembly | 6 min |

CI runners have 3 (macOS) or 4 (Ubuntu) cores, so a build there takes considerably longer. It
only happens once per OCCT version or build revision.

### Binary size

28 toolkits: everything requested in `occt.env` plus the dependencies OCCT adds, including the
TKService/TKV3d/TKCAF chain that STEP needs.

| Item | Size |
|---|---|
| `OCCT.framework` per Apple slice (stripped, all toolkits) | 43 MB |
| Apple artifact (`.tar.gz`, three slices + headers) | 53 MB |
| WebAssembly static libraries | 121 MB |
| WebAssembly artifact (`.tar.gz`) | 33 MB |
| Headers | 36 MB, 6179 files |

### WebAssembly download size

Release build (`-O3`). Unused code is removed by the linker, so the size depends on what the
program actually uses.

| Program | Raw | gzip -9 | brotli 11 |
|---|---|---|---|
| Modeling only: box, boolean cut, fillet, meshing, volume | 9.95 MB | 3.71 MB | 2.58 MB |
| Core tests: modeling + STEP read/write | 14.10 MB | 4.76 MB | 3.33 MB |

STEP adds about 0.75 MB (brotli) to the download. That is small enough to start without lazy
loading. A separate, on-demand STEP module stays an option for later (see
[Platforms](PLATFORMS.md)).

### Load time and memory

Time to download, compile and initialize the core tests (release build, served from localhost),
and total time including all 7 smoke tests: a filleted box, a failing fillet that throws, a STEP
round trip and version checks.

| Platform | Initialize | Initialize + all tests | Memory |
|---|---|---|---|
| macOS, native (dynamic `OCCT.framework`) | 0.05–0.18 s process start | 0.18 s | 23 MB peak RSS |
| iOS simulator, native | — | passes (startup includes `simctl spawn`) | — |
| Node 24.19 (WebAssembly) | — | 1.1 s (debug build) | — |
| Chromium 152 (built-in browser) | 89–149 ms (one outlier at 524 ms) | 128–181 ms | 20.5 MB Wasm memory |
| Safari 27.0 | 342–448 ms | 500–645 ms | 20.5 MB Wasm memory |

Notes:

- Safari takes about three times longer than Chromium to compile and instantiate the module.
  Still well below a second, but worth watching as the core grows.
- Over a real network, the 3.3 MB (brotli) download adds to these numbers. Browsers cache the
  compiled module afterwards.
- On-device numbers for a real iPad come with the app shell in M0.5.

### Web module (M0.3)

The core module the web app loads (`facetry-core.wasm`, release build). It contains the C API,
modeling (box, cylinder, booleans, fillet), tessellation and logging. STEP is not linked in yet.

| Raw | gzip -9 | brotli 11 |
|---|---|---|
| 10.11 MB | 3.77 MB | 2.63 MB |

In the browser the module loads in the core Web Worker. Box → fillet → tessellate, sent from the
UI thread through the worker, completes right after start-up (Chromium, local dev server).

### Known issue: setjmp/longjmp and WebAssembly exceptions

OCCT normally defines `OCC_CONVERT_SIGNALS`, which wraps error handlers in `setjmp`/`longjmp`.
Combined with `-fwasm-exceptions`, Emscripten 6.0.12 produced invalid WebAssembly in
`ShapeUpgrade_ShapeDivide::Perform` ("br_table: label arity inconsistent"). The module failed to
compile in Node and browsers. The WebAssembly build therefore uses `-UOCC_CONVERT_SIGNALS`.
WebAssembly has no signals to convert, so no behavior is lost. Native builds are unaffected.

## Viewport (M0.4)

Measured on 2026-10-16 on the same MacBook Pro (Apple M3, 120 Hz ProMotion display). Demo part:
the plate with rounded edges, boss and hole. **Stress** tessellates it finely to 215,482
triangles. 4× MSAA everywhere.

| Platform | Model | Framebuffer | Presented frame rate | GPU time per frame¹ | Draw call (CPU) | Pick (18 pt radius) |
|---|---|---|---|---|---|---|
| macOS app, Metal | 3,522 triangles | window | 120 Hz (8.33 ms) | — | 0.05–0.4 ms | — |
| macOS app, Metal | 215,482 triangles (stress) | window | 120 Hz (8.33 ms) | — | 0.1–0.4 ms | — |
| Chromium 152, WebGL2 | 3,522 triangles | 2048 × 1536 | 119 fps | — | 0.10 ms | — |
| Chromium 152, WebGL2 | 215,482 triangles (stress) | 2048 × 1536 | — ² | 1.0 ms | — | 0.6 ms |
| Safari 27, WebGL2 | 215,482 triangles (stress) | 3024 × 1794 | 56 fps ³ | 1.3 ms | — | 0.6–0.8 ms |
| iPad simulator, Metal | 3,522 triangles | 2064 × 2752 | not meaningful ⁴ | — | 0.16 ms | — |

1. Average of 60 frames drawn back to back, then waiting for the GPU (`viewport.benchmark(60)`).
2. Not measured: the built-in browser pane was hidden, which throttles `requestAnimationFrame`.
3. Safari on macOS limits `requestAnimationFrame` to 60 Hz by default. With 1.3 ms of GPU work per
   frame, the viewport is far from being the limit.
4. The simulator ran with heavy swapping on an 8 GB machine. **A real iPad still has to be
   measured** (M0.5).

The web renderer module is small: `facetry-render.wasm` is 0.14 MB raw and 0.04 MB brotli.

CPU picking (ADR-0004) costs well under a millisecond per pointer move at 215k triangles.
The linear scan over edge segments dominates. A screen-space grid is the next step if larger
models need it.
