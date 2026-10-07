# Architecture

AC Customs is intentionally split into three runtime layers plus developer tooling.

```text
+-----------------------------+
|       AC UI Manager         |
| Win32 + Dear ImGui surfaces |
| direct client_portal.dat IO |
+-------------+---------------+
              |
              | local named pipe + snapshot files
              v
+-----------------------------+
| Managed Decal plugin        |
| UI, pack lifecycle, paths   |
+-------------+---------------+
              |
              | dynamic native exports
              v
+-----------------------------+
| ACCustoms.Native.dll        |
| x86 hooks / texture runtime |
| Live Mirror capture         |
+-------------+---------------+
              |
              | in-process calls / observed structures
              v
+-----------------------------+
|  32-bit Asheron's Call      |
+-----------------------------+
```

## Managed plugin

`plugin/managed` is the Decal-facing .NET Framework 4.8 component.

Responsibilities:

- Create the Decal plugin UI.
- Discover/install `.acui` packs.
- Maintain per-user paths under `%LOCALAPPDATA%\ACCustoms`.
- Load `ACCustoms.Native.dll` and resolve its exported C ABI dynamically.
- Point the native runtime at the active replacement/capture directories.
- Coordinate Apply/Restore and developer capture operations.

The managed layer is deliberately light on reverse-engineered memory access.

## Native runtime

`plugin/native/ACCustoms.Native.cpp` contains the client-specific layer:

- Texture/surface observation and replacement.
- Capture of Vanilla bytes before mutation.
- Apply/Restore tracking for DIDs modified in the current session.
- UI snapshot capture and render/blit observation.
- Live Mirror named-pipe server.
- Experimental generated-surface/item provenance and control-state observations.

### Apply/Restore invariant

A theme must never destroy the only known Vanilla data required to restore a live surface. Theme switching therefore behaves conceptually as:

```text
Theme A active
  -> restore A using captured Vanilla bytes
  -> point runtime at Theme B
  -> apply B
```

The native runtime tracks the set of DIDs actually changed by the active theme rather than blindly restoring every file present in a pack.

## UI Manager

`manager/ACModernUIManager.cpp` is a standalone Win32 application. The current Manager:

- Locates/opens `client_portal.dat` read-only.
- Walks the DAT B-tree and builds a texture catalog directly.
- Decodes supported raw RenderSurface data for previews.
- Stores replacement `.rgb` files under `%LOCALAPPDATA%\ACCustoms\User\textures`.
- Imports/exports `.acui` packs.
- Loads snapshots and renders the Live UI Editor.
- Connects to the native Live Mirror bridge when AC is running.

The direct-DAT path replaced the older generated `dat_textures.csv` and hundreds-of-megabytes preview-folder workflow.

## Converter

`manager/ACModernUIConverter.cpp` performs PNG-to-raw conversion out-of-process using Windows Imaging Component (WIC). The Manager passes authoritative metadata from its direct DAT scan:

```text
replace DID width height imageSize pixelFormat input.png output.rgb
```

This keeps the converter simple while avoiding stale development catalogs and absolute source-tree paths.

## Filesystem contract

Program/source files should be read-only at runtime. Mutable data lives under `%LOCALAPPDATA%\ACCustoms`.

Typical structure:

```text
ACCustoms\
  Packs\
  User\
    textures\
    custom_tabs.txt
    texture_notes.txt
  Manager\
    client_dat_path.txt
  Runtime\
    Captured\
    EmptyTheme\
    LiveMirror\
    Snapshots\
  Logs\
```

## Threading and safety

The Manager has background preview/Live Mirror work, while the native DLL runs inside the game process. Hook callbacks should avoid expensive blocking work. Snapshot code uses bounded capture windows and atomic/finalized files so the Manager does not consume partially written scenes.

Reverse-engineered pointer reads must remain defensive. A valid address on one supported client build is not proof of validity on another.
