#include "launcher_services.h"

#include "launcher_config.h"
#include "load_order.h"
#include "path_utils.h"
#include "string_utils.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <set>

namespace uml
{
namespace
{
bool EqualsNoCase(const std::string& left, const std::string& right)
{
    return _stricmp(left.c_str(), right.c_str()) == 0;
}

std::string MakeUniqueResourceModId(const LauncherConfig& config, const std::string& gameRoot, const std::string& name)
{
    const std::string base = SanitizeManifestOwner(name);
    std::string candidate = base;
    int suffix = 2;
    for (;;) {
        bool unique = true;
        for (const ResourceModEntry& mod : config.resourceMods) {
            if (EqualsNoCase(mod.id, candidate)) {
                unique = false;
                break;
            }
        }

        if (unique && FileExists(GetInstallManifestPath(gameRoot, candidate).c_str())) {
            unique = false;
        }

        if (unique) {
            return candidate;
        }

        candidate = base + "_" + Uint32ToString(static_cast<uint32_t>(suffix));
        ++suffix;
    }
}

void AppendMatch(std::vector<ModMatch>* matches, ModType type, std::size_t index)
{
    for (const ModMatch& match : *matches) {
        if (match.type == type && match.index == index) {
            return;
        }
    }
    matches->push_back({type, index});
}

struct ManifestOwnerInfo
{
    std::string owner;
    std::string modName;
};

std::vector<ManifestOwnerInfo> GetManifestOwners(const LauncherConfig& config)
{
    std::vector<ManifestOwnerInfo> owners;
    for (const ModEntry& mod : config.mods) {
        owners.push_back({mod.dllName, GetDllModDisplayName(mod)});
    }
    for (const ResourceModEntry& mod : config.resourceMods) {
        owners.push_back({mod.manifestOwner.empty() ? mod.id : mod.manifestOwner, GetResourceModDisplayName(mod)});
    }
    return owners;
}

std::map<std::string, std::vector<ManifestOwnerInfo>> BuildManifestPathOwners(const LauncherConfig& config, const std::string& gameRoot)
{
    std::map<std::string, std::vector<ManifestOwnerInfo>> pathOwners;
    for (const ManifestOwnerInfo& owner : GetManifestOwners(config)) {
        std::vector<std::string> relativePaths;
        if (!ReadInstallManifest(gameRoot, owner.owner, &relativePaths)) {
            continue;
        }
        std::set<std::string> seenForOwner;
        for (const std::string& manifestPath : relativePaths) {
            const std::string relativePath = NormalizeRelativePath(manifestPath);
            if (seenForOwner.insert(ToLower(relativePath)).second) {
                pathOwners[ToLower(relativePath)].push_back(owner);
            }
        }
    }
    return pathOwners;
}
}

std::string GetDllModDisplayName(const ModEntry& mod)
{
    return mod.name.empty() ? mod.dllName : mod.name;
}

std::string GetResourceModDisplayName(const ResourceModEntry& mod)
{
    return mod.name.empty() ? mod.id : mod.name;
}

std::string GetModDisplayName(const LauncherConfig& config, const ModMatch& match)
{
    if (match.type == ModType::Dll) {
        return GetDllModDisplayName(config.mods[match.index]);
    }
    return GetResourceModDisplayName(config.resourceMods[match.index]);
}

std::string GetModManifestOwner(const LauncherConfig& config, const ModMatch& match)
{
    if (match.type == ModType::Dll) {
        return config.mods[match.index].dllName;
    }
    const ResourceModEntry& mod = config.resourceMods[match.index];
    return mod.manifestOwner.empty() ? mod.id : mod.manifestOwner;
}

std::string GetModIdentity(const LauncherConfig& config, const ModMatch& match)
{
    if (match.type == ModType::Dll) {
        return config.mods[match.index].dllName;
    }
    return config.resourceMods[match.index].id;
}

std::vector<ModMatch> FindInstalledMods(const LauncherConfig& config, const std::string& query)
{
    std::vector<ModMatch> matches;
    const std::string trimmedQuery = Trim(query);
    for (std::size_t i = 0; i < config.mods.size(); ++i) {
        const ModEntry& mod = config.mods[i];
        if (EqualsNoCase(mod.dllName, trimmedQuery) || EqualsNoCase(GetDllModDisplayName(mod), trimmedQuery)) {
            AppendMatch(&matches, ModType::Dll, i);
        }
    }

    for (std::size_t i = 0; i < config.resourceMods.size(); ++i) {
        const ResourceModEntry& mod = config.resourceMods[i];
        if (EqualsNoCase(mod.id, trimmedQuery)
            || EqualsNoCase(GetResourceModDisplayName(mod), trimmedQuery)
            || EqualsNoCase(mod.manifestOwner, trimmedQuery)) {
            AppendMatch(&matches, ModType::Resource, i);
        }
    }

    return matches;
}

bool FindSingleInstalledMod(const LauncherConfig& config, const std::string& query, ModMatch* match, std::string* error)
{
    const std::vector<ModMatch> matches = FindInstalledMods(config, query);
    if (matches.empty()) {
        if (error) {
            *error = "No installed mod matches: " + query;
        }
        return false;
    }

    if (matches.size() > 1) {
        if (error) {
            *error = "Mod name is ambiguous: " + query + "\nMatches:";
            for (const ModMatch& candidate : matches) {
                *error += "\n  ";
                *error += GetModDisplayName(config, candidate);
                *error += " (";
                *error += candidate.type == ModType::Dll ? "DLL Mod, " : "Resource Mod, ";
                *error += GetModIdentity(config, candidate);
                *error += ")";
            }
        }
        return false;
    }

    if (match) {
        *match = matches.front();
    }
    return true;
}

bool RenameInstalledMod(LauncherConfig* config, const ModMatch& match, const std::string& newName, std::string* error)
{
    if (!config || Trim(newName).empty()) {
        if (error) {
            *error = "Mod name cannot be empty.";
        }
        return false;
    }

    if (match.type == ModType::Dll) {
        config->mods[match.index].name = Trim(newName);
    }
    else {
        config->resourceMods[match.index].name = Trim(newName);
    }
    return true;
}

bool SetInstalledModStage(LauncherConfig* config, const ModMatch& match, InjectionStage stage, uint32_t delayMs, std::string* error)
{
    if (!config) {
        return false;
    }
    if (stage == InjectionStage::Suspended && delayMs != 0) {
        if (error) {
            *error = "Suspended mods cannot have a delay.";
        }
        return false;
    }

    if (match.type == ModType::Dll) {
        ModEntry& mod = config->mods[match.index];
        mod.stage = stage;
        mod.delayMs = delayMs;
        mod.spec = SerializeModEntry(mod);
    }
    else {
        ResourceModEntry& mod = config->resourceMods[match.index];
        mod.stage = stage;
        mod.delayMs = delayMs;
    }
    return true;
}

bool MoveInstalledMod(LauncherConfig* config, const ModMatch& match, int direction, std::string* error)
{
    if (!config || (direction != -1 && direction != 1)) {
        return false;
    }

    if (match.type == ModType::Dll) {
        const int target = static_cast<int>(match.index) + direction;
        if (target < 0 || target >= static_cast<int>(config->mods.size())) {
            if (error) {
                *error = "Mod cannot be moved further.";
            }
            return false;
        }
        std::swap(config->mods[match.index], config->mods[static_cast<std::size_t>(target)]);
    }
    else {
        const int target = static_cast<int>(match.index) + direction;
        if (target < 0 || target >= static_cast<int>(config->resourceMods.size())) {
            if (error) {
                *error = "Mod cannot be moved further.";
            }
            return false;
        }
        std::swap(config->resourceMods[match.index], config->resourceMods[static_cast<std::size_t>(target)]);
    }
    return true;
}

bool ContainsNoCase(const std::vector<std::string>& values, const std::string& value)
{
    return std::find_if(values.begin(), values.end(), [&value](const std::string& candidate) {
        return EqualsNoCase(candidate, value);
    }) != values.end();
}

const ModEntry* FindDllMod(const LauncherConfig& config, const std::string& dllName)
{
    for (const ModEntry& mod : config.mods) {
        if (EqualsNoCase(mod.dllName, dllName)) {
            return &mod;
        }
    }
    return nullptr;
}

std::vector<ModEntry> ReadPackageLoadOrderHints(const std::string& packageRoot, std::vector<std::string>* warnings)
{
    std::vector<ModEntry> hints;
    std::string packageIniPath;
    if (!FindPackageFileByRelativePath(packageRoot, JoinPath(JoinPath("bin", "Final"), "GameModLauncher.ini"), &packageIniPath)) {
        return hints;
    }

    const std::string loadOrder = ReadIniStringFromFile("Mods", "LoadOrder", "", packageIniPath);
    for (const std::string& spec : SplitLoadOrderList(loadOrder)) {
        ModEntry entry;
        if (!ParseModEntry(spec, &entry)) {
            if (warnings) {
                warnings->push_back("Package GameModLauncher.ini has invalid LoadOrder entry: " + spec);
            }
            continue;
        }

        const std::string section = "Mod:" + entry.dllName;
        entry.name = ReadIniStringFromFile(section.c_str(), "Name", entry.dllName.c_str(), packageIniPath);
        hints.push_back(entry);
    }
    return hints;
}

std::string GetDllRelativePath(const std::string& dllName)
{
    return JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), dllName);
}

