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

## VVS interface migration — required Windows checks

Windows compilation and initial in-game testing passed. Use the following checks for release validation:

- Build Release x86 against installed Decal and VVS assemblies.
- With VVS enabled and Decal Disable View Rendering checked, open AC Customs from the VVS bar and verify all labels, the selector, and all three buttons are visible and usable.
- Repeat with Disable View Rendering unchecked; only one AC Customs window should exist.
- Check long pack names, status wrapping, light/dark VVS themes, and the custom icon.
- Test no installed packs, import cancellation, import success, overwrite cancellation, and replacing an active pack.
- Confirm selecting a pack does not apply it; confirm Apply, cooldown, pack A to B, and Restore Default.
- Restart AC: vanilla remains active and the previous pack selection is remembered.
- Log out/in and unload/reload: no duplicate events, stale windows, or crashes.
- Test VVS starting after AC Customs, and missing/disabled VVS; installation errors must be understandable.

## v1.5.0.0 plugin update — final Windows checks

- [ ] Confirm `AssemblyVersion`, `AssemblyFileVersion`, and the Win32 VERSIONINFO resource all report `1.5.0.0`.
- [ ] Rebuild **both** `ACCustoms.Native.dll` and `ACCustoms.dll` from the updated sources; do not reuse any previous `artifacts/plugin` DLL.
- [ ] Launch AC through Decal with VVS enabled; verify the plugin icon/window and all notebook tabs display correctly.
- [ ] With Auto Apply OFF, log into a character and confirm the Classic UI remains untouched.
- [ ] Manually Apply a PNG theme; confirm the progress bar fills smoothly then disappears and texture switching stays responsive.
- [ ] Restore Classic UI, switch between two themes and relog; confirm no leftover/missing textures.
- [ ] Enable Auto Apply, relog and restart AC; confirm it applies the *last successfully applied* theme after `LoginComplete` (no intentional delay).
- [ ] Close AC by Alt+F4 and the window X after a completed swap; confirm Auto Apply remains enabled on next launch.
- [ ] Confirm an interrupted Auto Apply queue disables Auto Apply on the next launch, preventing a repeated crash loop.
- [ ] Run `scripts\publish_plugin_release.ps1 -ReleaseVersion 1.5.0.0` and inspect generated SHA-256 hashes, two DLLs and two-file ZIP.
- [ ] Commit/push `updates/latest.json`, `updates/releases/v1.5.0.0/` and `zip download/ACCustoms_Plugin_v1.5.0.0.zip` **together**, after tests.
- [ ] Install v1.4.0.0 in a separate test environment, use the in-plugin updater, exit AC and confirm both installed DLLs update to the new release.
