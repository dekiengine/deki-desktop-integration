#pragma once

// Desktop HAL package. Provides the program entry (main) and sets up the
// desktop memory and filesystem providers before the engine starts (see
// DesktopHALPackage.cpp, under SIMULATOR). Display, input and time come from
// the deki-sdl3-integration package.

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
