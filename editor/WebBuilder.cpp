#include "WebBuilder.h"

#include <deki-editor/BundledToolchain.h>
#include <deki-editor/EditorPaths.h>
#include <deki-editor/EditorSettings.h>
#include <deki-editor/ProcessLaunch.h>
#include <deki-editor/build/CMakeGenUtils.h>
#include <deki-editor/build/ExecutableCMakeGen.h>
#include <deki-editor/build/PlatformConfig.h>
#include <deki-editor/build/ProjectPaths.h>
#include <deki/LogSystem.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace DekiEditor
{

namespace
{

// One version for everyone, so a build does not change with the day it was
// installed on.
constexpr const char* kEmsdkVersion = "7.0.0";

// The page around the game: the canvas and nothing else, centred.
constexpr const char* kShellHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{{{ DEKI_TITLE }}}</title>
<style>
  html, body { margin: 0; height: 100%; background: #0a0b0d; }
  body { display: flex; align-items: center; justify-content: center; }
  /* At its own size: SDL takes the canvas's on-page size for the window's, so
     scaling it here draws the picture into a corner. */
  #canvas { display: block; outline: none; flex: none; }
  #status { position: fixed; color: #8f8f8a; font: 14px monospace; }
</style>
</head>
<body>
<div id="status">Loading...</div>
<canvas id="canvas" tabindex="0" oncontextmenu="event.preventDefault()"></canvas>
<script>
  var Module = {
    canvas: document.getElementById('canvas'),
    setStatus: function (text) { document.getElementById('status').textContent = text; },
    onRuntimeInitialized: function () { document.getElementById('status').remove(); }
  };
</script>
{{{ SCRIPT }}}
</body>
</html>
)HTML";

std::string CMakePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string EmsdkDir()
{
    return (fs::path(EditorPaths::GetToolchainsDir()) / "emsdk").string();
}

bool EmsdkInstalled()
{
    std::error_code ec;
    return fs::exists(fs::path(EmsdkDir()) / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" /
                          "Emscripten.cmake",
                      ec) &&
           fs::exists(fs::path(EmsdkDir()) / ".emscripten", ec);
}

std::string InstalledVersion()
{
    std::ifstream in(fs::path(EmsdkDir()) / "upstream" / "emscripten" / "emscripten-version.txt");
    std::string version;
    std::getline(in, version);
    version.erase(std::remove(version.begin(), version.end(), '"'), version.end());
    return version;
}

// The folder holding a tool emsdk unpacked under <emsdk>/<tool>/<version>/,
// empty when it did not bring its own.
std::string EmsdkToolDir(const char* tool, const char* binSubdir)
{
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::path(EmsdkDir()) / tool, ec))
    {
        if (entry.is_directory(ec))
        {
            return CMakePath((entry.path() / binSubdir).string());
        }
    }
    return "";
}

std::string EmsdkPython()
{
    const std::string dir = EmsdkToolDir("python", "");
#ifdef _WIN32
    return dir.empty() ? "python" : dir + "python.exe";
#else
    return dir.empty() ? "python3" : dir + "bin/python3";
#endif
}

// `cmake -E env`, so the compiler finds its configuration, Python and node on
// every host, without changing the editor's own environment.
std::string EmscriptenEnvPrefix(const std::string& cmake)
{
    const std::string emsdk = CMakePath(EmsdkDir());
    std::string prefix = "\"" + cmake + "\" -E env \"EM_CONFIG=" + emsdk + "/.emscripten\" \"EMSDK=" + emsdk + "\"";
    for (const std::string& dir : { EmsdkToolDir("python", ""), EmsdkToolDir("node", "bin"),
                                    CMakePath((fs::path(EmsdkDir()) / "upstream" / "emscripten").string()) })
    {
        if (!dir.empty())
        {
            prefix += " \"--modify\" \"PATH=path_list_prepend:" + dir + "\"";
        }
    }
    return prefix + " ";
}

}  // namespace

std::string WebBuilder::GetBuildDirectory(const std::string& projectPath) const
{
    return (ProjectPaths::Build(projectPath) / "web").string();
}

bool WebBuilder::IsToolchainInstalled() const
{
    return EmsdkInstalled();
}

std::string WebBuilder::GetToolchainStatus() const
{
    return EmsdkInstalled() ? "Emscripten " + InstalledVersion() : "Emscripten is not installed";
}

std::vector<ToolchainComponent> WebBuilder::GetToolchainComponents() const
{
    ToolchainComponent emsdk;
    emsdk.id = "emscripten";
    emsdk.displayName = "Emscripten";
    emsdk.status = EmsdkInstalled() ? ToolchainComponentStatus::Installed : ToolchainComponentStatus::NotInstalled;
    emsdk.installedVersion = EmsdkInstalled() ? InstalledVersion() : "";
    emsdk.latestVersion = kEmsdkVersion;
    emsdk.canInstall = true;
    emsdk.tooltip = "The compiler for web builds. About 2 GB.";
    return { emsdk };
}

