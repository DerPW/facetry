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
| Fit the view | two-finger double-tap | — | `F` | two-finger double-tap (smart zoom, Mac) |
| Turn the view to a face, edge or corner | tap the ViewCube | tap the ViewCube | click the ViewCube | click the ViewCube |
| Orbit with the ViewCube | drag it | drag it | drag it | drag it |
| Select | tap | tap | click | click |
| Add to or remove from the selection | tap with **Multi** on | tap with **Multi** on | Shift/Cmd/Ctrl + click | Shift/Cmd + click |
| Select the next element under the pointer (overlapping, or a hidden face behind) | tap the same spot again | tap the same spot again | click the same spot again | click the same spot again |
| Precision selector | long press on the model: a crosshair above the finger selects where it points on release | — | — | — |
| Box selection | long press on empty space, then drag; or drag with **Box** on | drag with **Box** on | left drag in the Fusion preset, or with **Box** on | the same as the mouse |
| Highlight what is under the pointer | — | hover (supported iPads) | move | move |
| Choose an extrusion's profiles ("Choose profiles") | tap a region to add or remove it | tap | click | click |

**Box selection:** dragged to the right, a *window* (solid blue) takes what lies entirely inside;
dragged to the left, a *crossing box* (dashed green) also takes what it touches, like in Fusion.
It follows the selection filter; with "All" it takes edges. Shift, Cmd, Ctrl or **Multi** add to
the selection.

**Choosing profiles:** the sketch's regions show over the model, translucent blue, and only they
can be picked. Every click or tap adds a region or removes it; Done takes the selected ones,
Cancel keeps the extrusion as it was.

The mouse and trackpad columns show the **Facetry** preset, the default.

### Navigation presets

Mouse and trackpad behavior is a preset, chosen in the viewport controls (Navigation) and stored
per device. The mapping is shared C++ (`render/src/navigation.cpp`), so every platform agrees.

| Input | Facetry | Fusion | Trackpad |
|---|---|---|---|
| Left drag | orbit | — (click selects; a drag will box-select, #37) | orbit |
| Shift + left drag | pan | — | pan |
| Middle drag | pan | pan | pan |
| Shift + middle drag | pan | orbit | pan |
| Right drag | pan | — (context menu later) | pan |
| Mouse wheel | zoom | zoom | orbit |
| Shift + wheel | pan | zoom | pan |
| Two-finger trackpad scroll | orbit | pan | orbit |
| Shift + two-finger scroll | pan | orbit | pan |
| Pinch | zoom | zoom | zoom |

Apple tells a trackpad from a mouse wheel (precise deltas); browsers cannot, so on the web all
scrolling counts as a mouse wheel. **Trackpad** makes two-finger scrolling orbit there too, for
laptops without a mouse.

### Touch settings

**One finger pans** swaps the finger gestures: one finger pans and two fingers orbit around their
midpoint; pinch still zooms. The Pencil keeps orbiting until drawing tools exist (M1.3).

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
| `F` | Fit the model into the view, keeping the view direction (like Fusion's Fit) |
| `Shift+F` | Fit the selection into the view |
| `Home` | Home view: the whole model from the front right, above |
| Numpad `1`, `3`, `7` | Front, right and top view; with `Ctrl` back, left and bottom (as in Blender) |
| `O` | Toggle orthographic / perspective |
| `G` | Show or hide the ground grid |
| `1` … `5` | Select vertices, edges, faces, bodies, or everything (like Plasticity's selection modes) |
| `Esc` | Clear the selection |
| `⌘Z` / `Ctrl+Z` | Undo the last step (its name is shown on the Undo button) |
| `⇧⌘Z` / `Shift+Ctrl+Z` / `Ctrl+Y` | Redo |

On Apple, undo goes through the system's undo manager: the Edit menu and, on iPad, the
three-finger undo and redo gestures work too.

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
