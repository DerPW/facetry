# ADR-0008: Native file format

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.7

## Context

Facetry needs one file format that every platform opens and edits the same way
([Platforms](../PLATFORMS.md)). It has to:

- travel as **one file** through the Files app, iCloud Drive, AirDrop, email, a browser download
  or a Git repository;
- hold the **document description**: the timeline, parameters and references. The core already
  defines it as JSON (`core/include/facetry/core/document.hpp`, M0.6);
- optionally carry a **thumbnail** for the Files app and document browsers;
- optionally carry a **geometry cache**, so large models can show up before they finish
  regenerating;
- be **versioned from the first release**, with migrations instead of breaking old files;
- be written by the same code on every platform.

The M0.5 Apple app stores a provisional JSON file (`formatVersion` 0) without a model.

## Options

### Plain JSON (`document.json` only)

- Simplest, human-readable and diff-friendly.
- No place for a thumbnail or a binary cache except base64 inside the JSON, which bloats it.

### Zip container

- One file on every platform, compressed, and extensible with more entries later.
- Widely used for exactly this: FreeCAD's `.FCStd`, 3MF, Office documents.
- Not directly diffable; the JSON inside can be extracted for that.

### Directory package (an Apple document bundle)

- Native on Apple, with incremental saves through `FileWrapper`.
- On the web, on other systems, in email and in Git it shows up as a folder. That is bad for
  sharing, which is half the point of the web app.

### SQLite

- Incremental writes and queries.
- Needs SQLite in the WebAssembly module (about 1 MB) and is overkill for documents of a few
  hundred kilobytes.

## Decision

A `.facetry` file is a **zip archive**:

| Entry | Required | Content |
|---|---|---|
| `manifest.json` | yes | `{"format": "facetry", "formatVersion": 1, "createdWith": "<app version>"}` |
| `document.json` | yes | The core's document description, with its own `formatVersion` and per-feature `namingVersion` |
| `thumbnail.png` | no | A square PNG of the model, up to 512 × 512, rendered by the app when saving |
| `cache/…` | no | Geometry cache: one OCCT binary B-Rep per body and an index with the cache keys that produced them |

- **One implementation:** the core reads and writes the container with
  [miniz](https://github.com/richgel999/miniz) (MIT, one C file). The shells only move bytes:
  `FileDocument` data on Apple, a `File` or `Blob` on the web.
- **Compression:** JSON and cache entries are deflated; the PNG is stored as is.
- **Versions:**
  - `manifest.formatVersion` covers the container layout.
  - `document.json` and each feature carry their own versions (ADR-0006).
  - A file with a newer version than the app knows is refused with a clear message.
  - Older versions are migrated step by step in the core. Every version keeps an example file in
    `tests/documents/` that must keep loading.
- **The cache is disposable:**
  - It is only used if its keys match what regeneration would compute.
  - Deleting it, or a cache from another app version, only costs time.
  - Correctness never depends on it.
- **Unknown entries** are ignored when reading. They are not written back.
- **File type:**
  - The Apple type `io.github.derpw.facetry.document` stays as declared in M0.5 (conforms to
    `public.data` and `public.content`). It deliberately does not conform to
    `public.zip-archive`, so Finder and Files do not offer to unpack documents.
  - The web app accepts `.facetry` in its file pickers and registers it as a PWA file handler
    where the browser supports that.
- **M0.5 files:** provisional `formatVersion` 0 files contain no model. They open as an empty
  document.

## Consequences

- **Implemented in M1.1** ([#11](https://github.com/DerPW/facetry/issues/11)):
  `core/include/facetry/core/file_format.hpp`, `facetry_session_open_file` /
  `facetry_session_save_file` in the C API, and `facetry-cli pack` / `unpack`. Entries have a
  fixed time, so the same document always gives the same bytes. Thumbnails can be stored, but the
  apps do not render them yet (M1.9). The geometry cache is not written yet; it comes when large
  models need it.

- **M1.1 implements format v1:**
  - container code and miniz in the core;
  - a migration hook;
  - thumbnails from both shells.
- **Saving rewrites the whole file.** Documents are small (the M0.6 example is about 2 KB of
  JSON). If caches make saving slow, the cache can be written less often.
- **Autosave and crash recovery** (M1.1) write the same format, so a recovered file is an
  ordinary document.
- **Debugging and diffs:** a CLI command to extract and pack `document.json` (`facetry-cli`)
  comes with M1.1. Then a document can be inspected or versioned as text.
- **The geometry cache format** depends on the OCCT version. The cache index records it, and a
  different OCCT version ignores the cache.
