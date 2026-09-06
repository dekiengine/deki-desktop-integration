#pragma once

/**
 * @file DesktopHALPackage.h
 * @brief Central header for the Deki Desktop HAL Package
 *
 * Desktop platform host: provides the program entry (main) and brings up the
 * desktop memory + filesystem HAL before the engine initializes (see
 * DesktopHALPackage.cpp, guarded by SIMULATOR). Display/input/time are supplied
 * by the separate deki-sdl3-integration package.
 */

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
