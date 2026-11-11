#include "NativeBuilder.h"
#include "WebBuilder.h"
#include <deki-editor/build/BuilderWidgets.h>
#include <deki-editor/EditorTheme.h>
#include <deki-editor/EditorUI.h>
#include <deki-editor/Localization.h>
#include <deki/LogSystem.h>
#include <deki-editor/SafeNames.h>
#include <deki-editor/EditorHttpUtils.h>
#include <fstream>
#include <cstdio>
#include <deki-editor/Paths.h>
#include <deki-editor/build/ProjectPaths.h>
#include <deki-editor/build/CMakeGenUtils.h>
#include <deki-editor/build/ExecutableCMakeGen.h>
#include <deki-editor/EditorSettings.h>
#include <deki-editor/BundledToolchain.h>
#include <deki-editor/CMakeCacheReader.h>
#include <deki-editor/ProcessLaunch.h>
#include <deki-editor/build/PlatformConfig.h>
#include "imgui.h"
#include "imgui_internal.h"
#include <deki/interop/Plugin.h>
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

    // The tools the editor builds everything with (BundledToolchain.h): the
    // ones its download carries, else this computer's. Named in full, so a
    // computer with no compiler, make or cmake of its own builds as well, and
    // the game is compiled by the compiler the engine expects.
    auto cmakePath = [](std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    };
    const std::string cmake = DekiEditor::ResolveTool("cmake");
    const std::string cxx = cmakePath(DekiEditor::ProjectCxxCompiler());
    const std::string cc = cmakePath(DekiEditor::ProjectCCompiler());

    // A build folder configured before with other tools (Make, or another
    // compiler) is configured afresh: CMake will not change either in place.
    {
        const std::string cacheDir = (fs::path(buildDir) / "build").string();
        const std::string generator = DekiEditor::ReadCMakeCacheVar(cacheDir, "CMAKE_GENERATOR");
        const std::string cachedCxx = cmakePath(DekiEditor::ReadCMakeCacheVar(cacheDir, "CMAKE_CXX_COMPILER"));
        if ((!generator.empty() && generator != "Ninja") || (!cachedCxx.empty() && cachedCxx != cxx))
        {
            std::error_code ec;
            fs::remove(fs::path(cacheDir) / "CMakeCache.txt", ec);
            fs::remove_all(fs::path(cacheDir) / "CMakeFiles", ec);
            if (outputCallback)
            {
                outputCallback("Configuring afresh: this build used " + (generator.empty() ? cachedCxx : generator) +
                                   ", and now builds with Ninja and " + cxx,
                               false);
            }
        }
    }

    std::string configureCmd = "\"" + cmake + "\" -G Ninja -DCMAKE_MAKE_PROGRAM=\"" +
                               cmakePath(DekiEditor::ResolveTool("ninja")) + "\" -DCMAKE_C_COMPILER=\"" + cc +
                               "\" -DCMAKE_CXX_COMPILER=\"" + cxx + "\" -DDEKI_GXX16=\"" + cxx + "\"" +
                               EditorSettings::GetCMakeLocationArgs() + " -B build -S .";
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

    std::string buildCmd = "\"" + cmake + "\" --build build --config Release";
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

const char* NativeBuilder::PrebuiltKey() const
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
    const char* const prebuiltKey = PrebuiltKey();
    const std::string host = prebuiltKey ? prebuiltKey : "";
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
    // The project itself is CMakeGen::GenerateExecutableCMakeLists, shared with
    // the other backends that build one executable for an OS. What is this
    // backend's own is SDL3, and that the simulator compiles every installed
    // package with its debug logging on.
    CMakeGen::ExecutableCMakeOptions options;
    options.builderName = "NativeBuilder";
    options.kindLabel = "Native Desktop";
    options.codegenUnitPrefix = "native";
    options.allInstalledPackages = true;
    options.engineLogsAlways = true;
    options.enableLogging = m_BuildOptions.enableLogging;
    options.enableInternalLogging = m_BuildOptions.enableInternalLogging;
    options.platformFlagsOwnOptimisation = false;
    options.libraries.push_back(Sdl3Library());

    std::string error;
    if (!CMakeGen::GenerateExecutableCMakeLists(projectPath, buildDir, config, packageDefines, options, error))
    {
        DEKI_LOG_ERROR("NativeBuilder: cannot generate the build for platform '%s': %s", config.id.c_str(),
                       error.c_str());
        return false;
    }
    return true;
}

