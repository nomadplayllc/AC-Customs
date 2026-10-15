# Changelog

## 1.5.0.0 — Auto Apply and smoother texture switching

- Added **Auto Apply On Login** in Advanced, off by default. Remembers the last successfully applied PNG theme and starts applying it after `LoginComplete` with no fixed delay.
- Added a frame-sliced texture swap queue (up to **3 textures per rendered frame**, with the existing soft 4 ms budget), preventing overlapping theme changes.
- Added a smooth, text-free blue progress bar on Browse that appears only while applying or restoring a theme.
- Supports switching between themes, restoring Classic UI, and relogging using the queue; completion updates the last successfully used theme.
- Retained targeted crash-loop protection for interrupted automatic swaps and terminating managed exceptions. Normal Alt+F4 and window close are not interpreted as crashes after a completed swap.
- Kept the experimental early-login texture trigger out of this release. Auto Apply begins after the character finishes entering the world.


## 1.4.0.0 — In-game automatic plugin updates

- New **Update** tab is visible only if the published GitHub plugin version is newer than the installed assembly.
- Added manual **Check Plugin Version** control in Advanced; mapping updates remain independent.
- Download `ACCustoms.dll` and `ACCustoms.Native.dll` with SHA-256/size verification.
- Embed an external helper in `ACCustoms.dll` to install both DLLs safely after AC exits.
- Generate release manifests and versioned GitHub assets with `scripts/publish_plugin_release.ps1`.
- Continue previous Theme Library Add Local, Remove, and Rebuild changes in this source bundle.


## Unreleased — VVS interface

- Move the in-game window and controls to Virindi View Service; VVS is now required.
- Keep the interface available when native Decal view rendering is disabled.
- Wait for VVS startup and dispose the view and event subscriptions on shutdown.
- Discover the VVS build dependency and document installation and Windows validation.
- Successfully built on Windows; user testing confirmed the VVS interface and revised layout work.

## Unreleased

### Public source preparation

- Established a clean public repository layout for the managed plugin, native runtime, UI Manager, converter, and snapshot viewer.
- Removed compiled binaries, build intermediates, backup files, generated catalogs, logs, and nested Git repositories from the source distribution.
- Vendored only the required Dear ImGui and MinHook source files with upstream licenses preserved.
- Made native build inputs repository-relative instead of depending on `C:\ACModernUI-Dev`.
- Updated the Manager/converter replacement path so conversion metadata comes from the Manager's direct DAT scan rather than the legacy `dat_textures.csv` development catalog.
- Added architecture, build, Live Mirror, ACUI, reverse-engineering, and contribution documentation.
