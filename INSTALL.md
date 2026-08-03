# Building

## Windows: bootstrap script

`scripts/bootstrap.ps1` provisions every prerequisite below and builds:

```
.\scripts\bootstrap.ps1
```

It locates MSYS2 (`$env:MSYS2_ROOT`, `C:\msys64`, `C:\msys2`, or the uninstall
registry), installs the ucrt64 packages, syncs the OpenOCD submodules, writes a
`CMakeUserPresets.json` pinned to the detected toolchain, then configures and
builds. Re-running is safe. It never modifies your persistent `PATH`.

MSYS2 itself is not installed for you — if it's missing, the script says so and
points at `winget install --id MSYS2.MSYS2`.

Useful switches: `-Clean` (wipe `build/` first), `-Package` (run `cpack`
afterwards), `-SkipBuild` (provision only), `-BuildType Debug`,
`-Msys2Root D:\msys64`.

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

  **Windows, via MSYS2 ucrt64:**
  ```
  pacman -S mingw-w64-ucrt-x86_64-{clang,lldb,llvm,cmake,ninja,pkgconf}
  ```
  Note `mingw-w64-ucrt-x86_64-llvm` specifically: `llvm-libs` and `llvm-tools`
  alone are not enough. The headers, the static component libraries the
  disassembler links, and `lib/cmake/llvm/LLVMConfig.cmake` all live in the
  `llvm` package, and without it `find_package(LLVM CONFIG)` fails with
  `LLVM_DIR-NOTFOUND`.

- Xerces-C dev package — linked by `device_provider`'s `device_xml` sublibrary
  (`modules/providers/device_provider/xml/`), which parses CMSIS-SVD files via
  CodeSynthesis XSD-generated C++/Tree bindings. Found via CMake's bundled
  `FindXercesC` module.

  **Ubuntu / Debian:**
  ```
  sudo apt install libxerces-c-dev
  ```

  **Windows, via MSYS2 ucrt64:**
  ```
  pacman -S mingw-w64-ucrt-x86_64-xerces-c
  ```

- CodeSynthesis **libxsd 4.2.0** headers. This is a build dependency, not just a
  regeneration one: the committed bindings include `<xsd/cxx/*.hxx>` and pin
  `LIBXSD_VERSION` to 4.2.0 exactly, so any other version fails to compile with
  `#error XSD runtime version mismatch`. The library is header-only.

  CMake looks for a system copy first and falls back to fetching the official
  tarball, so on Windows — where MSYS2 has no `xsd` package at all — nothing is
  required of you. Set `XSD_INCLUDE_DIR` to override the lookup.

  **Ubuntu / Debian:**
  ```
  sudo apt install libxsd-dev
  ```

- libusb-1.0, hidapi and libftdi — required, not optional. OpenOCD's default
  driver set enables CMSIS-DAP, FTDI, ST-Link and J-Link, and a missing
  dependency for an enabled driver is a hard configure error.

  **Ubuntu / Debian:**
  ```
  sudo apt install libusb-1.0-0-dev libhidapi-dev libftdi1-dev
  ```

  **Windows, via MSYS2 ucrt64:**
  ```
  pacman -S mingw-w64-ucrt-x86_64-{libusb,hidapi,libftdi}
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

On Windows, `scripts/bootstrap.ps1` writes a `windows-local` preset that pins
the compilers, `ninja` and `CMAKE_PREFIX_PATH` to the MSYS2 installation it
found, so `cmake --preset windows-local` is reproducible outside the script.

`-DTRAILER_BUILD_TESTS=OFF` skips the test executables, including the ones that
need real hardware. Packaging builds should use it.

## Packaging

```
cmake --preset windows-local -DCMAKE_BUILD_TYPE=Release -DTRAILER_BUILD_TESTS=OFF
cmake --build build
cd build && cpack -C Release
```

or just `.\scripts\bootstrap.ps1 -Package`. This produces an NSIS installer and
a portable archive next to each other in `build/`.

The installed tree is a self-contained flat directory, which is what
`device_provider`'s `ResourcesRoot()` requires — it resolves resources relative
to the executable, so nothing may be split off into a separate prefix:

```
<install>/
  trailer-dap.exe
  *.dll                 16 runtime dependencies, resolved by CMake
  resources/
    device_index.tsv
    XML/Cores/          per-architecture SVDs
    XML/Rzone/          CubeMX memory maps
  openocd/scripts/      OpenOCD's tcl board/target/interface library
  lib/python3.14/       Python standard library (see below)
```

Runtime DLLs are discovered with `install(RUNTIME_DEPENDENCY_SET)`, which walks
the import table rather than relying on a hand-maintained list.

The Python standard library has to be installed separately, and this is not
optional: MSYS2's `liblldb` initialises Python during
`SBDebugger::Initialize()`, so a tree with `libpython3.14.dll` but no standard
library aborts at startup with `ModuleNotFoundError: No module named
'encodings'`. Being data rather than an import, it is invisible to the
dependency walk. CPython's own `test/` package (145 MB), `idlelib`,
`ensurepip`, `pydoc_data` and `__pycache__` are excluded, taking it from
228 MB to 38 MB.

Sizes: about 302 MB installed, a 62 MB installer and a 96 MB archive.

Everything shipped is scoped to a `runtime` install component, because OpenCSD
installs its headers and static libraries unconditionally and they would
otherwise be packaged too. Note that component mode drops the archive's
top-level directory, so the `.zip` unpacks into the current directory — extract
it into a directory you have already created.

The installer does not modify `PATH`, by design — the VS Code extension requires
an explicit `trailer.executable-path` and has no auto-discovery. Point it at
`<install>/trailer-dap.exe`, and point a launch configuration's
`openocd.scriptSearchDirs` at `<install>/openocd/scripts`.

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
