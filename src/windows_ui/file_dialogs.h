#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace uml::windows_ui
{
bool PickFolder(HWND owner, const char* title, std::string* path);
bool PickZipFile(HWND owner, std::string* path);
bool PickModPackageFile(HWND owner, std::string* path);
bool PickStringFromList(HWND owner, const char* title, const std::vector<std::string>& items, std::string* selectedItem);
bool CreateTempDirectory(std::string* path, std::string* error);
bool ExtractZipToDirectory(const std::string& zipPath, const std::string& targetDirectory, std::string* error);
bool ExtractArchiveToDirectory(const std::string& archivePath, const std::string& targetDirectory, std::string* error);
}
