# Changelog

Notable changes to `deki-desktop-integration`. Engine and editor changes are in the
[engine changelog](https://github.com/dekiengine/deki-engine/blob/master/CHANGELOG.md).

A package's `minEngine` names the engine version it needs. Before 1.0 a
breaking change bumps the minor across the editor, the engine and every
package together, so a package with no changes of its own is still released
alongside one that has them.

## Unreleased

### Changed
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
