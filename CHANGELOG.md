# Changelog

Notable changes to `deki-desktop-integration`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Added
- A **Web** platform: builds a project for the browser with Emscripten, and
  opens it from the Build panel. The Build panel installs Emscripten (about
  2 GB). WebGL 2, no threads, and the game is shown at its own resolution.

### Fixed
- Choosing Native in the New Platform dialog no longer crashes the panel: the package now takes the editor's ImGui context.
- macOS: a game built as a program links the SDL3 the editor built with clang.
  Building SDL3 with GCC stopped at its Objective-C.

## 0.30.2

### Fixed
- A Desktop build uses the tools the editor builds everything with: the
  toolchain its download carries (GCC, CMake, Ninja), else the computer's.
  A computer with no compiler of its own builds, and Linux needs no Make. A
  build folder configured with other tools is configured afresh, and the
  build says so.

### Changed
- `minEngine` 0.30.2: it asks the editor for its toolchain
  (`<deki-editor/BundledToolchain.h>`, new in 0.30.2).

## 0.30.0

### Changed
- `minEngine` 0.30.0: built against engine 0.30.0. A build of this package
  from before does not load and is rebuilt.
- **The platform is called Desktop** (it was "Desktop Simulator (SDL3)"): it
  builds the PC version of a game for Windows, Linux or macOS, and simulates
  no board; the editor's play mode is where a game runs while it is made. Its
  id stays `native_simulator`, so a project keeps its target; a project's own
  copy of the platform takes the new name on **Update**.
- **`platformRequires` is `hosted`** (it was `desktop-os`), and the
  `native_simulator` platform provides `hosted` and no longer lists the
  `SIMULATOR` define: the build defines `DEKI_HOSTED` itself. Needs the editor
  release that names the capability `hosted`.
- **`main()`, the memory and the file system are the engine's now**
  (`deki-engine-hosted`: `src/hosted/HostedMain.cpp`, and
  `Deki::HostFileSystem` for `DesktopFileSystem`). Any target that runs a game
  as a program uses them without this package, the Miyoo Mini among them;
  this package keeps the desktop simulator platform and its builder. Needs
  the editor and engine release that adds `deki-engine-hosted`: with an older
  one a desktop game has no `main()`, and with this package's older version
  a newer engine's game has two.

### Changed
- The desktop game's CMake is written by the editor's
  `CMakeGen::GenerateExecutableCMakeLists`, which other targets built as one
  program (the Miyoo Mini, in deki-miyoo-boards) share; this package gives it
  SDL3. The file it writes is the same as before. Needs the editor release
  that adds `ExecutableCMakeGen.h`; raise `minEngine` to it when releasing.

## 0.22.0

### Added
- Desktop games on Windows have the Deki cat as their icon. A package's
  `resources/windows/*.rc` files are compiled into a Windows game; this
  package's gives the icon.

### Changed
- Desktop games no longer link OpenGL: the SDL3 package draws with SDL's GPU
  API.
- Desktop builds run at below-normal priority and with as many compile jobs
  as free memory holds (needs editor 0.22).
- `minEngine` 0.22.0: built against engine 0.22.0. A build of this package
  from before does not load and is rebuilt.

## 0.21.0

### Changed
- `F:/` works as on a board: the game's data (`./flash/`) is read and never
  written, and files the game writes under `F:/` go to `./userdata/`, read from
  there when the game's data has no file of that name. Before, a game could
  overwrite its own exported files.

## 0.19.0

### Changed
- `minEngine` 0.19.0: built against engine 0.19.0, whose component metadata records the package each class is in. A build of this package from before does not load and is rebuilt.

### Added
- Japanese, Simplified Chinese and Korean: `editor/lang/ja.json`, `zh.json`
  and `ko.json` translate the names, tooltips and enum values this
  package's components show in the Inspector.
- The package's editor text goes through the editor's translation (`Tr`), so
  it shows in the editor's language. Needs the editor release that adds
  `Localization.h`; raise `minEngine` to it when releasing.

## 0.18.0

### Changed
- `minEngine` 0.18.0. Reflection ABI 21: the package must be rebuilt.
- The boot scenes are saved in the current scene format, as the editor writes
  them, so an adopted board matches its package copy.
- The desktop board template keeps `displayBus` and `displayDriver` in
  `frameworkOptions`, as the editor saves them.
- **Names follow the code style** (deki-engine/docs/codestyle): types, functions and enum values are PascalCase, constants kPascalCase, members m_PascalCase, locals and parameters camelCase. The code is formatted with clang-format 22.
- The functions the editor finds by name are PascalCase: DekiDesktopHALRegisterComponents, DekiDesktopHALGetAutoComponentCount, DekiDesktopHALEnsureRegistered and the rest. Built against engine ABI 21; a build of this package from before does not load and is rebuilt.

### Fixed
- A build picks up package source files added after it was first configured.
- A failed build on Linux or macOS reports the build's exit code. It reported
  the raw wait status, so a build that exited 2 said "exit code 512".

## 0.17.0

### Changed
- `minEngine` 0.17.0. Reflection ABI 20: the package must be rebuilt.
- The simulator reads the game's assets from `flash/assets/` (`F:/assets/`),
  which the build fills; `--export` is no longer needed first, and nothing is
  copied into `storage/`, which is the game's own to write.
- The simulator build defines `DEKI_SCREEN_COLOR_FORMAT` (the platform's
  colorFormat) for the SDL window, instead of a format for the engine.
- The simulator's boot scene sets only the window scale; the screen is the
  platform's.
- The desktop simulator's platform (`platforms/native_simulator/`, config and
  boot scene) is offered by this package; the editor no longer carries it. A
  new project adopts it on first open.
- The generated simulator project passes the engine its screen size, colour
  format and `DEKI_FAST_ATTR` from the platform. The engine used to assume
  320x240 RGB565 for any simulator.
- The deploy step (builder ABI 2) is "Run", with nothing to choose; it used to
  be handed a serial port and ignore it.
- Native dependencies (`dependencies.native`: version, git, per-host prebuilt
  archive) are parsed by this backend from the declaration as written.

## 0.16.0

### Fixed
- **`main` stays at global scope, so a simulator build has an entry point.**
  The namespace move took the program's `main` with it. The C++ runtime looks
  for `::main` and nothing else will do, so a desktop simulator or firmware
  binary failed to link, reporting an undefined `WinMain` — an error pointing
  nowhere near the cause.

### Changed
- **Moved into the `DekiDesktop` namespace.** Every component was declared at global
  scope, which made its identity a bare class name — the name a scene file
  stores and the name the registry keys on — so two packages defining one name
  collided there with nothing to tell them apart. Each component carries
  `DEKI_FORMER_NAME` with the name it was saved under before, so existing
  scenes load unchanged and are written back qualified on the next save.
  Code naming these types needs the namespace: `using namespace DekiDesktop;` or a
  qualified name.
- Enum properties are stored by name rather than by number, so appending to an
  enum or reordering one no longer changes what a saved scene means. Files
  written before this still read.
- `minEngine` 0.16.0. Reflection ABI 17: the package must be rebuilt.

## 0.15.0

### Changed
- Allocates through the engine with an explicit region
  (`Deki::Memory::Internal` / `Deki::Memory::External`) instead of `new[]` and
  `malloc`. The old `MemoryUse` enum is gone.