std::string GetDllIniRelativePath(const std::string& dllName)
{
    return JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), ReplaceExtension(dllName, ".ini"));
}

PackageDllInstallHint MakePackageDllHint(
    const LauncherConfig& config,
    const std::string& gameRoot,
    const std::string& dllName,
    const ModEntry* packageHint,
    bool presentInPackage)
{
    PackageDllInstallHint hint;
    hint.dllName = dllName;
    hint.presentInPackage = presentInPackage;
    hint.fromPackageLoadOrder = packageHint != nullptr;
    hint.stage = packageHint ? packageHint->stage : InjectionStage::Resume;
    hint.delayMs = packageHint ? packageHint->delayMs : 0;
    hint.displayName = packageHint && !packageHint->name.empty() ? packageHint->name : dllName;

    if (const ModEntry* installed = FindDllMod(config, dllName)) {
        hint.installedInConfig = true;
        if (!packageHint) {
            hint.stage = installed->stage;
            hint.delayMs = installed->delayMs;
        }
        if (!packageHint || hint.displayName == dllName) {
            hint.displayName = GetDllModDisplayName(*installed);
        }
    }

    hint.targetFileExists = FileExists(JoinPath(gameRoot, GetDllRelativePath(dllName)).c_str());
    return hint;
}

ModEntry MakeModEntryFromHint(
    const PackageDllInstallHint& hint,
    const std::string& displayName)
{
    ModEntry entry;
    entry.dllName = hint.dllName;
    entry.dllPath = ResolveModsPath(hint.dllName);
    entry.name = displayName.empty() ? hint.displayName : displayName;
    entry.stage = hint.stage;
    entry.delayMs = hint.delayMs;
    entry.spec = SerializeModEntry(entry);
    return entry;
}

