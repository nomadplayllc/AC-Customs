# ACUI pack format v1

An `.acui` file is a ZIP-compatible container for AC Customs texture replacements.

## Container constraints

The native Manager's v1 writer/reader intentionally keeps the format simple:

- Standard ZIP structure.
- STORE method (no compression) for pack entries.
- No encryption.
- No multi-disk archives.
- No ZIP64 requirement for normal v1 packs.
- CRC32 is checked.

## Required layout

```text
manifest.json
textures/
  06001234.rgb
  06005678.rgb
  ...
```

Texture names use the eight-digit hexadecimal DID used by the AC client.

## Manifest

A typical manifest is:

```json
{
  "formatVersion": 1,
  "name": "Example UI",
  "author": "Example Author",
  "description": "Example AC Customs pack",
  "textures": [
    {
      "did": "06001234",
      "width": 32,
      "height": 32,
      "imageSize": 4096,
      "pixelFormat": "0x00000015",
      "file": "textures/06001234.rgb"
    }
  ]
}
```

The Manager may emit additional metadata such as `formatInfo` or `paletteDID`; the managed plugin tolerates unknown JSON fields.

## Supported raw formats

The current editor/runtime path primarily handles:

- `0x14` — 24-bit BGR payload (`width * height * 3`).
- `0x15` — 32-bit BGRA payload (`width * height * 4`).

The manifest's `imageSize` must match the dimensions and bytes-per-pixel expected for the format. The corresponding `textures/<DID>.rgb` entry must have exactly that byte count.

## Import safety

The managed plugin validates the manifest and archive before installing a pack, stages the install, and avoids treating a partial extraction as a valid installed pack. Duplicate/invalid paths and payload-size mismatches are rejected.

The Manager performs its own v1 parsing/validation for authoring/import/export.

## Versioning

Future incompatible changes should increment `formatVersion`. Readers should reject versions they cannot safely interpret rather than guessing.
