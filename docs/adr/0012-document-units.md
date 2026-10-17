# ADR-0012: Document units: values in the document's unit, geometry in millimeters

- **Status:** Accepted
- **Date:** 2026-10-17
- **Milestone:** M1.1 ([#18](https://github.com/DerPW/facetry/issues/18))

## Context

A document has a unit: mm, cm, m or in. Values are shown and entered in it, and the file stores
it. The kernel needs one fixed unit. In M1.1 parameters are plain numbers that features use by
name. Expressions with units of their own ("10 mm + 0.5 in") come with M1.4.

## Options

### Store millimeters, convert for display

The document holds millimeters, and the unit only changes how values are shown and entered.

- Changing the unit is only a relabel.
- Every place that shows or takes a value converts it: parameters, feature details, sketch
  dimensions later. That logic would be repeated in both shells, or the JSON API would need a
  converted copy of every value.
- Typed values come back with conversion noise (0.1 in becomes 2.54 mm and shows as
  0.09999999999999999 in).

### Store values in the document's unit, build geometry in millimeters

The document holds what the user typed. Regeneration multiplies every length by millimeters per
unit before it reaches the kernel.

- The shells show and send values as they are; they only show the unit's symbol.
- The same values regenerate the same geometry on every platform; a document in inches equals
  its millimeter equivalent.
- Changing the unit has to convert every length, or the model would change size.

## Decision

Values are stored in the document's unit; geometry is built in millimeters.

- `Document::units`: `LengthUnit` (mm, cm, m, in). Document format version 2 stores it as
  `"units"`. Version 1 documents migrate as millimeters.
- Regeneration converts lengths with `millimetersPer(units)`. The units are part of each
  feature's cache key.
- `convertUnits()` and the `setUnits` edit change the unit and convert every length:
  - parameters;
  - literal values in sketches, extrusions and fillets.

  The geometry stays the same, and the change is one undo step.
- Every parameter counts as a length until parameters get units of their own (M1.4).
- Reference shadows are kernel geometry and stay in millimeters. Messages show them in the
  document's unit.

## Consequences

- An older Facetry rejects version 2 documents as newer, rather than reading inches as millimeters.
- Angles and unitless values (counts, ratios) need a type when they arrive: `convertUnits()` must
  not scale them. The regenerator then uses a length or an angle resolver per value.
- With expressions (M1.4), a value with its own unit ("1 in" in a mm document) converts when it
  is evaluated. The document unit then becomes the default for values without a unit.
