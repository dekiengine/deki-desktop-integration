# Package descriptor for deki-engine auto-discovery
set(PACKAGE_DISPLAY_NAME "Desktop HAL")
set(PACKAGE_PREFIX "DekiDesktopHAL")
set(PACKAGE_UPPER "DESKTOP_HAL")
set(PACKAGE_TARGET "deki-desktop-hal")
set(PACKAGE_FILE_PREFIX "DesktopHAL")
# Platform-host package: the entry (DesktopHALPackage.cpp) owns main() and brings up
# the desktop memory + filesystem HAL directly, so there are no reflected components.
set(PACKAGE_ENTRY DesktopHALPackage.cpp)
