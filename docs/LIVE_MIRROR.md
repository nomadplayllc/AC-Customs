# Live Mirror

Live Mirror connects the standalone Manager to the in-process native runtime without exposing a network service. Communication uses the local named pipe:

```text
\\.\pipe\ACCustoms.LiveUI.v1
```

## Protocol

The server greets a connected Manager with:

```text
HELLO 1
```

The Manager requests a scene with:

```text
CAPTURE
```

Current responses include:

- `READY <generation>` — a finalized snapshot for that generation is available.
- `VANILLA_REQUIRED` — capture is intentionally refused while a custom theme is active.
- `BUSY` — another relevant operation/capture is active.
- `PATH_ERROR` — runtime snapshot path setup failed.
- `EMPTY` — capture completed but produced no usable scene.
- `CAPTURE_ERROR` — capture/finalization failed.

The Manager waits for a generation to be applied before requesting the next one, reducing churn and stale-scene races.

## Snapshot schema

The current native source writes `formatVersion: 3` snapshots. A scene includes a live UI tree and observed blit data. Depending on what the client exposes during the capture window, the snapshot can also contain dumped file-backed assets, generated/DID-less assets, item provenance, and experimental control-state metadata.

Snapshots are finalized before `READY` is sent. The writer uses a temporary/finalization pattern so consumers do not treat a partially written JSON file as complete.

## File-backed textures

For ordinary file-backed UI textures the snapshot can preserve:

- source DID
- source dimensions
- source rectangle/cropping evidence
- destination/root-relative geometry
- blend/alpha information
- repeated-call counts

The Manager can map a selected scene element back to its editable DAT DID and show Vanilla/replacement previews.

## Generated/DID-less surfaces

Some AC UI imagery is not drawn directly from a `0x06xxxxxx` file DID by the time the final UI blit happens. Item icons are an important example: the client may compose a generated render surface from one or more file-backed resources and later blit that generated surface with `sourceDid == 0`.

The native code contains experimental correlation paths around item-cache composition and generated surfaces. These are useful but not yet a contractual stable feature. See `KNOWN_LIMITATIONS.md` before changing the Manager to assume that every generated blit has reliable provenance.

## Interactive/control state metadata

The project has also investigated Normal/Hover/Pressed/Selected-family state data for controls. State relationships vary by control type and are not sufficiently uniform to treat every texture as having a deterministic secondary-state sibling. Current code should therefore prefer directly observed state relationships over heuristic "next DID" assumptions.

## Safety rule: Vanilla capture

Live Mirror capture is designed to request the scene while the runtime is in Vanilla state. This prevents a captured scene from silently baking active replacement content into what the editor believes is the original UI.

## Debugging

When debugging Live Mirror, distinguish three layers:

1. **Bridge** — pipe connectivity/protocol response.
2. **Capture** — whether the native side finalized a usable snapshot.
3. **Scene load/render** — whether the Manager can parse and render that snapshot.

Do not report a scene parse failure as a bridge protocol failure; the Manager keeps separate status/error paths for this reason.
