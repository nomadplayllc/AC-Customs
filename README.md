# AC Customs

AC Customs is an open-source UI customization system for the 32-bit Asheron's Call client. It combines a Decal plugin, a native texture/runtime hook layer, and a standalone UI Manager for browsing client textures, building `.acui` packs, and editing the interface with a live snapshot/mirror workflow.

Project site: https://ac-customs.mrgrid.chatgpt.site/

> **Development status:** AC Customs is under active development. The normal file-backed texture replacement path is the most mature. Live Mirror support for generated/DID-less runtime imagery (notably some item icons) and automatic interactive-state relationships remains experimental; see [Known limitations](docs/KNOWN_LIMITATIONS.md).

## Repository layout

```text
plugin/
  managed/             Decal-facing .NET Framework 4.8 plugin
  native/              x86 native runtime hooks and Live Mirror capture
manager/                Standalone Win32 UI Manager and PNG converter
third_party/             Vendored build dependencies (licenses preserved)
docs/                    Architecture, build, format, and RE notes
scripts/                 Reproducible Windows build scripts
artifacts/               Generated build output (gitignored)
```

## What AC Customs does

- Applies and restores UI texture replacement packs at runtime.
- Imports/exports the dependency-free `.acui` pack format.
- Reads texture metadata and preview pixels directly from `client_portal.dat` in the current Manager.
- Stores user/runtime data under `%LOCALAPPDATA%\ACCustoms` rather than in the source tree.
- Captures and replays the live UI scene through a local named-pipe bridge.
- Preserves reverse-engineered client addresses/offsets in source so the project is maintainable rather than merely compilable.

## Build quick start

Requirements:

- Windows, because the project targets Win32/Decal and uses Windows APIs.
- Visual Studio 2022 with **Desktop development with C++** and a Windows SDK.
- .NET Framework 4.8 developer tooling.
- Decal 3.0 installed to build the managed plugin (`Decal.Adapter.dll` is referenced from the local Decal installation and is not redistributed here).
- A 32-bit Asheron's Call client installation for runtime use and Manager DAT browsing.

From a normal Command Prompt:

```bat
scripts\build_all.cmd
```

Outputs are written to `artifacts\`:

```text
artifacts\plugin\ACCustoms.dll
artifacts\plugin\ACCustoms.Native.dll
artifacts\manager\ACModernUIManager.exe
artifacts\manager\ACModernUIConverter.exe
artifacts\manager\Defaults\...
```

To register a locally built plugin with the 32-bit .NET Framework registration tool:

```bat
scripts\register_plugin.cmd
```

See [docs/BUILDING.md](docs/BUILDING.md) for component-by-component instructions and troubleshooting.

## Runtime data

AC Customs intentionally separates program files from user/generated data. Important locations include:

```text
%LOCALAPPDATA%\ACCustoms\Packs
%LOCALAPPDATA%\ACCustoms\User\textures
%LOCALAPPDATA%\ACCustoms\Runtime\Captured
%LOCALAPPDATA%\ACCustoms\Runtime\Snapshots
%LOCALAPPDATA%\ACCustoms\Runtime\LiveMirror
%LOCALAPPDATA%\ACCustoms\Logs
```

The Manager remembers the selected `client_portal.dat` path under its local application-data directory. It does not require the old development `dat_textures.csv`/preview-folder workflow.

## Architecture and reverse engineering

Start with:

- [Architecture](docs/ARCHITECTURE.md)
- [Live Mirror](docs/LIVE_MIRROR.md)
- [ACUI pack format](docs/ACUI_FORMAT.md)
- [Reverse-engineering notes](docs/REVERSE_ENGINEERING.md)
- [Development guidelines](docs/DEVELOPMENT.md)

The native runtime is address-sensitive. When changing a hook, structure offset, or vtable assumption, document the evidence and supported client build rather than replacing a named/inferred concept with an unexplained magic constant.

## Third-party code

The repository vendors only the source files required for the current build:

- Dear ImGui, MIT licensed.
- MinHook, BSD-style licensed.

Their original license files are preserved under `third_party/`. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Decal, Asheron's Call, and the game client/data files are external dependencies and are **not** part of this repository or covered by the AC Customs MIT license.

## Asset policy

This source repository intentionally does not contain Asheron's Call binaries, DAT files, decompiled client source, or a bundled UI pack whose texture-asset redistribution rights have not been verified. AC Customs operates on a user's locally installed client data.

## Contributing

Bug reports and pull requests are welcome. Because the native layer interacts with a legacy 32-bit client, reproduction details matter: include the AC client build, whether the issue occurs in Vanilla or with a pack active, and any relevant AC Customs logs/snapshots that do not contain private information.

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

AC Customs source code in this repository is licensed under the [MIT License](LICENSE), except for files under `third_party/`, which retain their respective upstream licenses.

AC Customs is an independent community project and is not affiliated with or endorsed by the owners of Asheron's Call or Decal.
