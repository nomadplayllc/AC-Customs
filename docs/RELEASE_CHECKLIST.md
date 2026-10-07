# First public release checklist

Use this before making the repository public or attaching binaries to a GitHub Release.

## Source/legal review

- [ ] Confirm `plugin/managed/ACCustoms.ico` is project-owned/redistributable.
- [ ] Decide whether `LICENSE` should say `AC Customs contributors` or a specific copyright holder.
- [ ] Do not add `DarkMode_v1.acui` until every texture payload's redistribution rights are confirmed.
- [ ] Confirm no game binaries, DAT files, decompiler databases, or Decal binaries are present.
- [ ] Run a credential/secret scan over the *Git history*, not only the working tree.

## Clean build

On a Windows machine from a fresh clone:

```bat
scripts\build_all.cmd
```

Verify:

- [ ] `artifacts\plugin\ACCustoms.dll` builds.
- [ ] `artifacts\plugin\ACCustoms.Native.dll` builds.
- [ ] `artifacts\manager\ACModernUIManager.exe` builds.
- [ ] `artifacts\manager\ACModernUIConverter.exe` builds and sits beside the Manager.
- [ ] `artifacts\manager\Defaults` contains both default text files.
- [ ] Snapshot viewer builds.

## Runtime smoke test

- [ ] Fresh AC process starts Vanilla.
- [ ] Plugin loads through Decal.
- [ ] Apply a known-good pack; Restore Vanilla.
- [ ] Switch pack A -> pack B -> Vanilla.
- [ ] Manager finds/opens `client_portal.dat` and previews textures directly from DAT.
- [ ] Replace PNG creates `%LOCALAPPDATA%\ACCustoms\User\textures\<DID>.rgb` without `dat_textures.csv`.
- [ ] Live UI Editor connects and ordinary file-backed textures remain selectable/replaceable.
- [ ] Known generated/DID-less limitations are still accurately documented.

## GitHub setup

Recommended repository settings:

- [ ] Public repository named `AC-Customs` (or chosen canonical name).
- [ ] Description and project-site URL set.
- [ ] Issues enabled.
- [ ] Default branch protection once collaborative development begins.
- [ ] Add topics such as `asherons-call`, `decal`, `ui-modding`, `cpp`, `csharp` as appropriate.

## Release packaging

Keep source control and release binaries separate. A release package for end users should contain only what users need, while GitHub automatically provides source archives for each tag.

Do not commit release EXEs/DLLs into the repository merely to make them downloadable; attach them to GitHub Releases instead.
