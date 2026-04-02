#pragma once

/**
 * @file DesktopHALModule.h
 * @brief Central header for the Deki Desktop HAL Module
 *
 * This module provides desktop-specific SetupComponents for device/simulator builds:
 * - DesktopMemorySetup (configures desktop memory backend)
 * - DesktopFileSystemSetup (configures desktop file system)
 *
 * For the editor, these backends are initialized directly (built-in).
 * This module provides SetupComponent wrappers for boot.prefab-driven initialization.
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
