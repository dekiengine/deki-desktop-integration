#include "DesktopMemorySetup.h"
#include "DekiLogSystem.h"

#if defined(DEKI_MODULE_DESKTOP_HAL)

#include "platforms/desktop/DesktopMemoryProvider.h"
#include "providers/DekiMemoryProvider.h"

void DesktopMemorySetup::Setup(SetupCallback onComplete)
{
    DEKI_LOG_INFO("DesktopMemorySetup: Initializing desktop memory backend");

    DekiMemoryProvider::SetBackend(new DesktopMemoryProvider());
    if (DekiMemoryProvider::Initialize())
    {
        DEKI_LOG_INFO("DesktopMemorySetup: Memory backend initialized successfully");
        onComplete(true);
    }
    else
    {
        DEKI_LOG_ERROR("DesktopMemorySetup: Failed to initialize memory backend");
        onComplete(false);
    }
}

#else

void DesktopMemorySetup::Setup(SetupCallback onComplete)
{
    // ESP32: Desktop memory backend not applicable
    onComplete(true);
}

#endif
