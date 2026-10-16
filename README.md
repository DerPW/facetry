# Facetry

Parametric 3D CAD for iPad, Mac and the web — Fusion 360's way of working, with a pencil-first
interface.

> **Status: Phase 0, technical foundation.** Nothing useful to run yet. See the
> [roadmap](docs/ROADMAP.md) for where things stand.

## What Facetry wants to be

- **Parametric like Fusion 360:** sketches with constraints and dimensions, a feature timeline you
  can roll back and edit, components and bodies, user parameters.
- **Direct like Shapr3D:** select a face and push it. Tools appear where you are working. Draw
  with the Apple Pencil and navigate with your fingers.
- **Friendly for beginners:** start with simple shapes, or build models from Scratch-style
  blocks with sliders for every value.
- **Everywhere:** the same core and file format on iPad (App Store), Mac (direct download) and
  in the browser (WebAssembly).
- **Local and private:** no account, no cloud requirement, no tracking.

Read more in the [roadmap](docs/ROADMAP.md).

## Built with AI

Facetry is developed with heavy AI assistance ("vibe coded"), mainly with
[Claude Code](https://claude.com/claude-code). The maintainer sets the direction, reviews the
changes and tests the app, but a large part of the code is written by AI. The project says this
openly. Small pull requests, recorded decisions and automated tests keep it maintainable.

## Technology

- **Core:** C++20 on top of the [OpenCASCADE](https://dev.opencascade.org/) geometry kernel,
  shared by all platforms
- **iPad & Mac:** SwiftUI and Metal
- **Web:** TypeScript, with the core compiled to WebAssembly

Details: [Architecture](docs/ARCHITECTURE.md) · [Platforms & distribution](docs/PLATFORMS.md)

## Documentation

| Document | Contents |
|---|---|
| [Roadmap](docs/ROADMAP.md) | Vision, phases, milestones, benchmark models, release plan, risks |
| [Phase 0 — Foundation](docs/roadmap/phase-0-foundation.md) | Builds, core, renderer, app shells, persistent naming |
| [Phase 1 — Core Modeling](docs/roadmap/phase-1-core-modeling.md) | The MVP: sketch, extrude, fillet, timeline, export |
| [Phase 2 — Advanced Modeling](docs/roadmap/phase-2-advanced-modeling.md) | Parameters, patterns, shell, loft, sweep, threads, … |
| [Phase 3 — Assemblies & Output](docs/roadmap/phase-3-assemblies-and-output.md) | Joints, drawings, scripting, blocks, 1.0 |
| [Future ideas & non-goals](docs/roadmap/future.md) | What is not scheduled, and what is out of scope |
| [Architecture](docs/ARCHITECTURE.md) | Structure, rules, document model, decisions |
| [Platforms & distribution](docs/PLATFORMS.md) | Feature matrix and how each platform is shipped |
| [Building](docs/BUILDING.md) | Toolchain, build presets, formatting, CI |
| [Benchmarks](docs/benchmarks.md) | Measured sizes, load times and memory |
| [Decision records](docs/adr/README.md) | Why the architecture looks the way it does |
| [Contributing](CONTRIBUTING.md) | Workflow and conventions |

## License

Facetry is released under the [MIT License](LICENSE). Third-party components keep their own
licenses. OpenCASCADE, for example, is LGPL-2.1 with an exception (see
[Architecture](docs/ARCHITECTURE.md#third-party-components)).

Facetry is an independent project. It is not affiliated with Autodesk (Fusion 360) or Shapr3D.
Product names are trademarks of their respective owners.
