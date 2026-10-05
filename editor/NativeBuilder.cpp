#include "NativeBuilder.h"
#include <deki-editor/build/BuilderWidgets.h>
#include <deki-editor/EditorTheme.h>
#include <deki-editor/EditorUI.h>
#include <deki/LogSystem.h>
#include <deki-editor/SafeNames.h>
#include <deki-editor/EditorHttpUtils.h>
#include <fstream>
#include <cstdio>
#include <deki-editor/Paths.h>
#include <deki-editor/build/ProjectPaths.h>
#include <deki-editor/build/CMakeGenUtils.h>
#include <deki-editor/EditorSettings.h>
#include <deki-editor/ProcessLaunch.h>
#include <deki-editor/FeatureResolver.h>
#include <deki-editor/build/PlatformConfig.h>
#include "imgui.h"
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace DekiEditor
{

namespace
{
// The simulator target is DekiGame; only Windows gives it a suffix.
#ifdef _WIN32
const char* const kNativeExecutableName = "DekiGame.exe";
#else
const char* const kNativeExecutableName = "DekiGame";
#endif

// A release archive of a third-party library for one host, from package.json:
//   "dependencies": { "native": [ { "name": "SDL3", "version": "3.2.8",
//       "git": "https://github.com/libsdl-org/SDL.git",
//       "prebuilt": { "windows-mingw": { "url": "...", "sha256": "...",
//           "cmakeDir": "SDL3-3.2.8/x86_64-w64-mingw32/lib/cmake/SDL3",
//           "includeDir": "SDL3-3.2.8/x86_64-w64-mingw32/include" } } } ] }
// Unpacked under <project>/generated/deps and used through its CMake package
// instead of a source build.
//
// This shape belongs to this backend. The editor passes each declaration on
// as written (CMakeGen::FrameworkDependency) without reading it.
struct NativePrebuilt
{
    std::string url;         // https archive
    std::string sha256;      // pinned; an unpinned archive is not used
    std::string cmakeDir;    // directory holding <Name>Config.cmake, relative to the archive root
    std::string includeDir;  // public headers, relative to the archive root
};

// A third-party library a package needs in a desktop build. The package
// picks the version; the builder follows it.
struct NativeDependency
{
    std::string name;                                // e.g. "SDL3"
    std::string version;                             // e.g. "3.2.8"
    std::string git;                                 // source fallback: repository, built at tag release-<version>
    std::map<std::string, NativePrebuilt> prebuilt;  // host key ("windows-mingw", "linux", "macos") -> archive
};

// Only the object form describes a library to fetch. A bare string under
// "native" is ignored.
bool ParseNativeDependency(const CMakeGen::FrameworkDependency& declared, NativeDependency& out)
{
    if (declared.json.empty())
    {
        return false;
    }

    const nlohmann::json dep = nlohmann::json::parse(declared.json, nullptr, /*allow_exceptions*/ false);
    if (!dep.is_object())
    {
        return false;
    }

    out.name = declared.name;
    out.version = dep.value("version", "");
    out.git = dep.value("git", "");
    if (dep.contains("prebuilt") && dep["prebuilt"].is_object())
    {
        for (auto& [host, archive] : dep["prebuilt"].items())
        {
            if (!archive.is_object())
            {
                continue;
            }
            NativePrebuilt pre;
            pre.url = archive.value("url", "");
            pre.sha256 = archive.value("sha256", "");
            pre.cmakeDir = archive.value("cmakeDir", "");
            pre.includeDir = archive.value("includeDir", "");
            out.prebuilt[host] = pre;
        }
    }
    return true;
}
}  // namespace

NativeBuilder::NativeBuilder()
{
}

NativeBuilder::~NativeBuilder()
{
    Cancel();
    if (m_BuildThread.joinable())
    {
        m_BuildThread.join();
    }
}

// ============================================================================
// Identity
// ============================================================================

std::string NativeBuilder::GetBuildDirectory(const std::string& projectPath) const
{
    // build/native, next to the other platforms' build/<id> folders.
    return (ProjectPaths::Build(projectPath) / "native").string();
}

// ============================================================================
// Toolchain (delegated to NativeToolchain)
// ============================================================================

bool NativeBuilder::IsToolchainInstalled() const
{
    return m_Toolchain.IsInstalled();
}

std::string NativeBuilder::GetToolchainStatus() const
{
    return m_Toolchain.GetStatus();
}

std::vector<ToolchainComponent> NativeBuilder::GetToolchainComponents() const
{
    return m_Toolchain.GetComponents();
}

// ============================================================================
// Core operations
// ============================================================================

void NativeBuilder::Build(const std::string& projectPath, BuildOutputCallback outputCallback,
                          BuildProgressCallback progressCallback)
{
    RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                     { DoBuild(projectPath, outputCallback, progressCallback); });
}

void NativeBuilder::Deploy(const std::string& projectPath, const std::string& /*deployTargetId*/,
                           BuildOutputCallback outputCallback, BuildProgressCallback progressCallback)
{
    std::string buildDir = GetBuildDirectory(projectPath);

    // The generated project uses Ninja, which is single-config, so the
    // executable lands in build/bin. The per-config folders below cover a
    // multi-config generator.
    std::string exePath;
    std::vector<std::string> candidates = { buildDir + "/build/bin/" + kNativeExecutableName,
                                            buildDir + "/build/bin/Release/" + kNativeExecutableName,
                                            buildDir + "/build/bin/Debug/" + kNativeExecutableName,
                                            buildDir + "/build/" + kNativeExecutableName,
                                            buildDir + "/build/Release/" + kNativeExecutableName,
                                            buildDir + "/build/Debug/" + kNativeExecutableName };

    for (const auto& path : candidates)
    {
        if (fs::exists(path))
        {
            exePath = path;
            break;
        }
    }

    if (exePath.empty())
    {
        if (outputCallback)
        {
            outputCallback("Executable not found. Build the project first.", true);
        }
        return;
    }

    if (outputCallback)
    {
        outputCallback("Launching: " + exePath, false);
    }

    // Run from its own folder: it finds the storage partitions beside it
    // through the working directory.
    const std::string workDir = fs::path(exePath).parent_path().string();
    if (LaunchDetachedInDirectory(exePath, {}, workDir))
    {
        if (outputCallback)
        {
            outputCallback("Application launched.", false);
        }
    }
    else
    {
        if (outputCallback)
        {
            outputCallback("Failed to launch application.", true);
        }
    }
}

void NativeBuilder::Clean(const std::string& projectPath, BuildOutputCallback outputCallback,
                          BuildProgressCallback progressCallback)
{
    RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                     { DoClean(projectPath, outputCallback, progressCallback); });
}

