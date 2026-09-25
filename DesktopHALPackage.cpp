/**
 * @file DesktopHALPackage.cpp
 * @brief Desktop platform host for the Deki Desktop HAL package.
 *
 * Owns the desktop program entry (main) and brings up the desktop HAL
 * (memory + filesystem providers) before the engine initializes. This mirrors
 * the ESP32 HAL package (ESP32HALPackage.cpp), whose ESP32BackendInit sets the
 * device backends before app_main(). Display/input/time come from a separate
 * graphics package (deki-sdl3-integration), exactly as lovyangfx supplies the
 * device display alongside the ESP32 HAL.
 */

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

extern void DekiDesktopHAL_RegisterComponents();
extern int DekiDesktopHAL_GetAutoComponentCount();
extern const Deki::ComponentMeta* DekiDesktopHAL_GetAutoComponentMeta(int index);

namespace DekiDesktop
{

// =============================================================================
// Desktop platform entry + HAL bring-up (standalone simulator build)
// =============================================================================
#if defined(SIMULATOR)


}  // namespace DekiDesktop

// The program's entry point, at GLOBAL scope. It has to be: the C++ runtime
// looks for ::main and nothing else will do, so wrapping it in the package's
// namespace produced DekiDesktop::main and left every simulator and firmware
// binary with no entry point at all — a link failure naming WinMain, which
// points nowhere near the cause. The using-directive below keeps the body
// reaching the package's own helpers unchanged.
using namespace DekiDesktop;

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    setvbuf(stdout, nullptr, _IONBF, 0);  // unbuffered: logs survive a crash
    // Standalone sim has no editor console; route engine logs to a file next
    // to the exe (the working directory, which is also where the flash/ and
    // storage/ partitions live) and echo them on stdout.
    Deki::LogSystem::SetLogCallback([](Deki::LogLevel level, const std::string& msg,
                                     const char* file, int line) {
        (void)level; (void)file; (void)line;
        static FILE* lf = fopen("dekigame.log", "w");
        if (lf) { fprintf(lf, "%s\n", msg.c_str()); fflush(lf); }
        printf("%s\n", msg.c_str());
    });
    // Desktop HAL providers must be live before Deki::Engine::Initialize() runs (it calls
    // Deki::Memory/Deki::FileSystem::Initialize()). Set them up here in main() rather than a
    // static initializer to avoid static-init-order issues with the provider singletons.
    Deki::Memory::SetBackend(new Deki::HostMemoryProvider());
    // Engine::Initialize() then finds the assets in F:/assets/ (./flash/assets/).
    Deki::FileSystem::SetFileSystem(new Deki::DesktopFileSystem());
    return Deki::Main();
}

namespace DekiDesktop
{

#endif // SIMULATOR

#ifdef DEKI_EDITOR

// Auto-generated registration helpers

// Track if already registered to avoid duplicates
static bool s_DesktopHALRegistered = false;


// The exports below are C symbols at global scope; the package's own
// registration helpers and statics live in its namespace.
using namespace DekiDesktop;

extern "C" {

/**
 * @brief Ensure deki-desktop-hal package is loaded and components are registered
 */
DEKI_DESKTOP_HAL_API int DekiDesktopHAL_EnsureRegistered(void)
{
    if (s_DesktopHALRegistered)
        return ::DekiDesktopHAL_GetAutoComponentCount();
    s_DesktopHALRegistered = true;

    // Auto-generated: registers all Desktop HAL components with ComponentRegistry + ComponentFactory
    ::DekiDesktopHAL_RegisterComponents();

    return ::DekiDesktopHAL_GetAutoComponentCount();
}

// =============================================================================
// Plugin metadata (for dynamic loading compatibility)
// =============================================================================

DEKI_PLUGIN_API const char* DekiPlugin_GetName(void)
{
    return "Deki Desktop HAL Package";
}

DEKI_PLUGIN_API const char* DekiPlugin_GetVersion(void)
{
#ifdef DEKI_PACKAGE_VERSION
    return DEKI_PACKAGE_VERSION;
#else
    return "0.0.0-dev";
#endif
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
    return ::DekiDesktopHAL_GetAutoComponentCount();
}

DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return ::DekiDesktopHAL_GetAutoComponentMeta(index);
}

DEKI_PLUGIN_API void DekiPlugin_RegisterComponents(void)
{
    DekiDesktopHAL_EnsureRegistered();
}

// =============================================================================
// Package-specific feature API (for linked DLL access without name conflicts)
// =============================================================================

DEKI_DESKTOP_HAL_API const char* DekiDesktopHAL_GetName(void)
{
    return "Desktop HAL";
}

} // extern "C"

#endif // DEKI_EDITOR
}  // namespace DekiDesktop

