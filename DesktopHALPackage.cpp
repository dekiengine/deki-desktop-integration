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

// =============================================================================
// Desktop platform entry + HAL bring-up (standalone simulator build)
// =============================================================================
#if defined(SIMULATOR)

#include <deki/Main.h>
#include <deki/LogSystem.h>
#include <deki/providers/Memory.h>
#include <deki/providers/FileSystem.h>
#include <deki/providers/IFileSystem.h>
#include <deki/platforms/desktop/DesktopMemoryProvider.h>
#include <deki/platforms/desktop/DesktopFileSystem.h>
#include <deki/assets/AssetManager.h>
#include <deki/assets/AssetLookupTable.h>
#include <deki/assets/AssetPackReader.h>
#include <cstdio>

// Load the deployed asset registry from the SD-card mount (S:/) so scene/asset
// lookups by key resolve. On a device this is done by SDCardComponent once the SD
// mounts; the simulator's S:/ is just ./storage/, already live via DesktopFileSystem.
static void LoadDeployedAssetTable() {
    const char* tablePath = "S:/asset_table.bin";
    Deki::IFileSystem* fs = Deki::FileSystem::GetFileSystemForPath(tablePath);
    if (!fs) { DEKI_LOG_WARNING("Simulator: no filesystem for %s", tablePath); return; }
    auto handle = fs->OpenFile(tablePath, Deki::IFileSystem::OpenMode::READ_BINARY);
    if (!handle) { DEKI_LOG_WARNING("Simulator: %s not found", tablePath); return; }
    long size = fs->GetFileSize(handle);
    if (size <= 0) { fs->CloseFile(handle); DEKI_LOG_WARNING("Simulator: %s is empty", tablePath); return; }
    // Persist for the process lifetime: AssetLookupTable references this buffer.
    static uint8_t* s_tableData = nullptr;
    if (s_tableData) delete[] s_tableData;
    s_tableData = new uint8_t[static_cast<size_t>(size)];
    size_t read = fs->ReadFile(handle, s_tableData, static_cast<size_t>(size));
    fs->CloseFile(handle);
    if (read != static_cast<size_t>(size)) {
        DEKI_LOG_ERROR("Simulator: short read on %s (%zu of %ld)", tablePath, read, size);
        delete[] s_tableData; s_tableData = nullptr; return;
    }
    if (Deki::AssetManager::Get()->LoadAssetLookupTable(s_tableData, static_cast<size_t>(size))) {
        DEKI_LOG_INFO("Simulator: loaded asset_table.bin (%u entries)", Deki::AssetLookupTable::GetEntryCount());
        // Exported assets live as S:/<guid>, the same layout SDCardComponent
        // announces on a device. Without the base the manager resolved a bare
        // GUID next to the exe and every load, the startup scene included,
        // came back null.
        Deki::AssetManager::Get()->SetCacheDirectory("S:/");
        Deki::AssetPackReader::Instance().LoadPackIndex("S:/pack_index.bin");
    } else {
        DEKI_LOG_ERROR("Simulator: failed to parse asset_table.bin");
    }
}

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
    Deki::Memory::SetBackend(new Deki::DesktopMemoryProvider());
    Deki::FileSystem::SetFileSystem(new Deki::DesktopFileSystem());
    // S:/ (./storage/) is now live; load the exported asset registry so the startup
    // scene + assets resolve by key. Must precede the boot scene's startup-scene load.
    LoadDeployedAssetTable();
    return Deki::Main();
}

#endif // SIMULATOR

#ifdef DEKI_EDITOR

// Auto-generated registration helpers
extern void DekiDesktopHAL_RegisterComponents();
extern int DekiDesktopHAL_GetAutoComponentCount();
extern const Deki::ComponentMeta* DekiDesktopHAL_GetAutoComponentMeta(int index);

// Track if already registered to avoid duplicates
static bool s_DesktopHALRegistered = false;

extern "C" {

/**
 * @brief Ensure deki-desktop-hal package is loaded and components are registered
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
    return DekiDesktopHAL_GetAutoComponentCount();
}

DEKI_PLUGIN_API const Deki::ComponentMeta* DekiPlugin_GetComponentMeta(int index)
{
    return DekiDesktopHAL_GetAutoComponentMeta(index);
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
