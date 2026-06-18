#include "path_utils.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <vector>

namespace uml
{
std::string GetModuleDirectory()
{
    char buffer[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, buffer, MAX_PATH);

    std::string path = buffer;
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

std::string JoinPath(const std::string& left, const std::string& right)
{
    if (left.empty()) {
        return right;
    }

    if (right.empty()) {
        return left;
    }

    if (left.back() == '\\' || left.back() == '/') {
        return left + right;
    }

    return left + "\\" + right;
}

bool IsAbsolutePath(const std::string& path)
{
    return (path.size() >= 2 && path[1] == ':')
        || (path.size() >= 2 && path[0] == '\\' && path[1] == '\\')
        || (path.size() >= 2 && path[0] == '/' && path[1] == '/');
}

std::string DirectoryName(const std::string& path)
{
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

std::string FileNamePart(const std::string& path)
{
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return path;
    }

    return path.substr(separator + 1);
}

std::string ReplaceExtension(const std::string& path, const char* extension)
{
    const std::size_t separator = path.find_last_of("\\/");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (separator != std::string::npos && dot < separator)) {
        return path + extension;
    }

    return path.substr(0, dot) + extension;
}

std::string GetModsDirectory()
{
    return JoinPath(GetModuleDirectory(), "mods");
}

std::string GetLauncherIniPath()
{
    return JoinPath(GetModuleDirectory(), "GameModLauncher.ini");
}

std::string ResolveLauncherPath(const std::string& path)
{
    if (IsAbsolutePath(path)) {
        return path;
    }

    return JoinPath(GetModuleDirectory(), path);
}

std::string ResolveModsPath(const std::string& fileName)
{
    if (IsAbsolutePath(fileName)) {
        return fileName;
    }

    return JoinPath(GetModsDirectory(), fileName);
}

std::string ResolveGameAdjacentPath(const std::string& gamePath, const std::string& fileName)
{
    if (IsAbsolutePath(fileName)) {
        return fileName;
    }

    return JoinPath(DirectoryName(ResolveLauncherPath(gamePath)), fileName);
}

bool FileExists(const char* path)
{
    const DWORD attributes = ::GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const char* path)
{
    const DWORD attributes = ::GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileText(const std::string& path, std::string* text)
{
    if (!text) {
        return false;
    }

    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) {
        return false;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    *text = stream.str();
    return true;
}

bool WriteFileText(const std::string& path, const std::string& text)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}
}
