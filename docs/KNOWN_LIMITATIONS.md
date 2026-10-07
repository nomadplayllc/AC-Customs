# Known limitations

This list describes current engineering limits, not permanent product goals.

## Generated / DID-less Live UI imagery

Some runtime UI imagery is drawn from generated surfaces whose final blit does not carry a normal `0x06xxxxxx` file DID. The native runtime contains item-cache/provenance experiments, but generated inventory/vendor icons are not yet guaranteed to render, hit-test, or resolve to a replaceable DAT texture consistently in the Live UI Editor.

Treat unresolved generated surfaces as unresolved; do not guess a replacement DID.

## Interactive state relationships

Normal/Hover/Pressed/Selected texture relationships differ across control implementations. Automatic "state-hood" detection is experimental and can be incomplete. The project deliberately avoids a universal neighboring-DID heuristic.

## Direct DAT reader

The current Manager's direct DAT scan intentionally supports the RenderSurface forms needed by the active editor path without introducing a general DAT dependency. At present it accepts uncompressed texture records with pixel formats `0x14` (BGR) and `0x15` (BGRA); compressed or other formats are counted/skipped rather than guessed.

## Client-version sensitivity

Native addresses, vtables, layouts, and calling conventions are reverse-engineered against a specific 32-bit client family. A different executable build may require revalidation before hooks are safe.