bool InstallModFromPackage(LauncherConfig* config, const InstallModOptions& options, InstallModResult* result, std::string* error)
{
    if (!config) {
        return false;
    }

    std::vector<PackageFile> files;
    const bool useTargetMapping = !Trim(options.packageTargetRelativeDirectory).empty();
    if (useTargetMapping) {
        if (!EnumeratePackageFilesForTarget(options.packageRoot, options.packageTargetRelativeDirectory, &files, error)) {
            return false;
        }
    }
    else if (!EnumeratePackageFiles(options.packageRoot, &files, error)) {
        return false;
    }

    const std::vector<std::string> dllNames = FindPackageDllNames(files);
    const bool isDllMod = !dllNames.empty();
    if (!isDllMod && !PackageHasResourceFiles(files)) {
        if (error) {
            *error = "Package must contain resource files under data or DLL files under bin\\Final\\mods.";
        }
        return false;
    }

    const std::string gameRoot = GetDefaultGameRootDirectory();
    const std::vector<PackageDllInstallHint> dllHints = GetPackageDllInstallHintsForGameRoot(*config, options.packageRoot, files, gameRoot, nullptr);

    std::string dllName = options.dllName;
    if (isDllMod) {
        if (dllName.empty()) {
            if (dllNames.size() > 1) {
                for (const PackageDllInstallHint& hint : dllHints) {
                    if (hint.fromPackageLoadOrder && hint.presentInPackage) {
                        dllName = hint.dllName;
                        break;
                    }
                }
                if (dllName.empty()) {
                    if (error) {
                        *error = "Package contains multiple DLLs. Specify --dll:";
                        for (const std::string& candidate : dllNames) {
                            *error += "\n  ";
                            *error += candidate;
                        }
                    }
                    return false;
                }
            }
            else {
                dllName = dllNames.front();
            }
        }
        else if (std::find_if(dllNames.begin(), dllNames.end(), [&dllName](const std::string& value) { return EqualsNoCase(value, dllName); }) == dllNames.end()) {
            if (error) {
                *error = "Selected DLL is not present in package: " + dllName;
            }
            return false;
        }

        for (const std::string& additionalDllName : options.additionalDllNames) {
            if (!ContainsNoCase(dllNames, additionalDllName)) {
                if (error) {
                    *error = "Selected additional DLL is not present in package: " + additionalDllName;
                }
                return false;
            }
        }
    }

    const std::string displayName = Trim(options.name).empty()
        ? (isDllMod ? dllName : SanitizeManifestOwner(FileNamePart(options.packageRoot)))
        : Trim(options.name);
    const std::string manifestOwner = isDllMod ? dllName : MakeUniqueResourceModId(*config, gameRoot, displayName);

    std::vector<std::string> selectedDllNames;
    std::vector<std::string> skippedRelativePaths;
    if (isDllMod) {
        if (!options.selectedDllNames.empty()) {
            selectedDllNames = options.selectedDllNames;
        }
        else {
            selectedDllNames.push_back(dllName);
            for (const PackageDllInstallHint& hint : dllHints) {
                if (hint.fromPackageLoadOrder) {
                    selectedDllNames.push_back(hint.dllName);
                }
            }
            for (const std::string& additionalDllName : options.additionalDllNames) {
                selectedDllNames.push_back(additionalDllName);
            }
        }

        if (!ContainsNoCase(selectedDllNames, dllName)) {
            selectedDllNames.push_back(dllName);
        }

        std::vector<std::string> orderedSelectedDllNames;
        for (const PackageDllInstallHint& hint : dllHints) {
            if (ContainsNoCase(selectedDllNames, hint.dllName) && !ContainsNoCase(orderedSelectedDllNames, hint.dllName)) {
                orderedSelectedDllNames.push_back(hint.dllName);
            }
        }
        for (const std::string& selectedDllName : selectedDllNames) {
            if (!ContainsNoCase(orderedSelectedDllNames, selectedDllName)) {
                orderedSelectedDllNames.push_back(selectedDllName);
            }
        }
        selectedDllNames = orderedSelectedDllNames;

        std::string missingDecisionMessage;
        for (const std::string& selectedDllName : selectedDllNames) {
            const PackageDllInstallHint* hint = nullptr;
            for (const PackageDllInstallHint& candidate : dllHints) {
                if (EqualsNoCase(candidate.dllName, selectedDllName)) {
                    hint = &candidate;
                    break;
                }
            }
            if (!hint) {
                if (error) {
                    *error = "Selected DLL is not present in package and is not already installed: " + selectedDllName;
                }
                return false;
            }

            if (hint->presentInPackage && hint->targetFileExists) {
                const bool overwrite = ContainsNoCase(options.overwriteDllNames, selectedDllName);
                const bool skip = ContainsNoCase(options.skipDllNames, selectedDllName);
                if (overwrite && skip) {
                    if (error) {
                        *error = "DLL has both overwrite and skip decisions: " + selectedDllName;
                    }
                    return false;
                }
                if (!overwrite && !skip) {
                    missingDecisionMessage += "\n  ";
                    missingDecisionMessage += selectedDllName;
                }
                if (skip) {
                    skippedRelativePaths.push_back(GetDllRelativePath(selectedDllName));
                    skippedRelativePaths.push_back(GetDllIniRelativePath(selectedDllName));
                }
            }
        }

        if (!missingDecisionMessage.empty()) {
            if (error) {
                *error = "These DLLs already exist. Choose --overwrite-dll or --skip-dll for each one:";
                *error += missingDecisionMessage;
            }
            return false;
        }
    }

    PackageInstallResult packageResult;
    if (!InstallModPackageFiles(files, gameRoot, manifestOwner, &packageResult, error, skippedRelativePaths)) {
        return false;
    }

    if (isDllMod) {
        std::vector<ModEntry> remainingMods;
        std::size_t insertIndex = config->mods.size();
        bool foundExistingSelected = false;
        for (const ModEntry& mod : config->mods) {
            if (ContainsNoCase(selectedDllNames, mod.dllName)) {
                if (!foundExistingSelected) {
                    insertIndex = remainingMods.size();
                    foundExistingSelected = true;
                }
                continue;
            }
            remainingMods.push_back(mod);
        }
        if (!foundExistingSelected) {
            insertIndex = remainingMods.size();
        }

        std::vector<ModEntry> selectedEntries;
        for (const std::string& selectedDllName : selectedDllNames) {
            const PackageDllInstallHint* hint = nullptr;
            for (const PackageDllInstallHint& candidate : dllHints) {
                if (EqualsNoCase(candidate.dllName, selectedDllName)) {
                    hint = &candidate;
                    break;
                }
            }
            if (!hint) {
                continue;
            }

            const std::string entryName = EqualsNoCase(selectedDllName, dllName) ? displayName : hint->displayName;
            selectedEntries.push_back(MakeModEntryFromHint(*hint, entryName));
        }

        remainingMods.insert(remainingMods.begin() + static_cast<std::ptrdiff_t>(insertIndex), selectedEntries.begin(), selectedEntries.end());
        config->mods = remainingMods;
    }
    else {
        ResourceModEntry entry;
        entry.id = manifestOwner;
        entry.name = displayName.empty() ? manifestOwner : displayName;
        entry.manifestOwner = manifestOwner;
        entry.stage = InjectionStage::Resume;
        entry.delayMs = 0;
        config->resourceMods.push_back(entry);
    }

    if (result) {
        result->type = isDllMod ? ModType::Dll : ModType::Resource;
        result->displayName = displayName;
        result->manifestOwner = manifestOwner;
        result->dllNames = dllNames;
    }
    return true;
}

