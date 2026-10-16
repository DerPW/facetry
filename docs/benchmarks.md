# Benchmarks

Measured numbers that guide technical decisions. Each section says what was measured, on which
machine, and with which build. Numbers are refreshed when the setup changes noticeably.

## Phase 0 summary (M0.7)

Measured on 2026-10-16 on the M0.2 machine (MacBook Pro, Apple M3, 8 GB RAM, macOS 27.0),
Emscripten 6.0.12 and Node 24.19. These are the baseline for the Phase 1 performance targets
(M1.10).

### Regeneration

`facetry-cli bench tests/documents/corner-fillet.json --runs 50`: the M0.6 example (sketch →
extrude → fillet, 3 features). Release builds; the median of 50 runs, three times, with the
spread shown.

| Measurement | macOS, native | WebAssembly (Node 24) |
|---|---|---|
| Load the document (parse and validate JSON) | 0.02 ms | 0.05 ms |
| Regenerate from scratch | 2.0 ms | 3.6–3.8 ms |
| First regeneration in a fresh process | 2.8–31 ms | 39–41 ms |
| After changing the first parameter (all 3 features run) | 2.0–2.1 ms | 3.6–3.9 ms |
| Nothing changed (all from the cache) | 0.01 ms | 0.02 ms |
| Peak memory | 17.5 MB process RSS | 26.9 MB WebAssembly memory |

- WebAssembly takes about 1.8 times as long as native code once warmed up.
- The first run pays for page faults, OCCT's start-up and, in WebAssembly, the JIT tiers.
- Browsers run the same code in V8 (Chromium) or JavaScriptCore (Safari). Safari compiled
  WebAssembly about three times slower in M0.2, so expect a slower first run there.

### Download size

| Module | Raw | gzip -9 | brotli 11 |
|---|---|---|---|
| `facetry-core.wasm` (C API, modeling, meshing, M0.6 document model) | 10.60 MB | 3.95 MB | 2.75 MB |
| `facetry-render.wasm` | 0.14 MB | 0.05 MB | 0.04 MB |
| PlaneGCS spike (ADR-0005), for comparison | 0.29 MB | 0.11 MB | 0.08 MB |

The document model added only 107 bytes to the core module: the C API does not reach it yet,
so the linker removes it. It grows with M1.1. PlaneGCS will add less than its spike size,
because the C++ runtime it brings along is already in the core.

### Frame time

Unchanged since M0.4 (see [Viewport](#viewport-m04)): 120 Hz in the Mac app and in Chromium,
Safari capped at 60 Hz with 1.3 ms of GPU time for 215k triangles. On a real iPad (iPad Pro
M4, 2026-10-17) the stress model also runs at 120 Hz.

### Sketch solver spike

PlaneGCS solve and diagnosis times, native and WebAssembly, are in
[ADR-0005](adr/0005-sketch-solver-planegcs.md#spike).

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
- On-device viewport numbers for a real iPad are in [Viewport](#viewport-m04).

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
| **iPad Pro 11" (M4), Metal** ⁵ | M0.6 example block | full screen | 120 Hz (8.33 ms) | — | 0.09–0.16 ms | — |
| **iPad Pro 11" (M4), Metal** ⁵ | 215,482 triangles (stress) | full screen | 120 Hz (8.33 ms) | — | 0.09–0.16 ms | — |

1. Average of 60 frames drawn back to back, then waiting for the GPU (`viewport.benchmark(60)`).
2. Not measured: the built-in browser pane was hidden, which throttles `requestAnimationFrame`.
3. Safari on macOS limits `requestAnimationFrame` to 60 Hz by default. With 1.3 ms of GPU work per
   frame, the viewport is far from being the limit.
4. The simulator ran with heavy swapping on an 8 GB machine, so it says nothing about speed.
5. Measured on 2026-10-17 on iPadOS 27.0.1, Debug build, with a free personal team
   ([#7](https://github.com/DerPW/facetry/issues/7)). The app's statistics line while orbiting
   with a finger. Also checked: picking, orbit, pan and pinch with fingers, and Pencil hover
   (highlights while hovering), drag and tap. The iPad holds its 120 Hz with the stress model.

The web renderer module is small: `facetry-render.wasm` is 0.14 MB raw and 0.04 MB brotli.

CPU picking (ADR-0004) costs well under a millisecond per pointer move at 215k triangles.
The linear scan over edge segments dominates. A screen-space grid is the next step if larger
models need it.
