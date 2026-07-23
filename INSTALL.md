# Building

## Prerequisites

- Clang and Ninja (required; see `CLAUDE.md`).
- LLVM, Clang, and LLDB dev packages, version 22 — headers + libraries only,
  not built from source. The DAP layer (`modules/dap`) links `liblldb`
  directly and forks source from LLVM's `lldb-dap` tool, so the installed
  version should match `TRAILER_LLVM_VERSION` in the root `CMakeLists.txt`.

  **Ubuntu / Debian (apt.llvm.org):**
  ```
  sudo apt install llvm-22-dev liblldb-22-dev clang-22
  ```

  **Windows, via MSYS2 ucrt64:** install the matching `mingw-w64-ucrt-x86_64-*`
  LLVM/Clang/LLDB packages from the ucrt64 repo. Not yet verified end-to-end;
  if `find_package(LLVM CONFIG)` doesn't find it unhinted, add an `LLVM_DIR`
  entry to the `windows-msys2-ucrt64` preset in `CMakePresets.json`.

## Configuring and building

```
cmake --preset <linux|windows-msys2-ucrt64>
cmake --build build
```

`CMakePresets.json` holds the per-platform `LLVM_DIR` (needed on Debian/Ubuntu,
which installs multiple LLVM versions side by side under
`/usr/lib/llvm-<N>/`, so it isn't on CMake's default search path). Add a
`CMakeUserPresets.json` (gitignored) for machine-specific overrides instead of
editing the checked-in file.

Plain `cmake -S . -B build` (no preset) also works wherever LLVM is already on
CMake's default search path with nothing else to disambiguate.
