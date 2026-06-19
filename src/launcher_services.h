#pragma once

#include "launcher_types.h"
#include "mod_package.h"

#include <string>
#include <vector>

namespace uml
{
struct ModMatch
{
    ModType type = ModType::Dll;
    std::size_t index = 0;
};

struct InstallModOptions
{
    std::string packageRoot;
    std::string packageTargetRelativeDirectory;
    std::string name;
    std::string dllName;
    std::vector<std::string> additionalDllNames;
    std::vector<std::string> selectedDllNames;
    std::vector<std::string> overwriteDllNames;
    std::vector<std::string> skipDllNames;
    std::vector<std::string> keepSharedDllNames;
};

struct InstallModResult
{
    ModType type = ModType::Dll;
    std::string displayName;
    std::string manifestOwner;
    std::vector<std::string> dllNames;
};

struct ModConflictEntry
{
    std::string relativePath;
    std::string owner;
    std::string modName;
    std::string otherOwner;
    std::string otherModName;
};

struct PackageDllInstallHint
{
    std::string dllName;
    std::string displayName;
    InjectionStage stage = InjectionStage::Resume;
    uint32_t delayMs = 0;
    bool fromPackageLoadOrder = false;
    bool presentInPackage = false;
    bool sharedDependency = false;
    bool installedInConfig = false;
    bool targetFileExists = false;
};

std::string GetDllModDisplayName(const ModEntry& mod);
std::string GetSharedDllDisplayName(const LauncherConfig& config, const std::string& dllName);
std::string GetResourceModDisplayName(const ResourceModEntry& mod);
ModType GetDllModType(const LauncherConfig& config, const std::string& dllName);
std::string GetDllPackageName(const LauncherConfig& config, const std::string& dllName);
std::string GetModDisplayName(const LauncherConfig& config, const ModMatch& match);
std::string GetModManifestOwner(const LauncherConfig& config, const ModMatch& match);
std::string GetModIdentity(const LauncherConfig& config, const ModMatch& match);
bool IsSharedDll(const LauncherConfig& config, const std::string& dllName);
std::string GetSharedDllManifestOwner(const LauncherConfig& config, const std::string& dllName);
std::vector<ModMatch> FindInstalledMods(const LauncherConfig& config, const std::string& query);
bool FindSingleInstalledMod(const LauncherConfig& config, const std::string& query, ModMatch* match, std::string* error);
bool RenameInstalledMod(LauncherConfig* config, const ModMatch& match, const std::string& newName, std::string* error);
bool SetInstalledModStage(LauncherConfig* config, const ModMatch& match, InjectionStage stage, uint32_t delayMs, std::string* error);
bool MoveInstalledMod(LauncherConfig* config, const ModMatch& match, int direction, std::string* error);
bool InstallModFromPackage(LauncherConfig* config, const InstallModOptions& options, InstallModResult* result, std::string* error);
bool DeleteInstalledMod(LauncherConfig* config, const ModMatch& match, ModDeleteResult* result, std::string* error);
std::vector<PackageDllInstallHint> GetPackageDllInstallHints(
    const LauncherConfig& config,
    const std::string& packageRoot,
    const std::vector<PackageFile>& packageFiles,
    std::vector<std::string>* warnings);
std::vector<PackageDllInstallHint> GetPackageDllInstallHintsForGameRoot(
    const LauncherConfig& config,
    const std::string& packageRoot,
    const std::vector<PackageFile>& packageFiles,
    const std::string& gameRoot,
    std::vector<std::string>* warnings);
std::vector<ManifestAuditEntry> GetVanillaFileAudit(const LauncherConfig& config, bool changedOnly, bool backedUpOnly);
std::vector<ModConflictEntry> GetInstalledModConflicts(const LauncherConfig& config);
std::vector<ModConflictEntry> GetInstalledModConflictsForGameRoot(const LauncherConfig& config, const std::string& gameRoot);
std::vector<ModConflictEntry> GetPackageConflicts(const LauncherConfig& config, const std::vector<PackageFile>& packageFiles);
std::vector<ModConflictEntry> GetPackageConflictsForGameRoot(const LauncherConfig& config, const std::vector<PackageFile>& packageFiles, const std::string& gameRoot);
}
