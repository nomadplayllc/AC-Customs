# Changelog

## Unreleased

### Public source preparation

- Established a clean public repository layout for the managed plugin, native runtime, UI Manager, converter, and snapshot viewer.
- Removed compiled binaries, build intermediates, backup files, generated catalogs, logs, and nested Git repositories from the source distribution.
- Vendored only the required Dear ImGui and MinHook source files with upstream licenses preserved.
- Made native build inputs repository-relative instead of depending on `C:\ACModernUI-Dev`.
- Updated the Manager/converter replacement path so conversion metadata comes from the Manager's direct DAT scan rather than the legacy `dat_textures.csv` development catalog.
- Added architecture, build, Live Mirror, ACUI, reverse-engineering, and contribution documentation.
