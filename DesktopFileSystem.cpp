#include "DesktopFileSystem.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace Deki
{

DesktopFileSystem::DesktopFileSystem()
    : m_Initialized(false)
{
}

DesktopFileSystem::~DesktopFileSystem()
{
    Shutdown();
}

bool DesktopFileSystem::Initialize()
{
    if (m_Initialized)
    {
        return true;
    }

    // Files are relative to the current working directory. The simulator mirrors
    // the device's two storage mounts:
    //   "S:/" (SD card) -> "./storage/"  (exported/packed assets)
    //   "F:/" (flash)   -> "./flash/"    (boot scene + project settings, from the SPIFFS export)
    m_BasePath = "./storage/";
    m_FlashPath = "./flash/";
    m_Initialized = true;
    return true;
}

void DesktopFileSystem::Shutdown()
{
    m_Initialized = false;
}

std::string DesktopFileSystem::ConvertPathInternal(const char* virtualPath)
{
    if (!virtualPath)
    {
        return "";
    }

    std::string path(virtualPath);
    // Convert "S:/..." to "./storage/..." (SD card mount)
    if (path.length() >= 3 && path.substr(0, 3) == "S:/")
    {
        path = m_BasePath + path.substr(3);  // Remove "S:/" and prepend m_BasePath
    }
    // Convert "F:/..." to "./flash/..." (flash mount: boot scene + project settings)
    else if (path.length() >= 3 && path.substr(0, 3) == "F:/")
    {
        path = m_FlashPath + path.substr(3);  // Remove "F:/" and prepend m_FlashPath
    }
    return path;
}

IFileSystem::FileHandle DesktopFileSystem::OpenFile(const char* path, OpenMode mode)
{
    if (!m_Initialized || !path)
    {
        return nullptr;
    }

    std::string realPath = ConvertPathInternal(path);
    const char* modeStr = "";
    bool isWrite = false;

    switch (mode)
    {
        case OpenMode::ReadBinary: modeStr = "rb"; break;
        case OpenMode::WriteBinary:
            modeStr = "wb";
            isWrite = true;
            break;
        case OpenMode::ReadText: modeStr = "r"; break;
        case OpenMode::WriteText:
            modeStr = "w";
            isWrite = true;
            break;
    }

    if (isWrite)
    {
        std::error_code ec;
        std::filesystem::path parent = std::filesystem::path(realPath).parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, ec);
        }
    }

    FILE* file = fopen(realPath.c_str(), modeStr);
    return static_cast<FileHandle>(file);
}

void DesktopFileSystem::CloseFile(FileHandle handle)
{
    if (handle)
    {
        fclose(static_cast<FILE*>(handle));
    }
}

size_t DesktopFileSystem::ReadFile(FileHandle handle, void* buffer, size_t size)
{
    if (!handle || !buffer)
    {
        return 0;
    }
    return fread(buffer, 1, size, static_cast<FILE*>(handle));
}

size_t DesktopFileSystem::WriteFile(FileHandle handle, const void* buffer, size_t size)
{
    if (!handle || !buffer)
    {
        return 0;
    }
    return fwrite(buffer, 1, size, static_cast<FILE*>(handle));
}

long DesktopFileSystem::SeekFile(FileHandle handle, long offset, SeekOrigin origin)
{
    if (!handle)
    {
        return -1;
    }

    int whence = SEEK_SET;
    switch (origin)
    {
        case SeekOrigin::Begin: whence = SEEK_SET; break;
        case SeekOrigin::Current: whence = SEEK_CUR; break;
        case SeekOrigin::End: whence = SEEK_END; break;
    }

    if (fseek(static_cast<FILE*>(handle), offset, whence) == 0)
    {
        return ftell(static_cast<FILE*>(handle));
    }
    return -1;
}

long DesktopFileSystem::TellFile(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }
    return ftell(static_cast<FILE*>(handle));
}

long DesktopFileSystem::GetFileSize(FileHandle handle)
{
    if (!handle)
    {
        return -1;
    }

    FILE* file = static_cast<FILE*>(handle);
    long currentPos = ftell(file);

    if (fseek(file, 0, SEEK_END) != 0)
    {
        return -1;
    }

    long size = ftell(file);

    // Restore original position
    fseek(file, currentPos, SEEK_SET);

    return size;
}

bool DesktopFileSystem::FileExists(const char* path)
{
    if (!m_Initialized || !path)
    {
        return false;
    }

    std::string realPath = ConvertPathInternal(path);
    FILE* file = fopen(realPath.c_str(), "rb");

    if (file)
    {
        fclose(file);
        return true;
    }

    return false;
}

bool DesktopFileSystem::ConvertPath(const char* virtualPath, char* outBuffer, size_t bufferSize)
{
    if (!virtualPath || !outBuffer || bufferSize == 0)
    {
        return false;
    }

    std::string converted = ConvertPathInternal(virtualPath);
    if (converted.length() >= bufferSize)
    {
        return false;
    }

    strcpy(outBuffer, converted.c_str());
    return true;
}

}  // namespace Deki
