#pragma once

#include <string>
#include <vector>

namespace uml
{
struct PackageFile
{
    std::string sourcePath;
    std::string relativePath;
};

struct PackageInstallResult
{
    std::vector<std::string> installedRelativePaths;
    std::vector<std::string> skippedRelativePaths;
    std::vector<std::string> dllNames;
    bool hasResourceFiles = false;
};

struct ModDeleteResult
{
    std::vector<std::string> deletedRelativePaths;
    std::vector<std::string> restoredRelativePaths;
    std::vector<std::string> skippedRelativePaths;
};

enum class ManifestInstallAction
{
    Created,
    Overwritten
};

enum class ManifestCurrentState
{
    Missing,
    Unchanged,
    Changed
};

struct InstallManifestEntryInfo
{
    std::string owner;
    std::string relativePath;
    ManifestInstallAction action = ManifestInstallAction::Created;
    std::string beforeSha256;
    std::string installedSha256;
    std::string backupRelativePath;
    ManifestCurrentState currentState = ManifestCurrentState::Missing;
};

struct ManifestAuditEntry
{
    std::string owner;
    std::string modName;
    InstallManifestEntryInfo entry;
};

std::string GetDefaultGameRootDirectory();
std::string GetManifestDirectory(const std::string& gameRoot);
std::string GetInstallManifestPath(const std::string& gameRoot, const std::string& manifestOwner);
std::string NormalizeRelativePath(std::string path);
bool IsSafeRelativePath(const std::string& relativePath);
bool IsAllowedPackageRelativePath(const std::string& relativePath);
std::string SanitizeManifestOwner(const std::string& name);
bool EnumeratePackageFiles(const std::string& packageRoot, std::vector<PackageFile>* files, std::string* error);
bool EnumeratePackageFilesForTarget(
    const std::string& packageRoot,
    const std::string& targetRelativeDirectory,
    std::vector<PackageFile>* files,
    std::string* error);
bool FindPackageFileByRelativePath(
    const std::string& packageRoot,
    const std::string& relativePath,
    std::string* sourcePath);
std::vector<std::string> FindPackageDllNames(const std::vector<PackageFile>& files);
bool PackageHasResourceFiles(const std::vector<PackageFile>& files);
bool InstallModPackageFiles(
    const std::vector<PackageFile>& files,
    const std::string& gameRoot,
    const std::string& manifestOwner,
    PackageInstallResult* result,
    std::string* error,
    const std::vector<std::string>& skippedRelativePaths = {});
bool InstallModPackageFromDirectory(
    const std::string& packageRoot,
    const std::string& gameRoot,
    const std::string& manifestOwner,
    PackageInstallResult* result,
    std::string* error);
bool ReadInstallManifestEntriesInfo(const std::string& gameRoot, const std::string& manifestOwner, std::vector<InstallManifestEntryInfo>* entries);
bool ReadInstallManifest(const std::string& gameRoot, const std::string& manifestOwner, std::vector<std::string>* relativePaths);
bool WriteInstallManifest(const std::string& gameRoot, const std::string& manifestOwner, const std::vector<std::string>& relativePaths, std::string* error);
std::vector<std::string> GetLegacyDeleteRelativePaths(const std::string& dllName);
bool DeleteInstalledModFiles(
    const std::string& gameRoot,
    const std::string& manifestOwner,
    bool allowLegacyDllFallback,
    ModDeleteResult* result,
    std::string* error,
    const std::vector<std::string>& protectedRelativePaths = {});
}