void WebBuilder::InstallToolchainComponent(const std::string& componentId, BuildProgressCallback cb)
{
    if (componentId != "emscripten" || m_Installing.exchange(true))
    {
        return;
    }
    RunOnBuildThread(
        [this, cb]()
        {
            auto report = [&](const std::string& message, float progress)
            {
                SetProgress(BuildState::Building, message, progress);
                if (cb)
                {
                    cb(GetProgress());
                }
            };
            auto log = [](const std::string& line, bool isError)
            {
                if (isError)
                {
                    DEKI_LOG_ERROR("%s", line.c_str());
                }
                else
                {
                    DEKI_LOG_EDITOR("%s", line.c_str());
                }
            };

            const fs::path root = EditorPaths::GetToolchainsDir();
            std::error_code ec;
            fs::create_directories(root, ec);
            const std::string script =
#ifdef _WIN32
                "emsdk.bat";
#else
                "./emsdk";
#endif
            int result = 0;
            if (!fs::exists(root / "emsdk" / "emsdk.py", ec))
            {
                report("Downloading Emscripten's installer...", 0.05f);
                result = m_Toolchain.ExecuteCommand("git clone --depth 1 https://github.com/emscripten-core/emsdk.git",
                                                    root.string(), log, m_CancelRequested);
            }
            if (result == 0)
            {
                report("Installing Emscripten (about 2 GB)...", 0.15f);
                result = m_Toolchain.ExecuteCommand(script + " install " + kEmsdkVersion, (root / "emsdk").string(), log,
                                                    m_CancelRequested);
            }
            if (result == 0)
            {
                report("Activating Emscripten...", 0.9f);
                result = m_Toolchain.ExecuteCommand(script + " activate " + kEmsdkVersion, (root / "emsdk").string(), log,
                                                    m_CancelRequested);
            }

            if (result != 0 || !EmsdkInstalled())
            {
                SetError("Emscripten could not be installed; the Console says why");
            }
            else
            {
                SetProgress(BuildState::Completed, "Emscripten installed", 1.0f);
            }
            if (cb)
            {
                cb(GetProgress());
            }
            m_Installing = false;
        });
}

void WebBuilder::Build(const std::string& projectPath, BuildOutputCallback outputCallback,
                       BuildProgressCallback progressCallback)
{
    RunOnBuildThread([this, projectPath, outputCallback, progressCallback]()
                     { DoBuild(projectPath, outputCallback, progressCallback); });
}

bool WebBuilder::GenerateBuildFiles(const std::string& projectPath, const PlatformConfig& config,
                                    const std::vector<std::string>& packageDefines)
{
    const fs::path buildDir = GetBuildDirectory(projectPath);
    std::error_code ec;
    fs::create_directories(buildDir, ec);

    std::string shell = kShellHtml;
    const std::string token = "{{{ DEKI_TITLE }}}";
    const std::string title = config.displayName.empty() ? fs::path(projectPath).filename().string() : config.displayName;
    shell.replace(shell.find(token), token.size(), title);
    if (!CMakeGen::WriteIfChanged((buildDir / "shell.html").string(), shell))
    {
        DEKI_LOG_ERROR("WebBuilder: cannot write the page in %s", buildDir.string().c_str());
        return false;
    }

    CMakeGen::ExecutableLibrary sdl3 = Sdl3Library();
    const std::string data = CMakePath((buildDir / "spiffs_data").string());
    const std::string page = CMakePath((buildDir / "shell.html").string());
    // WebGL 2 is OpenGL ES 3.0, the API the presenter already draws with. The
    // assets are packed into index.data and appear to the game as its flash.
    // Linked again when any of them changes: the linker cannot see them.
    sdl3.afterLink += "set_target_properties(DekiGame PROPERTIES OUTPUT_NAME index SUFFIX \".html\")\n"
                      "target_link_options(DekiGame PRIVATE\n"
                      "    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1\n"
                      "    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=1MB -sEXIT_RUNTIME=0\n"
                      "    \"--shell-file=" +
                      page + "\"\n    \"--preload-file=" + data +
                      "@/flash\")\n"
                      "file(GLOB_RECURSE _DEKI_WEB_DATA CONFIGURE_DEPENDS \"" +
                      data +
                      "/*\")\n"
                      "set_property(TARGET DekiGame APPEND PROPERTY LINK_DEPENDS \"" +
                      page + "\" ${_DEKI_WEB_DATA})\n\n";

    CMakeGen::ExecutableCMakeOptions options;
    options.builderName = "WebBuilder";
    options.kindLabel = "Web";
    options.codegenUnitPrefix = "web";
    options.allInstalledPackages = true;
    options.engineLogsAlways = false;
    options.enableLogging = m_BuildOptions.enableLogging;
    options.enableInternalLogging = m_BuildOptions.enableInternalLogging;
    options.libraries.push_back(sdl3);

    std::string error;
    if (!CMakeGen::GenerateExecutableCMakeLists(projectPath, buildDir.string(), config, packageDefines, options, error))
    {
        DEKI_LOG_ERROR("WebBuilder: cannot generate the build for platform '%s': %s", config.id.c_str(), error.c_str());
        return false;
    }
    return true;
}