bool DeleteInstalledMod(LauncherConfig* config, const ModMatch& match, ModDeleteResult* result, std::string* error)
{
    if (!config) {
        return false;
    }

    const std::string manifestOwner = GetModManifestOwner(*config, match);
    const bool isDllMod = match.type == ModType::Dll;
    std::vector<std::string> protectedRelativePaths;
    if (isDllMod) {
        for (std::size_t i = 0; i < config->mods.size(); ++i) {
            if (i == match.index) {
                continue;
            }
            protectedRelativePaths.push_back(GetDllRelativePath(config->mods[i].dllName));
            protectedRelativePaths.push_back(GetDllIniRelativePath(config->mods[i].dllName));
        }
    }

    if (!DeleteInstalledModFiles(GetDefaultGameRootDirectory(), manifestOwner, isDllMod, result, error, protectedRelativePaths)) {
        return false;
    }

    if (isDllMod) {
        config->mods.erase(config->mods.begin() + static_cast<std::ptrdiff_t>(match.index));
    }
    else {
        config->resourceMods.erase(config->resourceMods.begin() + static_cast<std::ptrdiff_t>(match.index));
    }
    return true;
}

std::vector<PackageDllInstallHint> GetPackageDllInstallHints(
    const LauncherConfig& config,
    const std::string& packageRoot,
    const std::vector<PackageFile>& packageFiles,
    std::vector<std::string>* warnings)
{
    return GetPackageDllInstallHintsForGameRoot(config, packageRoot, packageFiles, GetDefaultGameRootDirectory(), warnings);
}

