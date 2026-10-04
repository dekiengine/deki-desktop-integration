#pragma once

#include <deki-editor/build/TargetBuilder.h>
#include <string>
#include <vector>
#include <atomic>

namespace DekiEditor
{

/// Finds the system C++ compilers (MSVC, MinGW, GCC, Clang) and CMake, and
/// runs build commands for native desktop targets.
class NativeToolchain
{
public:
    struct CompilerInfo
    {
        std::string name;
        std::string compilerPath;
        std::string linkerPath;
        std::string version;
        bool isValid = false;
    };

    // Compiler detection
    void ScanSystemCompilers() const;
    std::string FindVSInstallation() const;
    std::string FindCMake() const;

    // Toolchain status
    bool IsInstalled() const;
    std::string GetStatus() const;
    std::vector<ToolchainComponent> GetComponents() const;

    /// Runs `command` in `workDir`, passing each output line to
    /// `outputCallback`. Returns the exit code, or -1 when it cannot start.
    int ExecuteCommand(const std::string& command, const std::string& workDir, BuildOutputCallback outputCallback,
                       std::atomic<bool>& cancelRequested);

    /// Runs `command` without a console window and returns its output, trimmed.
    /// Windows only; returns "" elsewhere.
    static std::string RunCommandSilent(const std::string& command);

private:
    mutable bool m_HasScanned = false;
    mutable std::vector<CompilerInfo> m_Compilers;
    mutable std::string m_CMakePath;
};

}  // namespace DekiEditor
