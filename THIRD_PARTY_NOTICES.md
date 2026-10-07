# Third-party notices

AC Customs vendors a minimal subset of two upstream libraries so a source checkout can be built without Git submodules.

## Dear ImGui

- Upstream: https://github.com/ocornut/imgui
- Vendored revision: `b48d1afbe8ee8b238e2961dc363a949dd7304e23` (`v1.92.9b-docking` in the source checkout used to prepare this repository)
- License: MIT
- License text: `third_party/imgui/LICENSE.txt`

Only the core files and Win32/DX11 backends required by the AC Customs Manager are vendored.

## MinHook

- Upstream: https://github.com/TsudaKageyu/minhook
- Vendored revision: `8af6b4acae5a9388fd742b56fa79ece89d96f823`
- License: BSD-style license, with HDE notices included upstream
- License text: `third_party/minhook/LICENSE.txt`

Only the x86 files required by the native runtime build are vendored. The public tree was populated from the recorded Git revision rather than from the dirty working copy found in the development archive.

## External, not redistributed

The following are build/runtime dependencies but are not part of this repository:

- Decal / `Decal.Adapter.dll`
- Asheron's Call client executables and DAT files
- Microsoft Visual Studio, MSVC, Windows SDK, and .NET Framework tooling

Their licenses and redistribution terms are independent of AC Customs.
