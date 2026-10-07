# Changelog

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
