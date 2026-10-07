# Contributing to AC Customs

Thank you for helping maintain AC Customs. The project combines ordinary application code with address-sensitive reverse engineering, so changes should be easy to reproduce and easy to audit.

## Before opening a pull request

1. Build the affected component as x86 using the scripts under `scripts/`.
2. Keep generated files out of the commit (`artifacts/`, `bin/`, `obj/`, snapshots, logs, `.res`, etc.).
3. Do not commit game binaries, DAT files, extracted original game textures, IDA/Ghidra databases, or Decal binaries.
4. Do not add machine-specific absolute development paths.
5. If changing a client address, offset, vtable, or hook signature, update `docs/REVERSE_ENGINEERING.md` with the evidence and failure mode.
6. If changing the Live Mirror protocol or snapshot schema, update `docs/LIVE_MIRROR.md` and preserve backward compatibility where practical.

## Coding expectations

- Treat x86 pointer width and calling conventions as part of the ABI.
- Prefer bounded reads and explicit validation around reverse-engineered memory.
- Keep hook bodies small and avoid blocking work on the game's render/UI thread.
- Keep theme Apply/Restore transactional: do not overwrite the only known Vanilla bytes.
- Prefer `%LOCALAPPDATA%\ACCustoms` or executable-relative paths over source-tree paths.
- Explain *why* a non-obvious constant exists, not just what the next line does.

## Bug reports

Useful reports include:

- AC client build/version if known.
- AC Customs build/commit.
- Plugin vs Manager vs Live Mirror subsystem.
- Whether the UI was Vanilla or a custom pack was active.
- Reproduction steps.
- Relevant log lines or a minimal snapshot when safe to share.

For crashes or incorrect memory behavior, note whether the problem reproduces without Developer Tests enabled.
