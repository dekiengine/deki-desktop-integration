#pragma once

#include <deki/providers/IFileSystem.h>
#include <string>

/// Desktop file system on the C standard library. Maps the device's virtual
/// mounts to local folders. Used on Windows, Linux and macOS, in both editor
/// and simulator builds.

namespace Deki
{

class DesktopFileSystem : public IFileSystem
{
private:
    std::string m_BasePath;   // "S:/" (SD card) -> exported assets
    std::string m_FlashPath;  // "F:/" (flash)   -> boot scene + project settings
    bool m_Initialized;

    /// Turns a virtual path ("S:/assets/texture.bin", "F:/boot.scene") into a
    /// real filesystem path.
    std::string ConvertPathInternal(const char* virtualPath);

public:
    DesktopFileSystem();
    virtual ~DesktopFileSystem();

    // IFileSystem
    bool Initialize() override;
    void Shutdown() override;
    FileHandle OpenFile(const char* path, OpenMode mode) override;
    void CloseFile(FileHandle handle) override;
    size_t ReadFile(FileHandle handle, void* buffer, size_t size) override;
    size_t WriteFile(FileHandle handle, const void* buffer, size_t size) override;
    long SeekFile(FileHandle handle, long offset, SeekOrigin origin) override;
    long TellFile(FileHandle handle) override;
    long GetFileSize(FileHandle handle) override;
    bool FileExists(const char* path) override;
    bool ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize) override;
};

}  // namespace Deki
