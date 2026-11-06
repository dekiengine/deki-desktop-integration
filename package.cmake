# Package descriptor for deki-engine auto-discovery
set(PACKAGE_DISPLAY_NAME "Desktop HAL")
set(PACKAGE_PREFIX "DekiDesktopHAL")
set(PACKAGE_UPPER "DESKTOP_HAL")
set(PACKAGE_TARGET "deki-desktop-hal")
set(PACKAGE_FILE_PREFIX "DesktopHAL")
# No runtime code of its own: a desktop game's main(), memory and file system
# are the engine's (deki-engine-hosted). The entry is the plugin's exports.
set(PACKAGE_ENTRY DesktopHALPackage.cpp)