void WebBuilder::DoBuild(const std::string& projectPath, BuildOutputCallback outputCallback,
                         BuildProgressCallback progressCallback)
{
    auto step = [&](const std::string& message, float progress)
    {
        SetProgress(BuildState::Building, message, progress);
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
    };
    auto fail = [&](const std::string& message)
    {
        SetError(message);
        if (progressCallback)
        {
            progressCallback(GetProgress());
        }
    };

    step("Starting web build...", 0.0f);
    if (!IsToolchainInstalled())
    {
        fail("Emscripten is not installed. Install it from the Build panel's toolchain section.");
        return;
    }

    const std::string buildDir = GetBuildDirectory(projectPath);
    if (outputCallback)
    {
        outputCallback("Building for the web...", false);
        outputCallback("Build directory: " + buildDir, false);
    }

    step("Resolving native dependencies...", 0.05f);
    if (!PrepareNativeDependencies(projectPath, outputCallback))
    {
        fail("Failed to resolve native dependencies");
        return;
    }
    if (!GenerateBuildFiles(projectPath, m_PlatformConfig, m_PackageDefines))
    {
        fail("Failed to generate build files");
        return;
    }

    step("Configuring...", 0.1f);
    const std::string cmake = DekiEditor::ResolveTool("cmake");
    const std::string env = EmscriptenEnvPrefix(cmake);
    const std::string toolchainFile =
        CMakePath((fs::path(EmsdkDir()) / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake")
                      .string());
    const std::string configure = env + "\"" + cmake + "\" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=\"" +
                                  CMakePath(DekiEditor::ResolveTool("ninja")) + "\" -DCMAKE_TOOLCHAIN_FILE=\"" +
                                  toolchainFile + "\" -DDEKI_GXX16=\"" + CMakePath(DekiEditor::ProjectCxxCompiler()) +
                                  "\"" + EditorSettings::GetCMakeLocationArgs() + " -B build -S .";
    if (m_CancelRequested)
    {
        fail("Build cancelled");
        return;
    }
    if (int result = m_Toolchain.ExecuteCommand(configure, buildDir, outputCallback, m_CancelRequested); result != 0)
    {
        fail("CMake configure failed with exit code " + std::to_string(result));
        return;
    }

    step("Building...", 0.3f);
    if (m_CancelRequested)
    {
        fail("Build cancelled");
        return;
    }
    const std::string build = env + "\"" + cmake + "\" --build build --config Release";
    if (int result = m_Toolchain.ExecuteCommand(build, buildDir, outputCallback, m_CancelRequested); result != 0)
    {
        fail("Build failed with exit code " + std::to_string(result));
        return;
    }

    SetProgress(BuildState::Completed, "Build succeeded", 1.0f);
    if (progressCallback)
    {
        progressCallback(GetProgress());
    }
}

void WebBuilder::Deploy(const std::string& projectPath, const std::string& /*deployTargetId*/,
                        BuildOutputCallback outputCallback, BuildProgressCallback /*progressCallback*/)
{
    const std::string buildDir = GetBuildDirectory(projectPath);
    std::string folder;
    std::error_code ec;
    for (const char* candidate : { "/build/bin/Release", "/build/bin" })
    {
        if (fs::exists(buildDir + candidate + "/index.html", ec))
        {
            folder = buildDir + candidate;
            break;
        }
    }
    if (folder.empty())
    {
        if (outputCallback)
        {
            outputCallback("index.html not found. Build the project first.", true);
        }
        return;
    }

    // A page's WebAssembly cannot load from a file:// address, so emrun serves
    // the folder and opens the browser.
    const std::string emrun = CMakePath((fs::path(EmsdkDir()) / "upstream" / "emscripten" / "emrun.py").string());
    if (LaunchDetachedInDirectory(EmsdkPython(), { emrun, "--no_emrun_detect", "index.html" }, folder))
    {
        if (outputCallback)
        {
            outputCallback("Opened in the browser: " + folder, false);
        }
    }
    else if (outputCallback)
    {
        outputCallback("Could not start the local web server.", true);
    }
}

}  // namespace DekiEditor
