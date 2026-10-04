// Desktop platform host. Owns the program entry (main) and sets up the
// memory and filesystem providers before the engine starts, as
// ESP32HALPackage.cpp does with ESP32BackendInit before app_main(). Display,
// input and time come from deki-sdl3-integration, as lovyangfx supplies the
// display next to the ESP32 HAL.

#include "DesktopHALPackage.h"
#include <deki/interop/Plugin.h>
#include <deki/reflection/ComponentRegistry.h>
#include <deki/reflection/ComponentFactory.h>

#if defined(SIMULATOR)
#include <deki/Main.h>
#include <deki/LogSystem.h>
#include <deki/providers/Memory.h>
#include <deki/providers/FileSystem.h>
#include <deki/providers/IFileSystem.h>
#include <deki/providers/HostMemoryProvider.h>
#include "DesktopFileSystem.h"
#include <cstdio>
#endif

extern void DekiDesktopHALRegisterComponents();
extern int DekiDesktopHALGetAutoComponentCount();
extern const Deki::ComponentMeta* DekiDesktopHALGetAutoComponentMeta(int index);

namespace DekiDesktop
{

// =============================================================================
// Desktop platform entry + HAL bring-up (standalone simulator build)
// =============================================================================
#if defined(SIMULATOR)

}  // namespace DekiDesktop

// The program's entry point. Must stay at global scope: the runtime looks for
// ::main only, and inside a namespace the link fails with an error naming
// WinMain, far from the cause. The using-directive lets the body reach the
// package's own helpers.
using namespace DekiDesktop;

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: logs survive a crash
    // The standalone simulator has no editor console: write engine logs to a
    // file in the working directory (next to flash/ and storage/) and echo
    // them on stdout.
    Deki::LogSystem::SetLogCallback(
        [](Deki::LogLevel level, const std::string& msg, const char* file, int line)
        {
            (void)level;
            (void)file;
            (void)line;
            static FILE* lf = fopen("dekigame.log", "w");
            if (lf)
            {
                fprintf(lf, "%s\n", msg.c_str());
                fflush(lf);
            }
            printf("%s\n", msg.c_str());
        });
    // The providers must exist before Engine::Initialize() runs, since it
    // initializes Memory and FileSystem. Set here rather than in a static
    // initializer, so static-init order cannot bite the provider singletons.
    Deki::Memory::SetBackend(new Deki::HostMemoryProvider());
    // Engine::Initialize() then finds the assets in F:/assets/ (./flash/assets/).
    Deki::FileSystem::SetFileSystem(new Deki::DesktopFileSystem());
    return Deki::Main();
}

namespace DekiDesktop
{

#endif  // SIMULATOR

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
