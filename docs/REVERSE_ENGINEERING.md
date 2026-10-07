# Reverse-engineering notes

This document records durable findings that affect production code. It replaces the pile of one-off ProbeXX markdown/build artifacts used during discovery.

The values below are empirical observations from the supported 32-bit AC client used during development. They are **not** stable API contracts. Revalidate them when supporting a different executable build.

## Important observed addresses

| Address / value | Current interpretation | Notes |
|---|---|---|
| `0x00442C70` | UI texture/surface blit path | Used to observe source/destination rectangles and late-stage UI drawing. A `sourceDid` of zero can indicate a generated/intermediate surface. |
| `0x0058DFB0` | Item-cache image composition/rebuild path | Observed while the client composes generated item imagery from file-backed resources; used experimentally for item provenance. |
| `0x0079E280` | Observed control/vtable family used in state-table investigation | State metadata is control-specific; do not infer universal texture-state adjacency from it. |
| `0x00472160` | Control-state routine investigated by earlier probes | Useful historical evidence for transition/state behavior. |
| `0x006A0610` | Observed downstream image-resource change path | Appeared in state-link investigation around node resource changes. |

Where source comments have more precise signatures/offsets, treat the source as authoritative for the current implementation and update this table when those assumptions change.

## UI blit observations

The `0x00442C70` path is valuable because it sees what the renderer actually draws, including source cropping and destination geometry. It is *not* sufficient to reconstruct every high-level resource relationship: by this stage some content has already been composed into DID-less render surfaces.

This distinction is why AC Customs keeps both:

- file-DID observations, and
- generated-surface provenance experiments.

## Generated item imagery

A key finding from the item-icon work is that a final UI blit may not carry the original file DID. The client can compose an item-cache/generated surface first and later draw that surface. Correlating the composition path with later blits is therefore more promising than assuming the final blit can always be mapped directly to a DAT resource.

Do not silently convert a failed provenance lookup into an arbitrary DID; unresolved is safer than editing the wrong asset.

## Control states

The project tested state values including normal/hover/pressed/selected-family variants and inspected control state records/property structures. The durable conclusion is that state relationships are not uniform enough to model as a simple global texture pairing rule.

Prefer:

1. directly observed state table/resource relationships;
2. transition evidence tied to a known control;
3. explicit "unknown" when neither exists.

Avoid heuristics that globally declare a neighboring DID to be a hover/pressed texture merely because that pattern works for some controls.

## Working rules for new hooks

For every new client hook or structure offset:

- Record the exact client build/hash when practical.
- Identify the calling convention and argument evidence.
- State whether the interpretation is proven, inferred, or heuristic.
- Bound pointer reads and validate expected vtables/ranges before dereferencing.
- Make hook installation failure non-fatal when the feature is diagnostic/optional.
- Keep Apply/Restore correctness independent from experimental probes.
- Prefer an observation-only first version before mutating client state.

## Historical probe material

The source archive used to prepare the public repository contained separate notes for Live Mirror bridge versions, state-table probes, item-DID tests, generated pixel dumping, and transition-driven state learning. Those files are not included individually because they described successive experiments, some of which contradicted or superseded earlier assumptions. Their durable findings are summarized here and in `LIVE_MIRROR.md`.

## Item-cache correlation lesson

One failed iteration is worth preserving because it explains an otherwise tempting regression: an early item-surface correlation implementation tried to learn `generatedSurface -> itemId` inside an older Probe87 detour on the `0x00442C70` blit path. That detour was not installed by the production Decal initializer, so the correlation code never ran in normal Live Mirror operation.

The later direct-cache approach instead observes the completed item-cache entry immediately after `0x0058DFB0` returns and reads the generated 32x32 surface references at the observed cache-entry offsets (`+0x20/+0x24` in that client build). This is a stronger production integration point because it does not depend on a diagnostic-only hook being enabled.

When adding future provenance logic, verify that the hook is installed by the **production initialization path**, not merely present in an old probe source file.

## Transition-driven state learning

A later state experiment moved away from broad snapshot-time state-table scanning. The safer model was to observe natural `0x00472160` transitions on known `0x0079E280` controls, then cache only the directly observed state/image relationship for states encountered in real UI use. This reduces arbitrary pointer traversal and avoids inventing mappings for states the client never exercised.