// SDL3. deki-sdl3-integration declares it in package.json under
// dependencies.native (name, version, git, prebuilt archives per host) and
// PrepareNativeDependencies() resolved that before the build files are written.
// Prebuilt: SDL's own release archive, unpacked under <project>/generated/
// deps and used through its CMake package, so the project holds no SDL3
// build tree (on Windows that tree pushes a deep project folder past the
// 260-character path limit). Source: FetchContent at release-<version>,
// pointed at the editor's own checkout when that is the same version, so
// nothing is cloned.
CMakeGen::ExecutableLibrary NativeBuilder::Sdl3Library() const
{
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

#ifdef __APPLE__
    // SDL's Objective-C needs clang, which this GCC build cannot be: the game
    // links the SDL the editor's own build made with clang. A web build is
    // compiled for another machine and takes no prebuilt.
    if (sdl3.cmakeDir.empty() && PrebuiltKey() != nullptr)
    {
        const fs::path clangSdl3 = fs::path(EditorSettings::GetDepsDir()) / "sdl3-clang";
        if (fs::exists(clangSdl3 / "lib" / "cmake" / "SDL3" / "SDL3Config.cmake"))
        {
            sdl3.cmakeDir = (clangSdl3 / "lib" / "cmake" / "SDL3").string();
            sdl3.includeDir = (clangSdl3 / "include").string();
        }
    }
#endif

    std::ostringstream declare;
    declare << "# SDL3 " << sdl3.version << "\n";
    if (!sdl3.cmakeDir.empty())
    {
        declare << "# Prebuilt SDL3, used through its CMake package.\n";
        declare << "set(SDL3_DIR \"" << ToCMakePath(sdl3.cmakeDir) << "\" CACHE PATH \"\" FORCE)\n";
        declare << "find_package(SDL3 " << sdl3.version << " EXACT REQUIRED CONFIG)\n";
        declare << "set(DEKI_SDL3_TARGET SDL3::SDL3)\n";
        declare << "set(DEKI_SDL3_INCLUDE \"" << ToCMakePath(sdl3.includeDir) << "\")\n\n";
    }
    else
    {
        declare << "include(FetchContent)\n";
        const fs::path editorSdl3 = fs::path(EditorSettings::GetDepsDir()) / "sdl3-src";
        if (SdlSourceVersion(editorSdl3) == sdl3.version)
        {
            declare << "if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_SDL3 AND EXISTS \"" << ToCMakePath(editorSdl3.string())
                    << "/CMakeLists.txt\")\n";
            declare << "    set(FETCHCONTENT_SOURCE_DIR_SDL3 \"" << ToCMakePath(editorSdl3.string())
                    << "\" CACHE PATH \"SDL3 source shared with the editor build\")\n";
            declare << "endif()\n";
        }
        declare << "set(SDL_SHARED OFF CACHE BOOL \"\" FORCE)\n";
        declare << "set(SDL_STATIC ON CACHE BOOL \"\" FORCE)\n";
        declare << "set(SDL_TEST OFF CACHE BOOL \"\" FORCE)\n";
        declare << "FetchContent_Declare(SDL3\n";
        declare << "    GIT_REPOSITORY " << sdl3.git << "\n";
        declare << "    GIT_TAG release-" << sdl3.version << "\n";
        declare << "    GIT_SHALLOW TRUE\n";
        declare << ")\n";
        declare << "FetchContent_MakeAvailable(SDL3)\n";
        declare << "set(DEKI_SDL3_TARGET SDL3::SDL3-static)\n";
        declare << "set(DEKI_SDL3_INCLUDE \"\")\n\n";
    }

    CMakeGen::ExecutableLibrary lib;
    lib.declare = declare.str();
    lib.linkTarget = "${DEKI_SDL3_TARGET}";

    // The codegen parses package headers itself, so it needs SDL's include
    // paths, which the compiler gets from the linked target; without them
    // deki-sdl3-integration fails on <SDL3/SDL.h>.
    lib.codegenIncludes = "    if(DEKI_SDL3_INCLUDE)\n"
                          "        list(APPEND _RC_INCS \"${DEKI_SDL3_INCLUDE}\")\n"
                          "    endif()\n"
                          "    if(DEFINED sdl3_SOURCE_DIR)\n"
                          "        list(APPEND _RC_INCS \"${sdl3_SOURCE_DIR}/include\")\n"
                          "    endif()\n"
                          "    if(DEFINED sdl3_BINARY_DIR)\n"
                          "        list(APPEND _RC_INCS \"${sdl3_BINARY_DIR}/include\" "
                          "\"${sdl3_BINARY_DIR}/include-revision\")\n"
                          "    endif()\n";

    // A prebuilt SDL3 is a DLL; it has to sit next to the exe.
    lib.afterLink = "if(TARGET SDL3::SDL3-shared)\n"
                    "    add_custom_command(TARGET DekiGame POST_BUILD\n"
                    "        COMMAND ${CMAKE_COMMAND} -E copy_if_different \"$<TARGET_FILE:SDL3::SDL3-shared>\" "
                    "\"$<TARGET_FILE_DIR:DekiGame>\"\n"
                    "        COMMENT \"Copying SDL3 next to DekiGame\")\n"
                    "endif()\n\n";

    // DeployPartitions() copies the storage partitions next to the exe after
    // the build. Not a POST_BUILD command: CMake checks the source folders at
    // configure time, so a partition exported after the first configure would
    // never be copied.
    return lib;
}

