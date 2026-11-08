#pragma once

#include "NativeBuilder.h"

namespace DekiEditor
{

/// Builds a project for the browser with Emscripten: index.html, its
/// WebAssembly and the game's assets, in build/web/build/bin/Release.
///
/// A web game is a hosted game like a desktop one, built from the same
/// generated CMake project with SDL3 from source; what differs is the compiler
/// and what is written around it. So it shares NativeBuilder rather than
/// repeating it. The reflection codegen still runs on the host's GCC.
class WebBuilder : public NativeBuilder
{
public:
    void Build(const std::string& projectPath, BuildOutputCallback outputCallback = nullptr,
               BuildProgressCallback progressCallback = nullptr) override;
    const char* GetDeployLabel() const override { return "Open in Browser"; }
    void Deploy(const std::string& projectPath, const std::string& deployTargetId,
                BuildOutputCallback outputCallback = nullptr,
                BuildProgressCallback progressCallback = nullptr) override;

    bool IsToolchainInstalled() const override;
    std::string GetToolchainStatus() const override;
    std::vector<ToolchainComponent> GetToolchainComponents() const override;
    void InstallToolchainComponent(const std::string& componentId, BuildProgressCallback cb = nullptr) override;
    bool IsToolchainBusy() const override { return m_Installing; }

    const char* GetName() const override { return "Web (Emscripten)"; }
    const char* GetIcon() const override { return ICON_TI_WORLD; }
    const char* GetDescription() const override { return TrMark("Play your project in a web browser"); }
    std::string GetFrameworkId() const override { return "web"; }
    std::string GetBuildDirectory(const std::string& projectPath) const override;

    bool GenerateBuildFiles(const std::string& projectPath, const PlatformConfig& config,
                            const std::vector<std::string>& packageDefines) override;

protected:
    const char* PrebuiltKey() const override { return nullptr; }

private:
    void DoBuild(const std::string& projectPath, BuildOutputCallback outputCallback,
                 BuildProgressCallback progressCallback);
    std::atomic<bool> m_Installing{ false };
};

}  // namespace DekiEditor
