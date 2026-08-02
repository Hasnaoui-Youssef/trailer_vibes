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

- Xerces-C dev package — linked by `device_provider`'s `device_xml` sublibrary
  (`modules/providers/device_provider/xml/`), which parses CMSIS-SVD files via
  CodeSynthesis XSD-generated C++/Tree bindings. Found via CMake's bundled
  `FindXercesC` module.

  **Ubuntu / Debian:**
  ```
  sudo apt install libxerces-c-dev
  ```

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

## Regenerating the CMSIS-SVD bindings

`modules/providers/device_provider/xml/generated/CMSIS_SVD.{hxx,cxx}` are
generated once by hand from `resources/Schemas/CMSIS_SVD.xsd` and committed —
this is not a build step, and CodeSynthesis XSD is not a build dependency.
Only regenerate when that schema changes:

```
xsd cxx-tree --std c++11 \
    --hxx-suffix .hxx --cxx-suffix .cxx \
    --output-dir modules/providers/device_provider/xml/generated/ \
    resources/Schemas/CMSIS_SVD.xsd
```

Requires CodeSynthesis XSD (tested with 4.2.0) on `PATH`. The generator
stamps a version check into every file it produces — it must match whatever
`xsd/cxx` runtime headers the `xsd` binary you used was built against, or
generated code fails to compile with `#error XSD runtime version mismatch`.
`PACK.xsd` and `RZONE.xsd` are not generated — the PDSC and CubeMX Rzone
files are small and hand-parsed with pugixml instead (see
`modules/providers/device_provider/src/rzone_parser.cc`).