// Copies the internal storage next to the exe so the simulator's F:/
// resolves (the engine's HostFileSystem maps it relative to the working directory, and
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
    NativeEditorUI(const PlatformConfig& config, std::string framework)
        : m_Framework(std::move(framework))
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
            DrawStringListEditor(TrMark("Preprocessor Defines"), m_Defines, m_NewDefineBuf, sizeof(m_NewDefineBuf));
            DrawStringListEditor(TrMark("C Flags"), m_CFlags, m_NewCFlagBuf, sizeof(m_NewCFlagBuf));
            DrawStringListEditor(TrMark("C++ Flags"), m_CxxFlags, m_NewCxxFlagBuf, sizeof(m_NewCxxFlagBuf));
            DrawStringListEditor(TrMark("Linker Flags"), m_LinkFlags, m_NewLinkFlagBuf, sizeof(m_NewLinkFlagBuf));
            DekiEditor::SchematicSectionEnd();
        }
    }

    void ApplyToConfig(PlatformConfig& config) const override
    {
        config.framework = m_Framework;
        config.screenWidth = m_ScreenWidth;
        config.screenHeight = m_ScreenHeight;
        config.defines = m_Defines;
        config.cFlags = m_CFlags;
        config.cxxFlags = m_CxxFlags;
        config.linkFlags = m_LinkFlags;
    }

private:
    std::string m_Framework;
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
    return std::make_unique<NativeEditorUI>(config, GetFrameworkId());
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
        static const DekiBuilderAbi kAbi =
            DekiBuilderThisAbi(static_cast<uint32_t>(sizeof(DekiEditor::PlatformConfig)),
                               static_cast<uint32_t>(sizeof(DekiEditor::CMakeGen::PackageEntry)));
        return &kAbi;
    }

    DEKI_BUILDER_API const char* DekiBuilderGetName(void)
    {
        return "Desktop and Web Builders";
    }
    DEKI_BUILDER_API const char* DekiBuilderGetVersion(void)
    {
        return "1.0.0";
    }
    DEKI_BUILDER_API int DekiBuilderGetBuilderCount(void)
    {
        return 2;
    }

    DEKI_BUILDER_API DekiEditor::ITargetBuilder* DekiBuilderCreateBuilder(int index)
    {
        if (index == 1)
        {
            return new DekiEditor::WebBuilder();
        }
        return index == 0 ? new DekiEditor::NativeBuilder() : nullptr;
    }

    DEKI_BUILDER_API void DekiBuilderDestroyBuilder(DekiEditor::ITargetBuilder* builder)
    {
        delete builder;  // in THIS module: its vtable and operator delete live here
    }

}  // extern "C"

// This package calls ImGui directly, which runs on this DLL's own copy of it. The
// editor hands that copy its context here; without these the first call writes
// through a null context.
extern "C"
{
    DEKI_PLUGIN_API void DekiPluginSetImGuiContext(void* context)
    {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
        if (context != nullptr)
        {
            ImTextInitClassifiers();
        }
    }

    DEKI_PLUGIN_API unsigned int DekiPluginGetImGuiContextSize(void)
    {
        return sizeof(ImGuiContext);
    }
}
