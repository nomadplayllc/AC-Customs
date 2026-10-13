# Automatic Plugin Releases

`latest.json` is intentionally **not** committed until the DLLs are built and
published. The plugin checks this manifest in the public `main` branch:

`https://raw.githubusercontent.com/nomadplayllc/AC-Customs/main/updates/latest.json`

Run on Windows (Visual Studio with C++ build tools, Decal, VVS, .NET Framework 4.8):

1. `scripts\build_native.cmd`
2. `scripts\build_managed.cmd`
3. `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\publish_plugin_release.ps1`
4. Review and **commit all** generated files: `updates/latest.json` and both DLLs
   under `updates/releases/v1.4.0.0/`. Push them to the `main` branch.

The publishing script reads `ACCustoms.dll`'s real assembly version and
computes SHA-256 and byte lengths. Use `-Notes @('Note one','Note two')` to
specify changelog items. Never publish a manifest without the matching DLLs.

## Update lifecycle

- On login, the managed plugin checks GitHub on a worker thread. A new **Update**
  notebook page appears only if the published version exceeds the installed
  managed assembly version. **Mapping Update** stays separate and unchanged.
- Clicking **Update Plugin** fetches and validates both DLLs into
  `%LOCALAPPDATA%\ACCustoms\PluginUpdates\v<version>`.
- It extracts an embedded `ACCustoms.Updater.exe` to AppData and starts it.
  Only the two DLLs are needed in the normal user-facing plugin package.
- The helper waits until the current `acclient.exe` process exits, then swaps
  the DLLs with per-file backups and verifies both installed checksums.
  It retries when other AC clients keep the files locked. No relog/download is
  needed before the initial click; changes **take effect on next launch**.
- The helper logs to `%LOCALAPPDATA%\ACCustoms\plugin-updater.log`.

The helper is independent of the game/Decal. If the plugin folder is
write-protected, an OS UAC elevation prompt may appear immediately when the
user clicks Update. An approval is required in such cases. In that
case the staged update remains available and the log records the failure.
Do not try to overwrite loaded DLLs: Windows may prevent it.

## Bootstrap requirement

An older `ACCustoms.dll` **does not contain the updater** and cannot update
itself. Existing users must install the first updater-enabled release once
using their current manual install method. All subsequent releases can use
in-plugin updates. The installed managed DLL version must advance for every
release, and the native DLL must be published alongside it.
