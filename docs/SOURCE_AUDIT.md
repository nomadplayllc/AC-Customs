# Source archive audit

The public tree was prepared from two zipped development directories: the Decal/plugin workspace and the UI Manager workspace. The development archive contained **573 files / ~54.2 MB** before cleanup. The public tree is intentionally much smaller because build output, backup history, complete nested dependency repositories, generated data, and superseded experiments were removed.

## Classification

| Development material | Classification | Public treatment |
|---|---|---|
| Current managed plugin `.cs/.csproj/.xml/.rc/.ico` | PUBLIC | Kept under `plugin/managed` |
| Current `ACCustoms.Native.cpp` | PUBLIC | Kept under `plugin/native`; local fallback paths cleaned up |
| Current `ACModernUIManager.cpp` + `ACModernUIModern.inl` | PUBLIC | Kept under `manager` |
| Current converter | PUBLIC | Kept; supported replacement CLI made DAT-catalog independent |
| Standalone snapshot replay/viewer utility | EXCLUDE | Obsolete development/debug utility; current Live UI Editor handles snapshot rendering internally |
| Default custom groups / texture notes | PUBLIC | Kept under `manager/Defaults` |
| Dear ImGui | PUBLIC / third-party | Reduced to build-required subset; upstream MIT license retained |
| MinHook | PUBLIC / third-party | Reduced to build-required x86 subset; copied from recorded clean Git revision |
| `bin/`, `obj/`, EXE/DLL/PDB/LIB/EXP/RES | EXCLUDE | Generated build products; covered by `.gitignore` |
| `*.bak`, `PreDAT`, `PreTabs`, named intermediate manager builds | EXCLUDE | Historical development states; Git should replace this pattern |
| Probe/test DLL source variants | EXCLUDE | Durable findings consolidated into docs |
| `dat_textures.csv`, preview folders, encountered CSV, logs | EXCLUDE | Generated runtime/development data |
| DAT test utility using hard-coded development paths | EXCLUDE | Superseded by direct DAT reader in current Manager |
| Nested `.git` directories | EXCLUDE | Dependency history is not embedded in the project repository |
| `DarkMode_v1.acui` | NEEDS REVIEW | Omitted until all texture asset redistribution rights are confirmed |
| Project icon (`ACCustoms.ico`) | NEEDS REVIEW | Included because it is a build/resource input; confirm that the artwork is project-owned/redistributable |
| Root MIT copyright holder wording | NEEDS REVIEW | Currently uses `AC Customs contributors`; maintainer may replace with a personal/project legal name |

## Secret/credential scan

A textual scan of the candidate public tree found no obvious API keys, passwords, private keys, or service credentials. Hits for the word `token` were JSON/parser variable names, not authentication tokens.

This is not a substitute for reviewing future commits. Git history can preserve a secret even after the working-tree file is cleaned, so run secret scanning before pushing if any credential-bearing files have ever entered a repository.

## Machine-specific path audit

Supported public paths were normalized as follows:

- Native runtime fallback directories now resolve under `%LOCALAPPDATA%\ACCustoms` instead of `C:\ACModernUI-Dev`.
- Native build uses `third_party\minhook` from the repository.
- Manager replacement conversion receives metadata/path arguments directly and does not require `C:\ACModernUI-Dev\dat_textures.csv`.
- Common *external installation discovery* paths (for example a conventional Asheron's Call or Decal installation) remain as optional search candidates, not source-tree dependencies.
