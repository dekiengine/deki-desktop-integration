#pragma once

// Desktop package: the Desktop platform and its builder. A game
// built as a desktop program starts at the engine's own main()
// (deki-engine-hosted), which sets up the memory and the file system.
// Display, input and time come from the deki-sdl3-integration package.

// DLL export macro
#ifdef _WIN32
#ifdef DEKI_DESKTOP_HAL_EXPORTS
#define DEKI_DESKTOP_HAL_API __declspec(dllexport)
#else
#define DEKI_DESKTOP_HAL_API __declspec(dllimport)
#endif
#else
#define DEKI_DESKTOP_HAL_API __attribute__((visibility("default")))
#endif
