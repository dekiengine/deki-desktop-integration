# Module descriptor for deki-engine auto-discovery
set(MODULE_DISPLAY_NAME "Desktop HAL")
set(MODULE_PREFIX "DekiDesktopHAL")
set(MODULE_UPPER "DESKTOP_HAL")
set(MODULE_TARGET "deki-desktop-hal")
set(MODULE_FILE_PREFIX "DesktopHAL")
set(MODULE_SOURCES
    DesktopMemorySetup.cpp
    DesktopFileSystemSetup.cpp
)
set(MODULE_ENTRY DesktopHALModule.cpp)
