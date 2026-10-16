# ADR-0006: Persistent naming

- **Status:** Accepted
- **Date:** 2026-10-16
- **Milestone:** M0.6

## Context

Features reference faces, edges and vertices of earlier results: a fillet references edges, a
sketch is placed on a face, a hole on a face. OpenCASCADE numbers sub-shapes by their position in
the result, and every regeneration can change that order. References stored as indices jump to
the wrong element or break as soon as an earlier feature changes. This is the *topological naming
problem*. FreeCAD lived with it for about twelve years
([prior art](../PRIOR-ART.md#persistent-naming)).

Requirements for Facetry:

- References survive parameter changes (sizes) and topology changes elsewhere in the model, for
  example a sketch edge inserted before the referenced one.
- A reference that really no longer exists is reported on the feature. It is never silently
  rebound to something else.
- Names are deterministic: the same document yields the same names on every platform (native and
  WebAssembly).
- The scheme must not depend on OCCT's output order.

## Options

### Indices with geometric re-matching

- Simple, and how most simple tools start.
- This is exactly the TNP: indices shift, and geometric re-matching guesses wrong as soon as
  geometry changes.

### OCAF TNaming

- OCCT's own framework: it records shape evolutions per algorithm and re-finds selections.
- It requires OCAF's document model and strict rules for every algorithm. FreeCAD did not adopt
  it, and Facetry's document model is not OCAF.

### OCCT 8 BRepGraph

- New in OCCT 8: a flat topology graph with UIDs that survive compaction and reordering
  *within* one graph, plus layers for history and parameters.
- A model rebuilt from changed parameters is a new graph with new UIDs, so names that survive
  regeneration are still needed on top. It may become useful later for fast adjacency queries
  and for history inside one long operation.

### Names derived from history (FreeCAD) and queries against history (Onshape)

- **FreeCAD:** names are built from OCCT's `Generated`/`Modified` history inside one wrapper around
  the makers, with an indexed shadow name for debugging.
- **Onshape:** a reference is (creating feature, role in that feature, disambiguator), and a
  repair tool propagates fixes.
- **Known limits:** elements split into several pieces are ambiguous, and names get long.

## Decision

A history- and semantics-based scheme, built on the lessons above:

1. **Faces are named by the feature that creates them**, using OCCT's operation history (one
   shared naming pass used by every feature):
   - A face that is unchanged or modified **keeps its name**. A trimmed top face stays the top
     face, so references to it survive later features being added, edited or removed.
   - A face generated from an input element is named `<feature>:<role>[<source name>]`, for
     example `f2:side[f1/l2]` for the side face extruded from sketch line `l2`, or
     `f3:fillet[<edge name>]`.
   - Faces with a fixed role get that role, for example the start and end caps of an extrusion:
     `f2:start`, `f2:end`.
   - Faces without history get `<feature>:new`.
2. **Edges and vertices are named by their neighbors**: an edge by the sorted names of its
   adjacent faces (`edge{f2:end|f2:side[f1/l2]}`), a vertex by the sorted names of the faces
   meeting there. Robust face names make edge and vertex names robust for free, and no per-feature
   edge bookkeeping is needed.
3. **Sketch elements carry stable IDs** (`f1/l2` for line `l2` in sketch `f1`). Editing a size
   keeps the IDs; inserting a line adds a new ID without touching the others.
4. **Ties are broken geometrically, never by OCCT's output order.** When several elements end up
   with the same name (a face split in two, two edges between the same faces), they get `#1`,
   `#2`, … ordered by their centroid (x, then y, then z, with a tolerance).
5. **A reference stores the name plus a shadow**: the element's last resolved ID and a geometric
   fingerprint (centroid, length or area, curve or surface type). The shadow is for debugging and
   for one fallback:
   - If the name is not found but exactly one element matches the fingerprint, the reference
     resolves **with a warning on the feature**. It is never silent, and the stored name is not
     rewritten: only the user confirms a replacement.
   - Otherwise the reference is **broken**: the feature reports which reference failed, it is
     skipped, and regeneration continues with the previous result.
6. **Repair** (UI in M1.1): a feature with broken references shows an error badge in the
   timeline. Its dialog lists each missing reference with its last known place (the shadow),
   drawn as a ghost in the viewport. The user picks a replacement or removes the reference; the
   new reference replaces the old one as one undo step, and the features after it regenerate and
   re-resolve their own references.
7. **Every feature records the version of its naming algorithm** (`namingVersion`). When an
   algorithm changes, old documents can be migrated instead of silently changing names.

## Consequences

- Fillets, sketch placements and similar references survive edits to sizes and to unrelated
  topology. The naming lives in `core/src/kernel/occt_naming.cpp`; the M0.6 scenarios are in
  `core/tests/naming_scenarios_test.cpp` and run on macOS, in the iOS simulator and in
  WebAssembly.
- Names are long nested strings. That is fine for the prototype. If documents grow too large,
  names can be hashed into a table (as FreeCAD does) without changing the scheme.
- Splitting into three or more pieces is resolved by geometric order. That is deterministic, but
  can pick a different piece after large geometry changes. Such references are reported when they
  stop resolving rather than silently moved.
- New features need two things only: run the shared naming pass with their OCCT maker, and give
  their generated faces a role.
