#include "DesktopFileSystemSetup.h"
#include "DekiLogSystem.h"

#if defined(DEKI_MODULE_DESKTOP_HAL)

#include "platforms/desktop/DesktopFileSystem.h"
#include "providers/DekiFileSystemProvider.h"

void DesktopFileSystemSetup::Setup(SetupCallback onComplete)
{
    DEKI_LOG_INFO("DesktopFileSystemSetup: Initializing desktop file system (data='%s', storage='%s')",
                  data_path.c_str(), storage_path.c_str());

    auto* fs = new DesktopFileSystem();
    if (DekiFileSystemProvider::SetFileSystem(fs))
    {
        DekiFileSystemProvider::RegisterFileSystem("F:/", fs);
        DekiFileSystemProvider::SetDefaultFileSystem(fs);
        DEKI_LOG_INFO("DesktopFileSystemSetup: File system initialized successfully");
        onComplete(true);
    }
    else
    {
        DEKI_LOG_ERROR("DesktopFileSystemSetup: Failed to initialize file system");
        onComplete(false);
    }
}

#else

void DesktopFileSystemSetup::Setup(SetupCallback onComplete)
{
    // ESP32: Desktop file system not applicable
    onComplete(true);
}

#endif
