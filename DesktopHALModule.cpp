/**
 * @file DesktopHALModule.cpp
 * @brief Module entry point for deki-desktop-hal DLL
 *
 * This file exports the standard Deki plugin interface so the editor
 * can load deki-desktop-hal.dll and discover available Desktop HAL components.
 *
 */

#include "DesktopHALModule.h"
#include "interop/DekiPlugin.h"
#include "modules/DekiModuleFeatureMeta.h"
#include "DesktopMemorySetup.h"
#include "DesktopFileSystemSetup.h"
#include "reflection/ComponentRegistry.h"
#include "reflection/ComponentFactory.h"
#include "platforms/desktop/DesktopMemoryProvider.h"
#include "providers/DekiMemoryProvider.h"

// Direct backend registration for desktop
namespace
{
struct DesktopBackendInit {
    DesktopBackendInit() {
        DekiMemoryProvider::SetBackend(new DesktopMemoryProvider());
    }
};
static DesktopBackendInit s_desktop_init;
}

#ifdef DEKI_EDITOR

// Auto-generated registration helpers
extern void DekiDesktopHAL_RegisterComponents();
extern int DekiDesktopHAL_GetAutoComponentCount();
extern const DekiComponentMeta* DekiDesktopHAL_GetAutoComponentMeta(int index);

// Track if already registered to avoid duplicates
static bool s_DesktopHALRegistered = false;

extern "C" {

/**
 * @brief Ensure deki-desktop-hal module is loaded and components are registered
 */
DEKI_DESKTOP_HAL_API int DekiDesktopHAL_EnsureRegistered(void)
{
    if (s_DesktopHALRegistered)
        return DekiDesktopHAL_GetAutoComponentCount();
    s_DesktopHALRegistered = true;

    // Auto-generated: registers all Desktop HAL components with ComponentRegistry + ComponentFactory
    DekiDesktopHAL_RegisterComponents();

    return DekiDesktopHAL_GetAutoComponentCount();
}

// =============================================================================
// Plugin metadata (for dynamic loading compatibility)
// =============================================================================

DEKI_PLUGIN_API const char* DekiPlugin_GetName(void)
{
    return "Deki Desktop HAL Module";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
    return "1.0.0";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetReflectionJson(void)
{
    return "{}";
}

DEKI_PLUGIN_API int DekiPlugin_Init(void)
{
    return 0;
}

DEKI_PLUGIN_API void DekiPlugin_Shutdown(void)
{
    s_DesktopHALRegistered = false;
}

DEKI_PLUGIN_API int DekiPlugin_GetComponentCount(void)
{
    return DekiDesktopHAL_GetAutoComponentCount();
}

DEKI_PLUGIN_API const DekiComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return DekiDesktopHAL_GetAutoComponentMeta(index);
}

DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void)
{
    DekiDesktopHAL_EnsureRegistered();
}

// =============================================================================
// Module Feature API
// =============================================================================

static const DekiModuleFeatureInfo s_Features[] = {
    {"memory", "Memory", "Desktop memory backend (malloc/free)", false},
    {"filesystem", "File System", "Desktop file system (fopen/fread/fwrite)", false},
};

DEKI_PLUGIN_API int DekiPlugin_GetFeatureCount(void)
{
    return sizeof(s_Features) / sizeof(s_Features[0]);
}

DEKI_PLUGIN_API const DekiModuleFeatureInfo* DekiPlugin_GetFeature(int index)
{
    if (index < 0 || index >= DekiPlugin_GetFeatureCount())
        return nullptr;
    return &s_Features[index];
}

// =============================================================================
// Module-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_DESKTOP_HAL_API const char* DekiDesktopHAL_GetName(void)
{
    return "Desktop HAL";
}

DEKI_DESKTOP_HAL_API int DekiDesktopHAL_GetFeatureCount(void)
{
    return DekiPlugin_GetFeatureCount();
}

DEKI_DESKTOP_HAL_API const DekiModuleFeatureInfo* DekiDesktopHAL_GetFeature(int index)
{
    return DekiPlugin_GetFeature(index);
}

} // extern "C"

#else // !DEKI_EDITOR - Runtime registration

// For runtime builds, component registration happens via static initializers
// or explicit calls from the application

#endif // DEKI_EDITOR
