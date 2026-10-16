# Input model

How each kind of input controls the viewport, on every platform. Both shells implement this
table (`apps/apple/Facetry/Viewport/ViewportView.swift`, `apps/web/src/render/viewport.ts`). The
camera and picking behind it are shared C++ ([ADR-0004](adr/0004-viewport-renderer-shared-cpp-sokol.md)).

The guiding idea from the [roadmap](ROADMAP.md#product-principles): **fingers navigate, the
Pencil draws and selects precisely**, and keyboard plus mouse or trackpad work fully on their own.
Until drawing tools exist (M1.3), a Pencil drag orbits like a finger.

## Navigation and selection

| Action | Finger (iPad, web touch) | Apple Pencil / pen | Mouse (Mac, iPad, web) | Trackpad (Mac, iPad) |
|---|---|---|---|---|
| Orbit around the point under the pointer | one-finger drag | drag | left drag | two-finger scroll, or click-drag |
| Pan | two-finger drag | — | right or middle drag, Shift + left drag | Shift + two-finger scroll |
| Zoom toward the pointer | pinch | — | wheel | pinch |
| Select | tap | tap | click | click |
| Highlight what is under the pointer | — | hover (supported iPads) | move | move |

**Pick radius**, the distance from the pointer within which edges and vertices are preferred
over faces:

| Input | Radius |
|---|---|
| Finger | 18 pt |
| Pencil / pen | 8 pt |
| Mouse, trackpad | 6 pt |

## Keyboard

Mac, iPad with a hardware keyboard, and the web app:

| Key | Action |
|---|---|
| `F` | Fit the model into the view |
| `O` | Toggle orthographic / perspective |
| `Esc` | Clear the selection |

The full shortcut set close to Fusion 360 (`L` line, `E` extrude, `S` command search, …) comes
with M1.8.

## Platform notes

- **Web trackpads:** browsers report two-finger trackpad scrolling as wheel events, which cannot
  be told apart from a mouse wheel reliably. On the web, two-finger scrolling therefore zooms,
  and Shift + scroll pans. Pinch zooms (Chrome and Safari send it as wheel events with Ctrl).
- **Web pens:** pressure and tilt are available through Pointer Events; squeeze, barrel roll and
  double-tap are not ([Platforms](PLATFORMS.md)).
- **Apple Pencil Pro gestures** (squeeze, barrel roll, double-tap) are not used yet; they arrive
  with M1.8.
