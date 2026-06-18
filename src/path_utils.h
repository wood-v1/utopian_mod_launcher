#pragma once

#include <string>

namespace uml
{
std::string GetModuleDirectory();
std::string JoinPath(const std::string& left, const std::string& right);
bool IsAbsolutePath(const std::string& path);
std::string DirectoryName(const std::string& path);
std::string FileNamePart(const std::string& path);
std::string ReplaceExtension(const std::string& path, const char* extension);
std::string GetModsDirectory();
std::string GetLauncherIniPath();
std::string ResolveLauncherPath(const std::string& path);
std::string ResolveModsPath(const std::string& fileName);
std::string ResolveGameAdjacentPath(const std::string& gamePath, const std::string& fileName);
bool FileExists(const char* path);
bool DirectoryExists(const char* path);
bool ReadFileText(const std::string& path, std::string* text);
bool WriteFileText(const std::string& path, const std::string& text);
}