std::vector<PackageDllInstallHint> GetPackageDllInstallHintsForGameRoot(
    const LauncherConfig& config,
    const std::string& packageRoot,
    const std::vector<PackageFile>& packageFiles,
    const std::string& gameRoot,
    std::vector<std::string>* warnings)
{
    if (warnings) {
        warnings->clear();
    }

    const std::vector<std::string> packageDllNames = FindPackageDllNames(packageFiles);
    const std::vector<ModEntry> packageLoadOrder = ReadPackageLoadOrderHints(packageRoot, warnings);
    std::vector<PackageDllInstallHint> hints;

    for (const ModEntry& packageEntry : packageLoadOrder) {
        const bool presentInPackage = ContainsNoCase(packageDllNames, packageEntry.dllName);
        const bool installed = FindDllMod(config, packageEntry.dllName) != nullptr
            || FileExists(JoinPath(gameRoot, GetDllRelativePath(packageEntry.dllName)).c_str());
        if (!presentInPackage && !installed) {
            if (warnings) {
                warnings->push_back("Package LoadOrder references missing DLL: " + packageEntry.dllName);
            }
            continue;
        }

        if (!ContainsNoCase(packageDllNames, packageEntry.dllName) && !installed) {
            continue;
        }

        bool alreadyAdded = false;
        for (const PackageDllInstallHint& existing : hints) {
            if (EqualsNoCase(existing.dllName, packageEntry.dllName)) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            hints.push_back(MakePackageDllHint(config, gameRoot, packageEntry.dllName, &packageEntry, presentInPackage));
        }
    }

    for (const std::string& dllName : packageDllNames) {
        bool alreadyAdded = false;
        for (const PackageDllInstallHint& existing : hints) {
            if (EqualsNoCase(existing.dllName, dllName)) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            hints.push_back(MakePackageDllHint(config, gameRoot, dllName, nullptr, true));
        }
    }

    return hints;
}

