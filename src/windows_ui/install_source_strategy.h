#pragma once

#include <windows.h>

#include <string>

namespace uml::windows_ui
{
enum class InstallSourceKind
{
    Folder,
    Zip,
    Rar,
    SevenZip
};

struct PreparedInstallPackage
{
    std::string packageRoot;
    std::string defaultName;
    std::string tempDirectory;
    InstallSourceKind sourceKind = InstallSourceKind::Folder;
};

class InstallSourceStrategy
{
public:
    virtual ~InstallSourceStrategy() = default;
    virtual bool Prepare(HWND owner, PreparedInstallPackage* package, std::string* error) = 0;
};

class FolderInstallStrategy final : public InstallSourceStrategy
{
public:
    bool Prepare(HWND owner, PreparedInstallPackage* package, std::string* error) override;
};

class ArchiveInstallStrategy final : public InstallSourceStrategy
{
public:
    explicit ArchiveInstallStrategy(std::string archivePath);
    bool Prepare(HWND owner, PreparedInstallPackage* package, std::string* error) override;

private:
    std::string archivePath_;
};

std::string DefaultModNameFromPath(const std::string& path);
bool HasPackageArchiveExtension(const std::string& path);
}
