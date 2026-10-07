# Building AC Customs

AC Customs targets the legacy 32-bit Asheron's Call/Decal environment. Build all native components as **x86**.

## Prerequisites

Install on Windows:

1. Visual Studio 2022 (Community is sufficient).
2. **Desktop development with C++** workload.
3. A Windows 10/11 SDK that provides `rc.exe`.
4. .NET Framework 4.8 developer/targeting tools.
5. Decal 3.0 for `Decal.Adapter.dll` when compiling the managed plugin.
6. Virindi Plugin Bundle for `VirindiViewService.dll`.

The game client/DAT is not needed merely to compile the code, but is required to exercise the Manager and runtime.

## Build everything

From a normal Command Prompt at the repository root:

```bat
scripts\build_all.cmd
```

The scripts locate Visual Studio through `vswhere.exe`, initialize an x86 MSVC environment, and place final outputs under `artifacts\`.

## Native plugin runtime

```bat
scripts\build_native.cmd
```

Inputs:

- `plugin\native\ACCustoms.Native.cpp`
- `third_party\minhook\...`

Output:

```text
artifacts\plugin\ACCustoms.Native.dll
```

MinHook is compiled from vendored x86 source; no external MinHook installation is required.

## Managed Decal plugin

```bat
scripts\build_managed.cmd
```

The script:

1. Locates MSBuild.
2. Locates `rc.exe` and generates `plugin\managed\ACCustoms.res` from the checked-in `.rc` + `.ico` sources.
3. Locates the developer's installed `Decal.Adapter.dll` and `VirindiViewService.dll`.
4. Builds `Release|x86` against .NET Framework 4.8.
5. Copies the resulting assembly to `artifacts\plugin`.

`Decal.Adapter.dll` and `VirindiViewService.dll` are referenced with `Private=False`; neither is copied into AC Customs artifacts.

For a nonstandard VVS installation, set `VVS_DIR` to the directory containing `VirindiViewService.dll` before running the script. For direct MSBuild invocation, pass `/p:VvsDir="path"` alongside `/p:DecalDir="path"`.

The script caches the locally discovered Decal adapter path in `.build_decal_adapter_path.txt`, which is gitignored.

## UI Manager

The Manager and converter are separate executables. Both are required for replacement editing.

```bat
scripts\build_converter.cmd
scripts\build_manager.cmd
```

Outputs:

```text
artifacts\manager\ACModernUIManager.exe
artifacts\manager\ACModernUIConverter.exe
artifacts\manager\Defaults\custom_tabs.txt
artifacts\manager\Defaults\texture_notes.txt
```

Keep the converter and `Defaults` directory next to the Manager executable when packaging a release.

## Plugin registration

After a successful build:

```bat
scripts\register_plugin.cmd
```

This invokes the **32-bit** .NET Framework `RegAsm.exe` on `artifacts\plugin\ACCustoms.dll`. Then enable AC Customs in Decal's plugin manager.

## Common build failures

### `Decal.Adapter.dll could not be found`

Install Decal 3.0 or place it in a normal installed location. The project intentionally does not vendor Decal assemblies.

### `rc.exe was not found`

Add a Windows SDK through Visual Studio Installer.

### x64/architecture problems

Do not substitute an x64 build. The target process, hook addresses, pointer assumptions, and managed registration path are for the 32-bit environment.

### Manager builds but replacement conversion fails

Verify that `ACModernUIConverter.exe` is beside `ACModernUIManager.exe`. The supported public workflow no longer needs `dat_textures.csv`; the Manager passes texture metadata directly to the converter.