std::vector<ManifestAuditEntry> GetVanillaFileAudit(const LauncherConfig& config, bool changedOnly, bool backedUpOnly)
{
    std::vector<ManifestAuditEntry> audit;
    const std::string gameRoot = GetDefaultGameRootDirectory();

    auto appendOwner = [&](const std::string& owner, const std::string& modName) {
        std::vector<InstallManifestEntryInfo> entries;
        if (!ReadInstallManifestEntriesInfo(gameRoot, owner, &entries)) {
            return;
        }

        for (const InstallManifestEntryInfo& entry : entries) {
            if (entry.action != ManifestInstallAction::Overwritten) {
                continue;
            }
            if (backedUpOnly && entry.backupRelativePath.empty()) {
                continue;
            }
            if (changedOnly && entry.currentState != ManifestCurrentState::Changed) {
                continue;
            }

            audit.push_back({owner, modName, entry});
        }
    };

    for (const ModEntry& mod : config.mods) {
        appendOwner(mod.dllName, GetDllModDisplayName(mod));
    }
    for (const ResourceModEntry& mod : config.resourceMods) {
        appendOwner(mod.manifestOwner.empty() ? mod.id : mod.manifestOwner, GetResourceModDisplayName(mod));
    }

    return audit;
}

std::vector<ModConflictEntry> GetInstalledModConflicts(const LauncherConfig& config)
{
    return GetInstalledModConflictsForGameRoot(config, GetDefaultGameRootDirectory());
}

std::vector<ModConflictEntry> GetInstalledModConflictsForGameRoot(const LauncherConfig& config, const std::string& gameRoot)
{
    std::vector<ModConflictEntry> conflicts;
    const std::map<std::string, std::vector<ManifestOwnerInfo>> pathOwners = BuildManifestPathOwners(config, gameRoot);
    for (const auto& item : pathOwners) {
        const std::vector<ManifestOwnerInfo>& owners = item.second;
        if (owners.size() < 2) {
            continue;
        }

        for (std::size_t i = 0; i < owners.size(); ++i) {
            for (std::size_t j = i + 1; j < owners.size(); ++j) {
                conflicts.push_back({item.first, owners[i].owner, owners[i].modName, owners[j].owner, owners[j].modName});
                conflicts.push_back({item.first, owners[j].owner, owners[j].modName, owners[i].owner, owners[i].modName});
            }
        }
    }
    return conflicts;
}

std::vector<ModConflictEntry> GetPackageConflicts(const LauncherConfig& config, const std::vector<PackageFile>& packageFiles)
{
    return GetPackageConflictsForGameRoot(config, packageFiles, GetDefaultGameRootDirectory());
}

std::vector<ModConflictEntry> GetPackageConflictsForGameRoot(const LauncherConfig& config, const std::vector<PackageFile>& packageFiles, const std::string& gameRoot)
{
    std::vector<ModConflictEntry> conflicts;
    const std::map<std::string, std::vector<ManifestOwnerInfo>> pathOwners = BuildManifestPathOwners(config, gameRoot);
    std::set<std::string> seenPackagePaths;
    for (const PackageFile& file : packageFiles) {
        const std::string key = ToLower(NormalizeRelativePath(file.relativePath));
        if (!seenPackagePaths.insert(key).second) {
            continue;
        }

        const auto found = pathOwners.find(key);
        if (found == pathOwners.end()) {
            continue;
        }

        for (const ManifestOwnerInfo& owner : found->second) {
            conflicts.push_back({key, "<package>", "Selected package", owner.owner, owner.modName});
        }
    }
    return conflicts;
}
}