// ============================================================================
// Internal workers
// ============================================================================

void NativeBuilder::DoBuild(const std::string& projectPath, BuildOutputCallback outputCallback,
                            BuildProgressCallback progressCallback)
{
    SetProgress(BuildState::Building, "Starting native build...", 0.0f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }

    if (!IsToolchainInstalled())
    {
        SetError("CMake is not installed. Please install CMake and ensure it is on your PATH.");
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    std::string buildDir = GetBuildDirectory(projectPath);
    std::string enginePath = GetEnginePath(projectPath);

    if (outputCallback)
    {
        outputCallback("Building standalone native executable...", false);
        outputCallback("Build directory: " + buildDir, false);
        outputCallback("Engine path: " + enginePath, false);
    }

    // Always regenerate the build files, so package changes (added or removed
    // packages, PACKAGE_PREFIX edits) reach the build. Both CMakeLists.txt and
    // deki_package_init.gen.cpp go through WriteIfChanged, so an unchanged
    // build keeps their timestamps and CMake skips the rebuild. Must not be
    // skipped when CMakeLists.txt exists: a stale deki_package_init.gen.cpp
    // calls registration functions that no longer exist and the link fails.
    if (outputCallback)
    {
        outputCallback("Generating build files...", false);
    }
    // Third-party libraries the packages declared (SDL3): fetch a prebuilt
    // archive where one is published for this host, else note the version
    // for a source build. Runs before the build files, which name the result.
    SetProgress(BuildState::Building, "Resolving native dependencies...", 0.05f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }
    if (!PrepareNativeDependencies(projectPath, outputCallback))
    {
        SetError("Failed to resolve native dependencies");
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    if (!GenerateBuildFiles(projectPath, m_PlatformConfig, m_PackageDefines))
    {
        SetError("Failed to generate build files");
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    // Configure
    SetProgress(BuildState::Building, "Configuring...", 0.1f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }

    std::string configureCmd = "cmake" + EditorSettings::GetCMakeLocationArgs() + " -B build -S .";
    if (m_CancelRequested)
    {
        SetError("Build cancelled");
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    int configResult = m_Toolchain.ExecuteCommand(configureCmd, buildDir, outputCallback, m_CancelRequested);
    if (configResult != 0)
    {
        SetError("CMake configure failed with exit code " + std::to_string(configResult));
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    // Build
    SetProgress(BuildState::Building, "Building...", 0.3f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }

    std::string buildCmd = "cmake --build build --config Release";
    if (m_CancelRequested)
    {
        SetError("Build cancelled");
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    int buildResult = m_Toolchain.ExecuteCommand(buildCmd, buildDir, outputCallback, m_CancelRequested);
    if (buildResult != 0)
    {
        SetError("Build failed with exit code " + std::to_string(buildResult));
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    if (!DeployPartitions(projectPath, buildDir, outputCallback))
    {
        SetError("Build succeeded but a storage partition could not be deployed next to the exe");
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    SetProgress(BuildState::Completed, "Build succeeded", 1.0f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }
}

void NativeBuilder::DoClean(const std::string& projectPath, BuildOutputCallback outputCallback,
                            BuildProgressCallback progressCallback)
{
    SetProgress(BuildState::Building, "Cleaning...", 0.0f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }

    std::string buildDir = GetBuildDirectory(projectPath);
    fs::path buildPath = fs::path(buildDir) / "build";

    try
    {
        if (fs::exists(buildPath))
        {
            fs::remove_all(buildPath);
            if (outputCallback)
            {
                outputCallback("Removed: " + buildPath.string(), false);
            }
        }
        else
        {
            if (outputCallback)
            {
                outputCallback("Build directory does not exist, nothing to clean.", false);
            }
        }
    }
    catch (const std::exception& e)
    {
        SetError(std::string("Clean failed: ") + e.what());
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
        return;
    }

    SetProgress(BuildState::Completed, "Clean succeeded", 1.0f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }
}

// ============================================================================
// Build file generation
// ============================================================================

bool NativeBuilder::GenerateBuildFiles(const std::string& projectPath, const PlatformConfig& config,
                                       const std::vector<std::string>& packageDefines)
{
    fs::path buildDir = fs::path(GetBuildDirectory(projectPath));

    try
    {
        fs::create_directories(buildDir);
    }
    catch (const std::exception&)
    {
        return false;
    }

    if (!GenerateCMakeLists(projectPath, buildDir.string(), config, packageDefines))
    {
        return false;
    }

    return true;
}

static std::string ToCMakePath(const std::string& path)
{
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

// The version of an SDL source checkout ("3.2.8") from its SDL_version.h,
// or "" when there is no readable checkout there.
static std::string SdlSourceVersion(const fs::path& srcDir)
{
    std::ifstream in(srcDir / "include" / "SDL3" / "SDL_version.h");
    if (!in)
    {
        return "";
    }
    int major = -1, minor = -1, micro = -1, v = 0;
    std::string line;
    while (std::getline(in, line))
    {
        if (std::sscanf(line.c_str(), "#define SDL_MAJOR_VERSION %d", &v) == 1)
        {
            major = v;
        }
        else if (std::sscanf(line.c_str(), "#define SDL_MINOR_VERSION %d", &v) == 1)
        {
            minor = v;
        }
        else if (std::sscanf(line.c_str(), "#define SDL_MICRO_VERSION %d", &v) == 1)
        {
            micro = v;
        }
    }
    if (major < 0 || minor < 0 || micro < 0)
    {
        return "";
    }
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(micro);
}

const char* NativeBuilder::HostPrebuiltKey()
{
#if defined(_WIN32)
    return "windows-mingw";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

// Resolves every native dependency the active packages declare. A prebuilt
// archive for this host is downloaded (only with a pinned SHA-256), unpacked
// under <project>/generated/deps/<name>-<version>/ and marked with that
// SHA-256, so a later build only checks the marker. Anything that cannot be
// fetched is built from source at release-<version> by the generated CMake.
bool NativeBuilder::PrepareNativeDependencies(const std::string& projectPath, BuildOutputCallback outputCallback)
{
    m_NativeDeps.clear();
    auto log = [&](const std::string& message, bool isError = false)
    {
        if (outputCallback)
        {
            outputCallback(message, isError);
        }
    };

    const auto allPackages = CMakeGen::ScanPackageManifests(projectPath);
    const auto activeIds =
        CMakeGen::ResolveActivePackages(allPackages, m_PackageDefines, m_PlatformConfig.Capabilities());
    const std::string host = HostPrebuiltKey();
    const fs::path depsRoot = ProjectPaths::Generated(projectPath) / "deps";

    for (const auto& pkg : allPackages)
    {
        if (activeIds.count(pkg.id) == 0)
        {
            continue;
        }
        const auto mine = pkg.frameworkDeps.find(GetFrameworkId());
        if (mine == pkg.frameworkDeps.end())
        {
            continue;
        }
        for (const auto& declared : mine->second)
        {
            NativeDependency dep;
            if (!ParseNativeDependency(declared, dep))
            {
                continue;
            }

            std::string reason;
            if (!SafeNames::IsSafeName(dep.name, reason) || !SafeNames::IsSafeName(dep.version, reason))
            {
                log(pkg.id + ": native dependency '" + dep.name + "' " + dep.version +
                        " has an unusable name: " + reason,
                    true);
                continue;
            }

            ResolvedNativeDependency resolved;
            resolved.version = dep.version;
            resolved.git = dep.git;

            const auto pit = dep.prebuilt.find(host);
            if (pit != dep.prebuilt.end())
            {
                const NativePrebuilt& pre = pit->second;
                const fs::path dest = depsRoot / (dep.name + "-" + dep.version);
                const fs::path marker = dest / ".deki-prebuilt";
                std::error_code ec;

                bool ready = false;
                {
                    std::ifstream in(marker);
                    std::string have;
                    ready = in && std::getline(in, have) && !pre.sha256.empty() && have == pre.sha256;
                }

                if (!ready && pre.sha256.empty())
                {
                    log(pkg.id + ": prebuilt " + dep.name + " has no sha256; building it from source instead", true);
                }
                else if (!ready && (!EditorHttpUtils::IsSafeArchiveEntry(pre.cmakeDir) ||
                                    !EditorHttpUtils::IsSafeArchiveEntry(pre.includeDir)))
                {
                    log(pkg.id + ": prebuilt " + dep.name +
                            " names a directory outside its archive; building it from source instead",
                        true);
                }
                else if (!ready)
                {
                    log("Fetching prebuilt " + dep.name + " " + dep.version + " from " + pre.url);
                    fs::create_directories(depsRoot, ec);
                    const fs::path zip = depsRoot / (dep.name + "-" + dep.version + ".zip");
                    auto status = [&](const std::string& s) { log(s); };
                    bool ok = EditorHttpUtils::DownloadFileVerified(pre.url, zip.string(), pre.sha256,
                                                                    &m_CancelRequested, status);
                    if (ok)
                    {
                        fs::remove_all(dest, ec);
                        fs::create_directories(dest, ec);
                        ok = EditorHttpUtils::ExtractZip(zip.string(), dest.string(), status);
                    }
                    fs::remove(zip, ec);
                    if (ok)
                    {
                        std::ofstream out(marker, std::ios::binary);
                        out << pre.sha256 << "\n";
                        ready = static_cast<bool>(out);
                    }
                    if (!ready)
                    {
                        fs::remove_all(dest, ec);
                        log("Could not fetch the prebuilt " + dep.name + "; building it from source instead", true);
                    }
                }

                if (ready)
                {
                    const fs::path cmakeDir = dest / pre.cmakeDir;
                    if (fs::exists(cmakeDir / (dep.name + "Config.cmake")))
                    {
                        resolved.cmakeDir = cmakeDir.string();
                        resolved.includeDir = (dest / pre.includeDir).string();
                        log("Using prebuilt " + dep.name + " " + dep.version);
                    }
                    else
                    {
                        log("Prebuilt " + dep.name + " archive has no " + dep.name + "Config.cmake under " +
                                pre.cmakeDir + "; building it from source instead",
                            true);
                    }
                }
            }

            if (resolved.cmakeDir.empty())
            {
                log("Building " + dep.name + " " + dep.version + " from source");
            }
            m_NativeDeps[dep.name] = resolved;
        }
    }
    return true;
}

bool NativeBuilder::GenerateCMakeLists(const std::string& projectPath, const std::string& buildDir,
                                       const PlatformConfig& config, const std::vector<std::string>& packageDefines)
{
    std::string enginePath = ToCMakePath(EditorSettings::GetEnginePath());
    if (enginePath.empty())
    {
        return false;
    }

    // The transform width this project compiles: the widest any active package
    // or the project itself declares (CMakeGenUtils, COMPATIBILITY.md).
    const auto allPackages = CMakeGen::ScanPackageManifests(projectPath);
    const auto activeIds =
        CMakeGen::ResolveActivePackages(allPackages, packageDefines, m_PlatformConfig.Capabilities());
    std::string transformWhy;
    const CMakeGen::TransformWidth transformWidth =
        CMakeGen::ResolveProjectTransformWidth(allPackages, CMakeGen::ReadProjectTags(projectPath), &transformWhy);
    const std::vector<std::string> transformDefines = CMakeGen::TransformDefines(transformWidth);

    // What this build strips (services/FeatureResolver): the stripped
    // components' sources stay out of the package globs and their headers out
    // of the reflection codegen, so nothing refers to code that is not built.
    const StripPlan strip = ComputeStripPlan(projectPath, m_PlatformConfig.id);
    for (const auto& w : strip.warnings)
    {
        DEKI_LOG_WARNING("%s", w.c_str());
    }
    std::string stripSourceRegex;
    for (const auto& rx : strip.SourceExcludeRegexes())
    {
        stripSourceRegex += (stripSourceRegex.empty() ? "" : "|") + rx;
    }

    std::ostringstream file;

    file << "# Generated by Deki Editor — do not edit\n";
    file << "# Platform: " << config.displayName << " (Native Desktop)\n\n";

    file << "cmake_minimum_required(VERSION 3.16)\n";
    file << "project(DekiGame LANGUAGES C CXX)\n\n";

    // The project's own layout: src/ and packages/ under its root. This file
    // is rewritten on every build, so an absolute root is safe.
    file << "set(DEKI_PROJECT_ROOT \"" << ToCMakePath(projectPath) << "\")\n\n";

    // C++23, the same as the editor's package build (BuildFileGenerator) and
    // what ESP-IDF forces for a device. Must be at least C++20: the engine's
    // allocation tracking includes <source_location>, and with C++17 the error
    // points at Memory.h rather than at the standard.
    file << "set(CMAKE_CXX_STANDARD 23)\n";
    file << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

    file << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin\")\n";
    file << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG \"${CMAKE_BINARY_DIR}/bin/Debug\")\n";
    file << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE \"${CMAKE_BINARY_DIR}/bin/Release\")\n\n";

    // The engine path comes at configure time (-DDEKI_ENGINE_PATH), not as a
    // literal here: the editor install can move, and nothing regenerates this
    // file when it does.
    file << "# Deki Engine — path supplied by the editor at configure time\n";
    file << "if(NOT DEKI_ENGINE_PATH)\n";
    file << "    message(FATAL_ERROR\n";
    file << "        \"DEKI_ENGINE_PATH must be supplied by the Deki Editor.\\n\"\n";
    file << "        \"Build this project from the editor rather than invoking CMake directly.\")\n";
    file << "endif()\n\n";

    // Engine component metadata (src/generated/*.gen.cpp) comes from the editor
    // build and is reused through the deki-engine subdirectory below.

    // SDL3. deki-sdl3-integration declares it in package.json under
    // dependencies.native (name, version, git, prebuilt archives per host) and
    // PrepareNativeDependencies() resolved that before this file is written.
    // Prebuilt: SDL's own release archive, unpacked under <project>/generated/
    // deps and used through its CMake package, so the project holds no SDL3
    // build tree (on Windows that tree pushes a deep project folder past the
    // 260-character path limit). Source: FetchContent at release-<version>,
    // pointed at the editor's own checkout when that is the same version, so
    // nothing is cloned.
    ResolvedNativeDependency sdl3;
    if (auto it = m_NativeDeps.find("SDL3"); it != m_NativeDeps.end())
    {
        sdl3 = it->second;
    }
    if (sdl3.version.empty())
    {
        sdl3.version = "3.2.8";
    }
    if (sdl3.git.empty())
    {
        sdl3.git = "https://github.com/libsdl-org/SDL.git";
    }

    file << "# SDL3 " << sdl3.version << "\n";
    if (!sdl3.cmakeDir.empty())
    {
        file << "# Prebuilt release archive, fetched and verified by the editor.\n";
        file << "set(SDL3_DIR \"" << ToCMakePath(sdl3.cmakeDir) << "\" CACHE PATH \"\" FORCE)\n";
        file << "find_package(SDL3 " << sdl3.version << " EXACT REQUIRED CONFIG)\n";
        file << "set(DEKI_SDL3_TARGET SDL3::SDL3)\n";
        file << "set(DEKI_SDL3_INCLUDE \"" << ToCMakePath(sdl3.includeDir) << "\")\n\n";
    }
    else
    {
        file << "include(FetchContent)\n";
        const fs::path editorSdl3 = fs::path(EditorSettings::GetDepsDir()) / "sdl3-src";
        if (SdlSourceVersion(editorSdl3) == sdl3.version)
        {
            file << "if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_SDL3 AND EXISTS \"" << ToCMakePath(editorSdl3.string())
                 << "/CMakeLists.txt\")\n";
            file << "    set(FETCHCONTENT_SOURCE_DIR_SDL3 \"" << ToCMakePath(editorSdl3.string())
                 << "\" CACHE PATH \"SDL3 source shared with the editor build\")\n";
            file << "endif()\n";
        }
        file << "set(SDL_SHARED OFF CACHE BOOL \"\" FORCE)\n";
        file << "set(SDL_STATIC ON CACHE BOOL \"\" FORCE)\n";
        file << "set(SDL_TEST OFF CACHE BOOL \"\" FORCE)\n";
        file << "FetchContent_Declare(SDL3\n";
        file << "    GIT_REPOSITORY " << sdl3.git << "\n";
        file << "    GIT_TAG release-" << sdl3.version << "\n";
        file << "    GIT_SHALLOW TRUE\n";
        file << ")\n";
        file << "FetchContent_MakeAvailable(SDL3)\n";
        file << "set(DEKI_SDL3_TARGET SDL3::SDL3-static)\n";
        file << "set(DEKI_SDL3_INCLUDE \"\")\n\n";
    }

    file << "find_package(OpenGL REQUIRED)\n\n";

    // The engine as a subdirectory: a static library without DEKI_EDITOR.
    file << "# Engine (static library, non-editor mode)\n";
    file << "set(DEKI_TRANSFORM_2D " << (transformWidth != CMakeGen::TransformWidth::None ? "ON" : "OFF")
         << " CACHE BOOL \"\" FORCE)\n";
    file << "set(DEKI_TRANSFORM_3D " << (transformWidth == CMakeGen::TransformWidth::ThreeD ? "ON" : "OFF")
         << " CACHE BOOL \"\" FORCE)\n";
    file << "message(STATUS \"Deki transform: " << CMakeGen::EscapeCMakeString(transformWhy) << "\")\n";
    file << "add_subdirectory(\"${DEKI_ENGINE_PATH}\" \"${CMAKE_BINARY_DIR}/deki-engine\")\n\n";

    // The engine links SDL3 and OpenGL, which the SDL3 package needs.
    file << "target_link_libraries(deki-engine-core PUBLIC ${DEKI_SDL3_TARGET} OpenGL::GL)\n\n";

    // What the engine has to be told about its target, taken from the
    // platform. The engine's CMake must not hard-code it: it would build at
    // a fixed screen size and format whatever the platform says.
    {
        // Validated: it lands in a generated CMake file, and a platform JSON
        // can come from a board pack or a package.
        const std::string colorFormat = config.colorFormat.empty() ? std::string("RGB565") : config.colorFormat;
        for (char c : colorFormat)
        {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok)
            {
                DEKI_LOG_ERROR("NativeBuilder: colorFormat '%s' is not an identifier; refusing to generate",
                               config.colorFormat.c_str());
                return false;
            }
        }
        file << "# The engine, told about this target (it no longer assumes one)\n";
        file << "target_compile_definitions(deki-engine-core PUBLIC\n";
        file << "    SIMULATOR\n";
        file << "    \"DEKI_SCREEN_WIDTH=" << config.screenWidth << "\"\n";
        file << "    \"DEKI_SCREEN_HEIGHT=" << config.screenHeight << "\"\n";
        // The screen the SDL window emulates: its size and pixel format.
        file << "    \"DEKI_SCREEN_COLOR_FORMAT=Deki::ColorFormat::" << colorFormat << "\"\n";
        file << "    \"DEKI_ENABLE_TRANSPARENCY=true\"\n";
        file << "    \"DEKI_FAST_ATTR=\")\n";
    }

    // The engine defines DEKI_LOG_ENABLED only for editor builds. The desktop
    // simulator is a debugging tool, so it gets engine-core logs too.
    file << "# Desktop simulator: enable engine-core logging (it's a debug build target)\n";
    file << "target_compile_definitions(deki-engine-core PUBLIC DEKI_LOG_ENABLED)\n";
    // The engine's internal trace (DEKI_LOG_INTERNAL: lifecycle, asset lookups,
    // platform setup) follows the platform's defines, so a project can switch
    // it on for a simulator run without editing the engine.
    if (std::find(config.defines.begin(), config.defines.end(), "DEKI_LOG_INTERNAL_ENABLED") != config.defines.end())
    {
        file << "target_compile_definitions(deki-engine-core PUBLIC DEKI_LOG_INTERNAL_ENABLED)\n";
    }
    file << "\n";

    // Project sources. PluginExports.cpp is DLL export glue for the editor (it
    // uses editor-only GetComponentMeta); the static exe registers components
    // through the init file below.
    CMakeGen::EmitProjectSourceCollection(file, "${DEKI_PROJECT_ROOT}/src");
    file << "list(FILTER PROJECT_SOURCES EXCLUDE REGEX \"PluginExports\\\\.cpp$\")\n";
    file << "\n";

    // Package discovery. One static exe, not DLLs, so: define each package's
    // *_EXPORTS (dllexport, not dllimport), collect declared system libs, leave
    // out each package's DLL-export entry (they collide across packages) except
    // the platform entry that defines main(), and compile each package's
    // generated metadata and vendored .c files.
    file << "# Package packages\n";
    file << "set(_ALL_PACKAGE_SOURCES \"\")\n";
    file << "set(PACKAGE_INCLUDE_DIRS \"\")\n";
    file << "set(PACKAGE_DEFINES \"\")\n";
    file << "message(STATUS \"Deki stripping: " << CMakeGen::EscapeCMakeString(strip.summary) << "\")\n";
    file << "set(_DEKI_STRIP_SRC_REGEX \"" << CMakeGen::EscapeCMakeString(stripSourceRegex) << "\")\n";
    // Totals are named _ALL_* because every include(package.cmake) sets
    // PACKAGE_SOURCES, PACKAGE_SYSTEM_LIBS and the like for that package.
    // Must stay separate names: sharing them keeps only the last package.
    file << "set(_ALL_SYSTEM_LIBS \"\")\n";
    // Reflection codegen inputs, filled by the package loop below.
    file << "set(_RC_PKG_DIRS \"\")\n";
    file << "set(_RC_PKG_TAGS \"\")\n";
    file << "set(_RC_PKG_PREFIXES \"\")\n";
    file << "set(_RC_PKG_OUTDIRS \"\")\n";
    file << "file(GLOB PACKAGE_CMAKE_FILES CONFIGURE_DEPENDS \"${DEKI_PROJECT_ROOT}/packages/*/package.cmake\")\n";
    file << "foreach(PACKAGE_CMAKE ${PACKAGE_CMAKE_FILES})\n";
    file << "    get_filename_component(PACKAGE_DIR \"${PACKAGE_CMAKE}\" DIRECTORY)\n";
    file << "    unset(PACKAGE_CORE_SOURCES)\n";
    file << "    unset(PACKAGE_ENTRY)\n";
    file << "    unset(PACKAGE_UPPER)\n";
    file << "    unset(PACKAGE_SYSTEM_LIBS)\n";
    file << "    include(\"${PACKAGE_CMAKE}\")\n";
    file << "    if(PACKAGE_UPPER)\n";
    file << "        list(APPEND PACKAGE_DEFINES \"DEKI_${PACKAGE_UPPER}_EXPORTS\")\n";
    file << "    endif()\n";
    file << "    if(PACKAGE_SYSTEM_LIBS)\n";
    file << "        list(APPEND _ALL_SYSTEM_LIBS ${PACKAGE_SYSTEM_LIBS})\n";
    file << "    endif()\n";
    file << "    set(_ENTRY_PATH \"\")\n";
    file << "    if(PACKAGE_ENTRY AND EXISTS \"${PACKAGE_DIR}/${PACKAGE_ENTRY}\")\n";
    file << "        set(_ENTRY_PATH \"${PACKAGE_DIR}/${PACKAGE_ENTRY}\")\n";
    file << "        file(READ \"${_ENTRY_PATH}\" _entry_content)\n";
    file << "        string(FIND \"${_entry_content}\" \"int main(\" _has_main)\n";
    file << "        if(NOT _has_main EQUAL -1)\n";
    file << "            set(_ENTRY_PATH \"\")\n";
    file << "        endif()\n";
    file << "    endif()\n";
    file << "    file(GLOB_RECURSE _MOD_SRCS CONFIGURE_DEPENDS \"${PACKAGE_DIR}/*.cpp\" \"${PACKAGE_DIR}/*.c\")\n";
    file << "    foreach(SRC ${_MOD_SRCS})\n";
    file << "        string(FIND \"${SRC}\" \"/editor/\" _IS_EDITOR)\n";
    file << "        string(FIND \"${SRC}\" \"/tests/\" _IS_TESTS)\n";
    // Skip the package's generated/ folder. Reflection .gen.cpp depends on the
    // configuration: the copy there comes from the editor plugin build with
    // DEKI_EDITOR defined, so it refers to editor-only members (such as
    // TextComponent::fontSize) missing from a runtime build. This build
    // generates its own below.
    file << "        string(FIND \"${SRC}\" \"/generated/\" _IS_GEN)\n";
    file << "        set(_IS_STRIPPED FALSE)\n";
    file << "        if(_DEKI_STRIP_SRC_REGEX AND \"${SRC}\" MATCHES \"${_DEKI_STRIP_SRC_REGEX}\")\n";
    file << "            set(_IS_STRIPPED TRUE)\n";
    file << "        endif()\n";
    file << "        if(_IS_EDITOR EQUAL -1 AND _IS_TESTS EQUAL -1 AND _IS_GEN EQUAL -1 AND NOT _IS_STRIPPED AND NOT "
            "\"${SRC}\" STREQUAL \"${_ENTRY_PATH}\")\n";
    file << "            list(APPEND _ALL_PACKAGE_SOURCES \"${SRC}\")\n";
    file << "        endif()\n";
    file << "    endforeach()\n";
    file << "    list(APPEND PACKAGE_INCLUDE_DIRS \"${PACKAGE_DIR}\")\n";
    // Codegen inputs, collected in the same pass. Output goes under this build
    // folder, not into the package, so the editor and firmware builds cannot
    // overwrite each other's reflection.
    file << "    get_filename_component(_MOD_NAME \"${PACKAGE_DIR}\" NAME)\n";
    file << "    unset(PACKAGE_PREFIX)\n";
    file << "    include(\"${PACKAGE_CMAKE}\")\n";
    file << "    if(PACKAGE_PREFIX)\n";
    file << "        list(APPEND _RC_PKG_DIRS \"${PACKAGE_DIR}\")\n";
    file << "        list(APPEND _RC_PKG_TAGS \"${_MOD_NAME}\")\n";
    file << "        list(APPEND _RC_PKG_PREFIXES \"${PACKAGE_PREFIX}\")\n";
    file << "        list(APPEND _RC_PKG_OUTDIRS \"${CMAKE_BINARY_DIR}/refl/${_MOD_NAME}/generated\")\n";
    // The package's headers do #include \"generated/X.gen.h\". A quoted include
    // looks in the including file's folder first, so a package the editor has
    // built uses its local .gen.h, which is safe: those hold declarations only
    // and do not depend on the configuration. On a clean machine that folder
    // does not exist, and this -I makes the include resolve here.
    file << "        list(APPEND PACKAGE_INCLUDE_DIRS \"${CMAKE_BINARY_DIR}/refl/${_MOD_NAME}\")\n";
    file << "    endif()\n";
    file << "endforeach()\n\n";

    // =========================================================================
    // Reflection codegen for this configuration
    // =========================================================================
    // Reflection output depends on the defines it is generated under, so a
    // runtime build cannot reuse the editor's. Generate it here, with this
    // build's defines and without DEKI_EDITOR, into this build folder.
    file << "# Reflection codegen (runtime configuration — no DEKI_EDITOR)\n";
    file << "if(NOT DEKI_GXX16)\n";
    file << "    find_program(DEKI_GXX16 NAMES g++-16 g++ PATHS \"C:/msys64/mingw64/bin\" NO_DEFAULT_PATH)\n";
    file << "    if(NOT DEKI_GXX16)\n";
    file << "        find_program(DEKI_GXX16 NAMES g++-16 g++)\n";
    file << "    endif()\n";
    file << "    if(NOT DEKI_GXX16)\n";
    file << "        message(FATAL_ERROR \"Deki reflection codegen needs GCC 16.1+; pass -DDEKI_GXX16=<path to "
            "g++>\")\n";
    file << "    endif()\n";
    file << "endif()\n\n";

    file << "if(_RC_PKG_DIRS)\n";
    file << "    include(\"${DEKI_ENGINE_PATH}/cmake/DekiReflectionCodegen.cmake\")\n";
    file << "    set(_RC_INCS ${PACKAGE_INCLUDE_DIRS}\n";
    file << "        \"${DEKI_ENGINE_PATH}/include\"\n";
    file << "        \"${DEKI_ENGINE_PATH}/third_party\"\n";
    file << "        \"${DEKI_PROJECT_ROOT}/packages\"\n";
    file << "        \"${DEKI_PROJECT_ROOT}/src\")\n";
    // The codegen parses package headers itself, so it needs the third-party
    // include paths the compiler gets from linked targets; without them
    // deki-sdl3-integration fails on <SDL3/SDL.h>.
    file << "    if(DEKI_SDL3_INCLUDE)\n";
    file << "        list(APPEND _RC_INCS \"${DEKI_SDL3_INCLUDE}\")\n";
    file << "    endif()\n";
    file << "    if(DEFINED sdl3_SOURCE_DIR)\n";
    file << "        list(APPEND _RC_INCS \"${sdl3_SOURCE_DIR}/include\")\n";
    file << "    endif()\n";
    file << "    if(DEFINED sdl3_BINARY_DIR)\n";
    file << "        list(APPEND _RC_INCS \"${sdl3_BINARY_DIR}/include\" \"${sdl3_BINARY_DIR}/include-revision\")\n";
    file << "    endif()\n";
    file << "    deki_reflection_codegen(\n";
    file << "        UNIT_PREFIX \"native\"\n";
    file << "        PACKAGE_DIRS ${_RC_PKG_DIRS}\n";
    file << "        PACKAGE_TAGS ${_RC_PKG_TAGS}\n";
    file << "        PACKAGE_PREFIXES ${_RC_PKG_PREFIXES}\n";
    file << "        PACKAGE_OUTDIRS ${_RC_PKG_OUTDIRS}\n";
    file << "        GENERATOR_SRC \"${DEKI_ENGINE_PATH}/tools/reflection_codegen.cpp\" GXX \"${DEKI_GXX16}\"\n";
    file << "        INCLUDE_DIRS ${_RC_INCS}\n";
    {
        const auto codegenExcludes = strip.CodegenExcludeRegexes();
        if (!codegenExcludes.empty())
        {
            file << "        EXCLUDE_REGEX";
            for (const auto& rx : codegenExcludes)
            {
                file << " \"" << CMakeGen::EscapeCMakeString(rx) << "\"";
            }
            file << "\n";
        }
    }
    file << "        DEFINES SIMULATOR DEKI_LOG_ENABLED";
    for (const auto& define : transformDefines)
    {
        file << " " << define;
    }
    file << " \"DEKI_FAST_ATTR=\"\n";
    for (const auto& define : config.defines)
    {
        file << "                " << define << "\n";
    }
    for (const auto& define : packageDefines)
    {
        file << "                " << define << "\n";
    }
    file << "                \"DEKI_SCREEN_WIDTH=" << config.screenWidth << "\"\n";
    file << "                \"DEKI_SCREEN_HEIGHT=" << config.screenHeight << "\")\n";
    // Compile the .gen.cpp this pass produces, not the package's checked-in copy.
    file << "    foreach(_OUTDIR ${_RC_PKG_OUTDIRS})\n";
    file << "        file(GLOB _RC_GEN_SRCS \"${_OUTDIR}/*.gen.cpp\")\n";
    file << "        list(APPEND _ALL_PACKAGE_SOURCES ${_RC_GEN_SRCS})\n";
    file << "    endforeach()\n";
    file << "endif()\n\n";

    // Static registration init file. It defines DekiRegisterProjectPackages(),
    // which the engine calls at startup, as the firmware build does for
    // non-DLL targets. Scan the project root itself: the build folder's
    // parents hold no packages, and an empty init file leaves every scene
    // component "missing".
    const std::string& projRoot = projectPath;
    // engineDefinesSystemInit=true: the static sim links deki-engine-core's
    // empty DekiInitPackageSystems() stub, so the init file puts the packages'
    // *_InitSystem() calls inside DekiRegisterProjectPackages() rather than
    // defining that symbol a second time, which would fail to link.
    std::string packageInitName = fs::path(CMakeGen::GeneratePackageInitFile(buildDir, allPackages, activeIds,
                                                                             fs::path(GetSourceDirectory(projRoot)),
                                                                             /*engineDefinesSystemInit=*/true))
                                      .filename()
                                      .string();

    file << "# Executable\n";
    // Package entry sources come from the package loop above, which keeps only
    // the platform entry that defines main(). Never name a package here.
    file << "add_executable(DekiGame\n";
    file << "    \"${DEKI_ENGINE_PATH}/entry/Main.cpp\"\n";
    file << "    \"${CMAKE_SOURCE_DIR}/" << packageInitName << "\"\n";
    file << "    ${PROJECT_SOURCES}\n";
    file << "    ${_ALL_PACKAGE_SOURCES}\n";
    file << ")\n\n";

    // Run the codegen before compiling, so an edited component header
    // regenerates its metadata instead of compiling against a stale copy.
    file << "foreach(_RC_TAG ${_RC_PKG_TAGS})\n";
    file << "    string(MAKE_C_IDENTIFIER \"${_RC_TAG}\" _RC_TAGID)\n";
    file << "    if(DEKI_RC_TARGETS_${_RC_TAGID})\n";
    file << "        add_dependencies(DekiGame ${DEKI_RC_TARGETS_${_RC_TAGID}})\n";
    file << "    endif()\n";
    file << "endforeach()\n\n";

    // Include paths. The exe includes headers across packages
    // ("deki-rendering/..."), so project/packages is on the path, plus each
    // package folder (PACKAGE_INCLUDE_DIRS). Package folders must come before
    // project/src: a game and a package may share a header name (src/FsmNodes.h
    // and deki-fsm/FsmNodes.h), and a package's generated reflection includes
    // its headers unqualified, so with src first it finds the game's file.
    // Game sources still work: a quoted include looks in the including file's
    // own folder first.
    file << "target_include_directories(DekiGame PRIVATE\n";
    file << "    \"${DEKI_ENGINE_PATH}/include\"\n";
    file << "    \"${DEKI_ENGINE_PATH}/third_party\"\n";
    file << "    \"${DEKI_PROJECT_ROOT}/packages\"\n";
    file << "    ${PACKAGE_INCLUDE_DIRS}\n";
    file << "    \"${DEKI_PROJECT_ROOT}/src\"\n";
    file << ")\n\n";

    // Link deki-engine-core, SDL3, OpenGL and the system libs packages declare (such as winhttp).
    file << "target_link_libraries(DekiGame PRIVATE deki-engine-core ${DEKI_SDL3_TARGET} OpenGL::GL "
            "${_ALL_SYSTEM_LIBS})\n\n";

    // A prebuilt SDL3 is a DLL; it has to sit next to the exe.
    file << "if(TARGET SDL3::SDL3-shared)\n";
    file << "    add_custom_command(TARGET DekiGame POST_BUILD\n";
    file << "        COMMAND ${CMAKE_COMMAND} -E copy_if_different \"$<TARGET_FILE:SDL3::SDL3-shared>\" "
            "\"$<TARGET_FILE_DIR:DekiGame>\"\n";
    file << "        COMMENT \"Copying SDL3 next to DekiGame\")\n";
    file << "endif()\n\n";

    file << "# Platform defines\n";
    file << "target_compile_definitions(DekiGame PRIVATE\n";
    file << "    SIMULATOR\n";
    for (const auto& define : transformDefines)
    {
        file << "    " << define << "\n";
    }
    file << "    ${PACKAGE_DEFINES}\n";
    for (const auto& define : config.defines)
    {
        file << "    " << define << "\n";
    }
    for (const auto& define : packageDefines)
    {
        file << "    " << define << "\n";
    }
    if (m_BuildOptions.enableLogging)
    {
        file << "    DEKI_LOG_ENABLED\n";
    }
    if (m_BuildOptions.enableInternalLogging)
    {
        file << "    DEKI_LOG_INTERNAL_ENABLED\n";
    }
    file << ")\n\n";

    if (!config.cFlags.empty())
    {
        file << "target_compile_options(DekiGame PRIVATE\n";
        file << "    $<$<COMPILE_LANGUAGE:C>:";
        for (const auto& flag : config.cFlags)
        {
            file << " " << flag;
        }
        file << ">\n";
        file << ")\n\n";
    }

    // A platform's cxxFlags may not choose the language standard. They come
    // after the flag CMAKE_CXX_STANDARD adds, so a "-std=" here wins silently,
    // and older simulator platforms carry "-std=c++17" in their JSON. With it
    // the engine fails on <source_location>, with an error pointing at
    // Memory.h. The build sets the standard: drop any "-std=" and warn once.
    std::vector<std::string> cxxFlags;
    std::vector<std::string> droppedStd;
    for (const auto& flag : config.cxxFlags)
    {
        if (flag.rfind("-std=", 0) == 0)
        {
            droppedStd.push_back(flag);
        }
        else
        {
            cxxFlags.push_back(flag);
        }
    }
    if (!droppedStd.empty())
    {
        std::string names;
        for (const auto& f : droppedStd)
        {
            names += (names.empty() ? "" : ", ") + f;
        }
        DEKI_LOG_WARNING("NativeBuilder: ignoring %s from platform '%s' cxxFlags. The C++ standard is set by "
                         "the build (C++23), and a flag here would override it and break the engine headers.",
                         names.c_str(), config.id.c_str());
    }

    if (!cxxFlags.empty())
    {
        file << "target_compile_options(DekiGame PRIVATE\n";
        file << "    $<$<COMPILE_LANGUAGE:CXX>:";
        for (const auto& flag : cxxFlags)
        {
            file << " " << flag;
        }
        file << ">\n";
        file << ")\n\n";
    }

    if (!config.linkFlags.empty())
    {
        file << "target_link_options(DekiGame PRIVATE";
        for (const auto& flag : config.linkFlags)
        {
            file << " " << flag;
        }
        file << ")\n\n";
    }

    // Windows: console subsystem, so the program starts at main() (the platform package entry).
    file << "# Windows: subsystem\n";
    file << "if(WIN32)\n";
    file << "    set_target_properties(DekiGame PROPERTIES WIN32_EXECUTABLE FALSE)\n";
    file << "endif()\n\n";

    // DeployPartitions() copies the storage partitions next to the exe after
    // the build. Not a POST_BUILD command: CMake checks the source folders at
    // configure time, so a partition exported after the first configure would
    // never be copied.

    return CMakeGen::WriteIfChanged(fs::path(buildDir) / "CMakeLists.txt", file.str());
}

// Copies the internal storage next to the exe so the simulator's F:/
// resolves (DesktopFileSystem maps it relative to the working directory, and
// the exe runs from its own folder):
//   flash/ <- project_data.bin, the boot scene and assets/ (F:/assets/),
//             written by FirmwareBuildService into <build>/spiffs_data
// S:/ (storage/) belongs to the game to write; a build leaves it empty.
bool NativeBuilder::DeployPartitions(const std::string& projectPath, const std::string& buildDir,
                                     BuildOutputCallback outputCallback)
{
    const fs::path binDir = fs::path(buildDir) / "build" / "bin";
    (void)projectPath;
    const std::pair<fs::path, const char*> partitions[] = {
        { fs::path(buildDir) / "spiffs_data", "flash" },
    };
    bool ok = true;
    for (const auto& [source, name] : partitions)
    {
        std::error_code ec;
        if (!fs::is_directory(source, ec))
        {
            if (outputCallback)
            {
                outputCallback(
                    std::string("No ") + name + " partition to deploy (" + source.string() + " does not exist)", false);
            }
            continue;
        }
        const fs::path dest = binDir / name;
        fs::remove_all(dest, ec);
        fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            if (outputCallback)
            {
                outputCallback(std::string("Failed to deploy ") + name + " partition: " + ec.message(), true);
            }
            ok = false;
            continue;
        }
        if (outputCallback)
        {
            outputCallback(std::string("Deployed ") + name + " partition next to DekiGame", false);
        }
    }
    return ok;
}

// ============================================================================
// Platform Editor UI
// ============================================================================

class NativeEditorUI : public IPlatformEditorUI
{
public:
    NativeEditorUI(const PlatformConfig& config)
    {
        m_ScreenWidth = config.screenWidth > 0 ? config.screenWidth : 320;
        m_ScreenHeight = config.screenHeight > 0 ? config.screenHeight : 240;
        m_Defines = config.defines;
        m_CFlags = config.cFlags;
        m_CxxFlags = config.cxxFlags;
        m_LinkFlags = config.linkFlags;
    }

    void Draw() override
    {
        // --- Display ---
        if (DekiEditor::SchematicSectionBegin("Display"))
        {
            DekiEditor::BeginPropertyContext();
            DekiEditor::EditorUI::Get().PropertyRow("Resolution");
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float fieldW = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("x").x - gap * 2.0f) * 0.5f;
            ImGui::SetNextItemWidth(fieldW);
            DekiEditor::SchematicDragInt("##ScreenWidth", &m_ScreenWidth, 1.0f, 1, 16384);
            ImGui::SameLine(0.0f, gap);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(DekiEditor::Palette::Dim, "x");
            ImGui::SameLine(0.0f, gap);
            ImGui::SetNextItemWidth(fieldW);
            DekiEditor::SchematicDragInt("##ScreenHeight", &m_ScreenHeight, 1.0f, 1, 16384);
            if (m_ScreenWidth < 1)
            {
                m_ScreenWidth = 1;
            }
            if (m_ScreenHeight < 1)
            {
                m_ScreenHeight = 1;
            }
            DekiEditor::EndPropertyContext();
            DekiEditor::SchematicSectionEnd();
        }

        // --- Compiler & Build ---
        if (DekiEditor::SchematicSectionBegin("Compiler & Build", 0))
        {
            DrawStringListEditor("Preprocessor Defines", m_Defines, m_NewDefineBuf, sizeof(m_NewDefineBuf));
            DrawStringListEditor("C Flags", m_CFlags, m_NewCFlagBuf, sizeof(m_NewCFlagBuf));
            DrawStringListEditor("C++ Flags", m_CxxFlags, m_NewCxxFlagBuf, sizeof(m_NewCxxFlagBuf));
            DrawStringListEditor("Linker Flags", m_LinkFlags, m_NewLinkFlagBuf, sizeof(m_NewLinkFlagBuf));
            DekiEditor::SchematicSectionEnd();
        }
    }

    void ApplyToConfig(PlatformConfig& config) const override
    {
        config.framework = "native";
        config.screenWidth = m_ScreenWidth;
        config.screenHeight = m_ScreenHeight;
        config.defines = m_Defines;
        config.cFlags = m_CFlags;
        config.cxxFlags = m_CxxFlags;
        config.linkFlags = m_LinkFlags;
    }

private:
    int m_ScreenWidth = 320;
    int m_ScreenHeight = 240;

    std::vector<std::string> m_Defines;
    std::vector<std::string> m_CFlags;
    std::vector<std::string> m_CxxFlags;
    std::vector<std::string> m_LinkFlags;

    char m_NewDefineBuf[256] = "";
    char m_NewCFlagBuf[256] = "";
    char m_NewCxxFlagBuf[256] = "";
    char m_NewLinkFlagBuf[256] = "";
};

std::unique_ptr<IPlatformEditorUI> NativeBuilder::CreateEditorUI(const PlatformConfig& config) const
{
    return std::make_unique<NativeEditorUI>(config);
}

}  // namespace DekiEditor

// ---------------------------------------------------------------------------
// Builder plugin entry points
//
// This backend reaches the editor through the same plugin ABI a third-party
// or NDA'd backend uses, so that path is tested on every build.
//
// In editor/ so the editor-side package DLL picks it up and firmware builds,
// which filter editor/ out, do not.
// ---------------------------------------------------------------------------

#include <deki-editor/build/BuilderPlugin.h>

extern "C"
{
    DEKI_BUILDER_API const DekiBuilderAbi* DekiBuilderGetAbi(void)
    {
        static const DekiBuilderAbi kAbi = DekiBuilderThisAbi((uint32_t)sizeof(DekiEditor::PlatformConfig),
                                                              (uint32_t)sizeof(DekiEditor::CMakeGen::PackageEntry));
        return &kAbi;
    }

    DEKI_BUILDER_API const char* DekiBuilderGetName(void)
    {
        return "Native Builder";
    }
    DEKI_BUILDER_API const char* DekiBuilderGetVersion(void)
    {
        return "1.0.0";
    }
    DEKI_BUILDER_API int DekiBuilderGetBuilderCount(void)
    {
        return 1;
    }

    DEKI_BUILDER_API DekiEditor::ITargetBuilder* DekiBuilderCreateBuilder(int index)
    {
        return index == 0 ? new DekiEditor::NativeBuilder() : nullptr;
    }

    DEKI_BUILDER_API void DekiBuilderDestroyBuilder(DekiEditor::ITargetBuilder* builder)
    {
        delete builder;  // in THIS module: its vtable and operator delete live here
    }

}  // extern "C"
