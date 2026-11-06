// The desktop package's entry, for the editor's plugin loader. A game built
// as a desktop program starts at the engine's own main()
// (deki-engine-hosted), which sets up the memory and the file system; the
// desktop build's display, input and time come from
// deki-sdl3-integration.

#include "DesktopHALPackage.h"
#include <deki/interop/Plugin.h>
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/ComponentFactory.h>

extern void DekiDesktopHALRegisterComponents();
extern int DekiDesktopHALGetAutoComponentCount();
extern const Deki::ComponentMeta* DekiDesktopHALGetAutoComponentMeta(int index);

namespace DekiDesktop
{

#ifdef DEKI_EDITOR

// Set once registered, so components register only once.
static bool s_DesktopHALRegistered = false;

// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace DekiDesktop;

extern "C"
{
    /// Registers the package's components once. Returns how many it has.
    DEKI_DESKTOP_HAL_API int DekiDesktopHALEnsureRegistered(void)
    {
        if (s_DesktopHALRegistered)
        {
            return ::DekiDesktopHALGetAutoComponentCount();
        }
        s_DesktopHALRegistered = true;

        // Generated: registers every component with ComponentRegistry and ComponentFactory.
        ::DekiDesktopHALRegisterComponents();

        return ::DekiDesktopHALGetAutoComponentCount();
    }

    // =============================================================================
    // Plugin metadata, read when the package is loaded as a DLL
    // =============================================================================

    DEKI_PLUGIN_API const char* DekiPluginGetName(void)
    {
        return "Deki Desktop HAL Package";
    }

    DEKI_PLUGIN_API const char* DekiPluginGetVersion(void)
    {
#ifdef DEKI_PACKAGE_VERSION
        return DEKI_PACKAGE_VERSION;
#else
        return "0.0.0-dev";
#endif
    }

    DEKI_PLUGIN_API int DekiPluginInit(void)
    {
        return 0;
    }

    DEKI_PLUGIN_API void DekiPluginShutdown(void)
    {
        s_DesktopHALRegistered = false;
    }

    DEKI_PLUGIN_API int DekiPluginGetComponentCount(void)
    {
        return ::DekiDesktopHALGetAutoComponentCount();
    }

    DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPluginGetComponentMeta(int index)
    {
        return ::DekiDesktopHALGetAutoComponentMeta(index);
    }

    DEKI_PLUGIN_API void DekiPluginRegisterComponents(void)
    {
        DekiDesktopHALEnsureRegistered();
    }

    // =============================================================================
    // Package-specific API, prefixed so linked DLLs do not clash
    // =============================================================================

    DEKI_DESKTOP_HAL_API const char* DekiDesktopHALGetName(void)
    {
        return "Desktop HAL";
    }

}  // extern "C"

#endif  // DEKI_EDITOR
}  // namespace DekiDesktop
