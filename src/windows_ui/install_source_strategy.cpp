#include "install_source_strategy.h"

#include "file_dialogs.h"

#include "../path_utils.h"

#include <cstring>
#include <utility>

namespace uml::windows_ui
{
namespace
{
std::string RemoveExtension(const std::string& fileName)
{
    const std::size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos) {
        return fileName;
    }
    return fileName.substr(0, dot);
}

bool HasExtensionNoCase(const std::string& path, const char* extension)
{
    const std::size_t extensionLength = std::strlen(extension);
    return path.size() >= extensionLength
        && _stricmp(path.c_str() + path.size() - extensionLength, extension) == 0;
}
}

std::string DefaultModNameFromPath(const std::string& path)
{
    return RemoveExtension(FileNamePart(path));
}

bool HasPackageArchiveExtension(const std::string& path)
{
    return HasExtensionNoCase(path, ".zip") || HasExtensionNoCase(path, ".rar");
}

bool FolderInstallStrategy::Prepare(HWND owner, PreparedInstallPackage* package, std::string* error)
{
    if (!package) {
        return false;
    }

    std::string folder;
    if (!PickFolder(owner, "Choose mod package folder", &folder)) {
        if (error) {
            error->clear();
        }
        return false;
    }

    package->packageRoot = folder;
    package->defaultName = DefaultModNameFromPath(folder);
    package->sourceKind = InstallSourceKind::Folder;
    return true;
}

ArchiveInstallStrategy::ArchiveInstallStrategy(std::string archivePath)
    : archivePath_(std::move(archivePath))
{
}

bool ArchiveInstallStrategy::Prepare(HWND, PreparedInstallPackage* package, std::string* error)
{
    if (!package) {
        return false;
    }

    std::string tempDirectory;
    if (!CreateTempDirectory(&tempDirectory, error) || !ExtractArchiveToDirectory(archivePath_, tempDirectory, error)) {
        return false;
    }

    package->packageRoot = tempDirectory;
    package->defaultName = DefaultModNameFromPath(archivePath_);
    package->tempDirectory = tempDirectory;
    package->sourceKind = HasExtensionNoCase(archivePath_, ".rar") ? InstallSourceKind::Rar : InstallSourceKind::Zip;
    return true;
}
}
