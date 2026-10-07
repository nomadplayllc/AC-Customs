# Public-source publication manifest

This file records how the public tree was derived from the October 2026 development archive. It is intentionally conservative: the public repository contains the maintainable product, not every probe or intermediate binary used to discover it.

## Included

- Current managed Decal plugin source (`PluginCore`, `NativeBridge`, `PackManager`, project/resource/view files).
- Current native runtime source (`ACCustoms.Native.cpp`).
- Current UI Manager source (`ACModernUIManager.cpp` + `ACModernUIModern.inl`).
- Current PNG/raw converter source.
- Default texture groups and texture notes used by the Manager.
- Minimal build-required Dear ImGui and MinHook sources with licenses.
- New reproducible build scripts and developer documentation.

## Excluded as generated/build output

- `*.exe`, `*.dll`, `*.obj`, `*.pdb`, `*.lib`, `*.exp`, generated `*.res`.
- Managed `bin/` and `obj/` trees.
- ImGui/MinHook build directories.
- Runtime logs, snapshots, INI state, generated texture catalogs, and encountered-texture data.

## Excluded as development history / superseded experiments

Examples include:

- `*.pre_*.bak` backup files.
- `ACModernUIManager_PreDAT.cpp`, `ACModernUIManager_PreTabs.cpp`.
- Historical executable variants such as `ACModernUIManager_AsyncGenerated.exe`, `..._LiveIntegrated.exe`, `..._UIPolish.exe`, etc.
- Earlier native-hook experiments (`ACModernUI.cpp`, `ACModernUI_HotSwapConfirmed.cpp`, `ACModernUI_PaletteConfirmed.cpp`, `ACModernUI_PixelFormatConfirmed.cpp`, `ACModernUI_WorkingBackup.cpp`).
- `ACModernUIAttach.cpp`, `TestLoader.cpp`, the obsolete CSV/preview DAT test utility, and the standalone `ACCustomsSnapshotViewer` debug utility.
- One-off patch scripts tied to the old development path.
- Individual probe markdown files after their durable findings were consolidated into `docs/`.

Git history should be used for future source history instead of accumulating backup copies in the repository.

## Excluded for licensing/review

`Saved Packs/DarkMode_v1.acui` was **not** included in the public tree. It contains many raw replacement texture payloads. Even if the pack was authored for AC Customs, the asset provenance/redistribution rights should be explicitly confirmed before publishing it as an example.

Likewise, test images and any extracted/reconstructed original game artwork were omitted unless clearly needed and clearly owned/redistributable.

## Third-party cleanup

The development archive contained nested `.git` repositories for Dear ImGui and MinHook. Their `.git` histories were removed. AC Customs vendors only the source files necessary to build, with upstream license files retained.

MinHook's development working tree appeared dirty due broad line-ending changes. The public copy uses source content read directly from the recorded Git commit `8af6b4acae5a9388fd742b56fa79ece89d96f823`.

## Functional cleanup made for publication

The old converter path still expected `C:\ACModernUI-Dev\dat_textures.csv` even though the current Manager already reads `client_portal.dat` directly. The public tree adds an explicit converter `replace` command: the Manager passes DID, dimensions, byte size, pixel format, input PNG, and destination path directly. This removes that stale development-directory dependency from the supported replacement workflow while leaving the historical converter preview modes available for developers.

## Outstanding maintainer review

Two non-code details should get a maintainer sign-off:

1. Confirm `plugin/managed/ACCustoms.ico` is project-owned or otherwise redistributable.
2. Decide whether the MIT copyright line should remain `AC Customs contributors` or use a specific maintainer/project legal name.
