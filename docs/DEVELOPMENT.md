# Development guidelines

## Keep production and probes separate

Temporary instrumentation is useful, but avoid accumulating `Foo_PreFix.cpp`, `Foo_WorkingBackup.cpp`, and compiled test variants in the repository. Use Git branches/commits for source history. If a probe produces a durable discovery, document the conclusion under `docs/` and remove the one-off artifact.

## Paths

Supported production code must not depend on a developer workspace such as `C:\ACModernUI-Dev` or `C:\ACCustoms_Decal_POC`.

Use one of:

- `%LOCALAPPDATA%\ACCustoms` for mutable per-user/runtime data.
- executable-relative paths for shipped companion files.
- repository-relative paths in build scripts.
- discovered external dependency paths (Decal, Visual Studio, Windows SDK).

## Source vs generated data

Commit code and stable defaults. Do not commit:

- generated DAT catalogs/previews;
- runtime snapshots/captures;
- compiled binaries/intermediates;
- user replacement workspaces;
- local application INI state.

## Reverse-engineered code

Address-dependent code should be easy to audit. Prefer named constants and comments that include the evidence/context. When a client structure is only partially understood, name unknown fields conservatively rather than inventing semantics.

## Native failure isolation

Experimental hooks should not take down the stable theme engine. Optional capture/state/provenance features should fail closed and report diagnostics while leaving Apply/Restore usable whenever possible.

## Testing changes

For texture/runtime changes, test at minimum:

1. Fresh process starts Vanilla.
2. Apply one pack.
3. Restore Vanilla.
4. Apply pack A, switch to pack B, restore.
5. Lazy-loaded textures do not restore from nonexistent/incorrect originals.
6. Manager replacement creation writes to the local user workspace.
7. Live Mirror still reports bridge, capture, and scene-load failures distinctly.

For generated/DID-less work, separately test inventory/vendor item imagery and ordinary file-backed controls so a fix for one path does not regress the other.
