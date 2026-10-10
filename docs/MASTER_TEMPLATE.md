# Master Template, Mapping Editor, and Reverse Exporter

The **Template Editor** is a development feature of AC Customs UI Manager.
It works with a fixed **2000 x 2000** master PNG, linked to texture DIDs by
mapping rectangles. The original art and all labels/guides remain in the
embedded master.

## Edit the template live

1. Build and run the Manager; load `client_portal.dat`.
2. Open **Import / Export > Master Template**.
3. Choose **Export Original Template**, or generate an editable template from
   the **current workspace** or a selected **ACUI pack**.
4. Open the PNG in Photoshop, Krita, GIMP, or another art editor. Keep the size
   **2000 x 2000** and do not move the template sections/texture slots.
5. Back in the Manager, use **Choose Edited PNG / Start Watching...** once.
   Subsequent saves in the art editor update replacement previews automatically.

## Generate an editable template (reverse export, v0.6)

- **From Current Workspace...** uses current `.rgb` replacements (read-only).
- **From ACUI Pack...** opens an ACUI v1 archive without importing it into
  your workspace. The built-in reader expects the project's STORE ZIP format.
- Each compatible mapped DID is copied to its mapped pixel bounds over the
  embedded master. Unchanged sections, labels, guides, and art stay visible.
- Replacement dimensions/format must match the DAT record. Invalid or missing
  replacements are skipped; shared-slot conflicts are counted in the summary.
- BGRA alpha is carried into the exported RGBA PNG. The output is a **flat PNG**,
  not a layered `.psd`.
- You can immediately choose the exported PNG for the live save watcher.

## Edit mappings

In **Template Editor**, hover/click a mapped rectangle to select it; modify
DID and bounds in the inspector, or add/remove mappings. **Show all bounds**
displays outlines and **Black out** hides mapped art to find unmapped areas.
The image uses nearest-neighbor filtering at zoom.

### Critical: custom mappings are stored outside the repository

Your **fully edited mapping registry is NOT included in this source ZIP**.
The editor saves it on your machine under:

`%LOCALAPPDATA%\ACCustoms\Manager\template_mapping_overrides.dev.json`

The *source ZIP* contains only the original embedded mapping and source PNG.
Replacing the repository folder does **not** delete your AppData mapping file.
For migration to another PC or a new Windows profile, copy/back up this file
separately. If you do not, the Manager uses the original embedded 293 mappings.

## Source assets / maintenance

- `manager/DefaultTemplate_2000x2000px.png` — original source art.
- `manager/template_auto_map.dev.json` — original 293-entry mapping source.
- `manager/ACModernUITemplateEmbedded.inl` — compiled master art and defaults.
- `tools/generate_assets.py` — regenerates that compiled header deliberately.
- `tools/test_reference.py` — validates embedded asset and mapping (requires
  Python and Pillow). Not required for compiling the Manager.

**Regenerating the embedded header will use the original 293-entry map.** Do
not regenerate it expecting your AppData mapping overrides to be embedded.

Build: `scripts\build_manager.cmd` from repository root on Windows with
Visual Studio's x86 C++ build tools.
