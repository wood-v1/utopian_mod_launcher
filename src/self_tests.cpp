#include "self_tests.h"

#include "launcher_config.h"
#include "launcher_cli.h"
#include "launcher_services.h"
#include "launcher_types.h"
#include "load_order.h"
#include "mod_ini.h"
#include "mod_package.h"
#include "path_utils.h"
#include "string_utils.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace uml
{
namespace
{
bool EnsureDirectoryForTest(const std::string& directory)
{
    if (directory.empty() || DirectoryExists(directory.c_str())) {
        return true;
    }

    const std::string parent = DirectoryName(directory);
    if (parent != directory && !DirectoryExists(parent.c_str()) && !EnsureDirectoryForTest(parent)) {
        return false;
    }

    return ::CreateDirectoryA(directory.c_str(), nullptr) != FALSE || ::GetLastError() == ERROR_ALREADY_EXISTS;
}
}

bool RunSelfTests()
{
    bool ok = true;
    auto require = [&ok](bool condition, const char* message) {
        if (!condition) {
            std::printf("SELFTEST FAILED: %s\n", message);
            ok = false;
        }
    };

    ModEntry entry;
    require(ParseModEntry("PPMM.dll@ui+3000", &entry), "parse @ui+3000");
    require(entry.stage == InjectionStage::Ui, "stage ui");
    require(entry.delayMs == 3000, "delay 3000");
    require(SerializeModEntry(entry) == "PPMM.dll@ui+3000", "serialize @ui+3000");
    char appName[] = "GameModLauncher.exe";
    char listCommand[] = "list";
    char installCommand[] = "install";
    char vanillaCommand[] = "vanilla-files";
    char conflictsCommand[] = "conflicts";
    char helpCommand[] = "help";
    char versionCommand[] = "--version";
    char uiCommand[] = "--ui";
    char* listArgv[] = {appName, listCommand};
    char* installArgv[] = {appName, installCommand};
    char* vanillaArgv[] = {appName, vanillaCommand};
    char* conflictsArgv[] = {appName, conflictsCommand};
    char* helpArgv[] = {appName, helpCommand};
    char* versionArgv[] = {appName, versionCommand};
    char* uiArgv[] = {appName, uiCommand};
    require(IsCliCommand(2, listArgv), "cli recognizes list");
    require(IsCliCommand(2, installArgv), "cli recognizes install");
    require(IsCliCommand(2, vanillaArgv), "cli recognizes vanilla-files");
    require(IsCliCommand(2, conflictsArgv), "cli recognizes conflicts");
    require(IsCliCommand(2, helpArgv), "cli recognizes help");
    require(IsCliCommand(2, versionArgv), "cli recognizes version");
    require(!IsCliCommand(2, uiArgv), "cli leaves ui command to main");

    char tempPath[MAX_PATH] = {};
    char tempFile[MAX_PATH] = {};
    ::GetTempPathA(MAX_PATH, tempPath);
    ::GetTempFileNameA(tempPath, "uml", 0, tempFile);
    const std::string iniPath = tempFile;

    WriteIniStringToFile("General", "GamePath", "Game.exe", iniPath);
    WriteIniStringToFile("Mods", "LoadOrder", "A.dll@suspended, B.dll@engine, C.dll@ui+3000", iniPath);
    WriteIniStringToFile("Mod:B.dll", "Name", "Better B", iniPath);

    LauncherConfig config;
    std::string error;
    require(LoadLauncherConfig(iniPath, &config, &error), "load old ini without stages");
    require(config.engineWait.timeoutMs == kDefaultStageTimeoutMs, "default engine timeout");
    require(config.uiWait.timeoutMs == kDefaultStageTimeoutMs, "default ui timeout");
    require(config.mods.size() == 3, "load three mods");
    require(config.mods[1].name == "Better B", "dll mod display name");

    WriteIniStringToFile("Stages", "EngineTimeoutMs", "oops", iniPath);
    require(LoadLauncherConfig(iniPath, &config, &error), "load invalid timeout ini");
    require(config.engineWait.timeoutMs == kDefaultStageTimeoutMs, "invalid timeout falls back");

    config.resourceMods.clear();
    ResourceModEntry resourceConfigEntry;
    resourceConfigEntry.id = "resource_pack";
    resourceConfigEntry.name = "Resource Pack";
    resourceConfigEntry.description = "Resource description";
    resourceConfigEntry.manifestOwner = "resource_pack";
    resourceConfigEntry.filesToDelete.push_back(JoinPath(JoinPath("data", "UI"), "generated.xml"));
    resourceConfigEntry.stage = InjectionStage::Ui;
    resourceConfigEntry.delayMs = 250;
    config.resourceMods.push_back(resourceConfigEntry);
    require(SaveLauncherConfig(iniPath, config, &error), "save resource mod config");
    LauncherConfig loadedResourceConfig;
    require(LoadLauncherConfig(iniPath, &loadedResourceConfig, &error), "load resource mod config");
    require(loadedResourceConfig.resourceMods.size() == 1, "resource mod count");
    require(loadedResourceConfig.resourceMods[0].name == "Resource Pack", "resource mod name");
    require(loadedResourceConfig.resourceMods[0].description == "Resource description", "resource mod description");
    require(loadedResourceConfig.resourceMods[0].filesToDelete.size() == 1, "resource mod cleanup count");
    require(loadedResourceConfig.resourceMods[0].stage == InjectionStage::Ui, "resource mod stage");
    require(loadedResourceConfig.resourceMods[0].delayMs == 250, "resource mod delay");
    ModMatch renameMatch{ModType::Resource, 0};
    require(RenameInstalledMod(&loadedResourceConfig, renameMatch, "Renamed Resource Pack", &error), "rename resource service");
    require(loadedResourceConfig.resourceMods[0].name == "Renamed Resource Pack", "renamed resource name");

    loadedResourceConfig.sharedDlls.clear();
    SharedDllEntry sharedConfigEntry;
    sharedConfigEntry.dllName = "OynonTools.dll";
    sharedConfigEntry.name = "OynonTools";
    sharedConfigEntry.manifestOwner = "shared-OynonTools.dll";
    sharedConfigEntry.requiredBy.push_back("StaminaSystem.dll");
    sharedConfigEntry.stage = InjectionStage::Suspended;
    loadedResourceConfig.sharedDlls.push_back(sharedConfigEntry);
    ModEntry sharedLoadOrderEntry;
    require(ParseModEntry("OynonTools.dll@suspended", &sharedLoadOrderEntry), "parse shared dll loadorder");
    sharedLoadOrderEntry.name = "OynonTools";
    loadedResourceConfig.mods.insert(loadedResourceConfig.mods.begin(), sharedLoadOrderEntry);
    require(SaveLauncherConfig(iniPath, loadedResourceConfig, &error), "save shared dll config");
    LauncherConfig loadedSharedConfig;
    require(LoadLauncherConfig(iniPath, &loadedSharedConfig, &error), "load shared dll config");
    require(loadedSharedConfig.sharedDlls.size() == 1, "shared dll count");
    require(IsSharedDll(loadedSharedConfig, "OynonTools.dll"), "shared dll role");
    require(GetSharedDllManifestOwner(loadedSharedConfig, "OynonTools.dll") == "shared-OynonTools.dll", "shared dll manifest owner");
    bool loadedMissingSharedDll = false;
    for (const ModEntry& mod : loadedSharedConfig.mods) {
        loadedMissingSharedDll = loadedMissingSharedDll || mod.dllName == "OynonTools.dll";
    }
    require(!loadedMissingSharedDll, "missing shared dll is not materialized as installed mod");
    LauncherConfig manualSharedConfig = loadedSharedConfig;
    manualSharedConfig.mods.insert(manualSharedConfig.mods.begin(), sharedLoadOrderEntry);
    ModMatch sharedDeleteMatch{ModType::SharedDll, 0};
    ModDeleteResult blockedDeleteResult;
    require(!DeleteInstalledMod(&manualSharedConfig, sharedDeleteMatch, &blockedDeleteResult, &error), "shared dll required-by blocks delete");

    InstalledPackageEntry packageConfigEntry;
    packageConfigEntry.id = "stamina-system";
    packageConfigEntry.name = "Stamina System";
    packageConfigEntry.description = "Stamina package description";
    packageConfigEntry.manifestOwner = "stamina-system";
    packageConfigEntry.primaryDll = "StaminaSystem.dll";
    packageConfigEntry.dlls.push_back("PPMM.dll");
    packageConfigEntry.dlls.push_back("StaminaSystem.dll");
    packageConfigEntry.sharedDlls.push_back("OynonTools.dll");
    packageConfigEntry.filesToDelete.push_back(JoinPath(JoinPath("data", "UI"), "playerstat_base.xml"));
    loadedSharedConfig.packages.push_back(packageConfigEntry);
    ModEntry ppmmPackageEntry;
    require(ParseModEntry("PPMM.dll@engine", &ppmmPackageEntry), "parse package dependency");
    loadedSharedConfig.mods.push_back(ppmmPackageEntry);
    ModEntry staminaPackageEntry;
    require(ParseModEntry("StaminaSystem.dll@ui+3000", &staminaPackageEntry), "parse package primary");
    loadedSharedConfig.mods.push_back(staminaPackageEntry);
    require(SaveLauncherConfig(iniPath, loadedSharedConfig, &error), "save package config");
    LauncherConfig loadedPackageConfig;
    require(LoadLauncherConfig(iniPath, &loadedPackageConfig, &error), "load package config");
    require(loadedPackageConfig.packages.size() == 1, "package config count");
    require(loadedPackageConfig.packages[0].description == "Stamina package description", "package description");
    require(loadedPackageConfig.packages[0].filesToDelete.size() == 1, "package cleanup count");
    require(GetDllModType(loadedPackageConfig, "StaminaSystem.dll") == ModType::Dll, "package primary role");
    require(GetDllModType(loadedPackageConfig, "PPMM.dll") == ModType::DllDependency, "package dependency role");
    require(GetDllModType(loadedPackageConfig, "OynonTools.dll") == ModType::SharedDll, "package shared role");
    std::size_t packagePrimaryIndex = loadedPackageConfig.mods.size();
    std::size_t packageDependencyIndex = loadedPackageConfig.mods.size();
    for (std::size_t i = 0; i < loadedPackageConfig.mods.size(); ++i) {
        if (loadedPackageConfig.mods[i].dllName == "StaminaSystem.dll") {
            packagePrimaryIndex = i;
        }
        if (loadedPackageConfig.mods[i].dllName == "PPMM.dll") {
            packageDependencyIndex = i;
        }
    }
    require(packagePrimaryIndex < loadedPackageConfig.mods.size(), "package primary index found");
    require(packageDependencyIndex < loadedPackageConfig.mods.size(), "package dependency index found");
    ModMatch packagePrimaryMatch{ModType::Dll, packagePrimaryIndex};
    ModMatch packageDependencyMatch{ModType::DllDependency, packageDependencyIndex};
    require(GetModManifestOwner(loadedPackageConfig, packagePrimaryMatch) == "stamina-system", "package primary manifest owner");
    require(GetModManifestOwner(loadedPackageConfig, packageDependencyMatch) == "stamina-system", "package dependency manifest owner");

    std::vector<ModIniEntry> entries;
    const std::string modIniText = "[General]\r\nDebug=0\r\nName=test\r\n\r\n[Constants]\r\nSpeed=1.5\r\n";
    require(ParseModIniText(modIniText, &entries), "parse mod ini");
    require(entries.size() == 3, "mod ini entry count");
    require(SerializeModIniEntries(entries).find("[Constants]") != std::string::npos, "mod ini preserves sections");

    char packageTempFile[MAX_PATH] = {};
    ::GetTempFileNameA(tempPath, "ump", 0, packageTempFile);
    ::DeleteFileA(packageTempFile);
    const std::string testRoot = packageTempFile;
    const std::string packageRoot = JoinPath(testRoot, "package");
    const std::string gameRoot = JoinPath(testRoot, "game");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(packageRoot, "bin"), "Final"), "mods")), "create package mods dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(packageRoot, "data"), "Scripts")), "create package data dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(gameRoot, "data"), "Scripts")), "create game root");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(packageRoot, "bin"), "Final"), "mods"), "Test.dll"), "dll"), "write package dll");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(packageRoot, "bin"), "Final"), "mods"), "Dependency.dll"), "dll"), "write package dependency dll");
    require(WriteFileText(JoinPath(JoinPath(packageRoot, "data"), "Scripts\\foo.bin"), "modded"), "write package data");
    require(WriteFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\foo.bin"), "vanilla"), "write vanilla data");

    std::vector<PackageFile> packageFiles;
    require(EnumeratePackageFiles(packageRoot, &packageFiles, &error), "enumerate package files");
    const std::vector<std::string> packageDlls = FindPackageDllNames(packageFiles);
    bool foundTestDll = false;
    bool foundDependencyDll = false;
    for (const std::string& dllName : packageDlls) {
        foundTestDll = foundTestDll || dllName == "Test.dll";
        foundDependencyDll = foundDependencyDll || dllName == "Dependency.dll";
    }
    require(packageDlls.size() == 2 && foundTestDll && foundDependencyDll, "detect package dlls");

    const std::string wrappedPackageRoot = JoinPath(testRoot, "wrapped_package");
    const std::string wrappedInnerRoot = JoinPath(wrappedPackageRoot, "Neon Twyre");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(wrappedInnerRoot, "data"), "Scripts")), "create wrapped package dir");
    require(WriteFileText(JoinPath(JoinPath(wrappedInnerRoot, "data"), "Scripts\\wrapped.bin"), "wrapped"), "write wrapped package data");
    require(EnumeratePackageFiles(wrappedPackageRoot, &packageFiles, &error), "enumerate wrapped package");
    require(packageFiles.size() == 1 && packageFiles[0].relativePath == JoinPath(JoinPath("data", "Scripts"), "wrapped.bin"), "wrapped package strips single top folder");

    const std::string wrappedDllPackageRoot = JoinPath(testRoot, "wrapped_dll_package");
    const std::string wrappedDllInnerRoot = JoinPath(wrappedDllPackageRoot, "StaminaSystem");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(wrappedDllInnerRoot, "bin"), "Final"), "mods")), "create wrapped dll package mods dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(wrappedDllInnerRoot, "data"), "Scripts")), "create wrapped dll package data dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(wrappedDllInnerRoot, "bin"), "Final"), "mods"), "StaminaSystem.dll"), "dll"), "write wrapped dll package dll");
    require(WriteFileText(JoinPath(JoinPath(wrappedDllInnerRoot, "data"), "Scripts\\stamina.bin"), "resource"), "write wrapped dll package data");
    require(EnumeratePackageFiles(wrappedDllPackageRoot, &packageFiles, &error), "enumerate wrapped dll package");
    require(FindPackageDllNames(packageFiles).size() == 1 && FindPackageDllNames(packageFiles)[0] == "StaminaSystem.dll", "wrapped dll package detects dll");

    const std::string looseTexturePackageRoot = JoinPath(testRoot, "loose_texture_package");
    require(EnsureDirectoryForTest(looseTexturePackageRoot), "create loose texture package dir");
    require(WriteFileText(JoinPath(looseTexturePackageRoot, "grass.tex"), "texture"), "write loose texture file");
    require(EnumeratePackageFilesForTarget(looseTexturePackageRoot, JoinPath("data", "Textures"), &packageFiles, &error), "enumerate loose package into textures");
    require(packageFiles.size() == 1 && packageFiles[0].relativePath == JoinPath(JoinPath("data", "Textures"), "grass.tex"), "loose package maps into selected texture folder");

    const std::string looseDataPackageRoot = JoinPath(testRoot, "loose_data_package");
    require(EnsureDirectoryForTest(JoinPath(looseDataPackageRoot, "Textures")), "create loose data package dir");
    require(WriteFileText(JoinPath(JoinPath(looseDataPackageRoot, "Textures"), "grass.tex"), "texture"), "write loose data texture file");
    require(EnumeratePackageFilesForTarget(looseDataPackageRoot, "data", &packageFiles, &error), "enumerate loose package into data");
    require(packageFiles.size() == 1 && packageFiles[0].relativePath == JoinPath(JoinPath("data", "Textures"), "grass.tex"), "loose package keeps known data subfolder");

    const std::string releasePackageRoot = JoinPath(testRoot, "release_package");
    std::string restoredText;
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(releasePackageRoot, "bin"), "Final"), "mods")), "create release package mods dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(releasePackageRoot, "data"), "Scripts")), "create release package data dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(releasePackageRoot, "bin"), "Final"), "GameModLauncher.exe"), "launcher"), "write bundled launcher exe");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(releasePackageRoot, "bin"), "Final"), "GameModLauncher.ini"), "launcher config"), "write bundled launcher config");
    require(WriteFileText(JoinPath(releasePackageRoot, "INSTALL.txt"), "docs"), "write release docs");
    require(WriteFileText(JoinPath(releasePackageRoot, "remove-old.ps1"), "tool"), "write release tool");
    require(WriteFileText(JoinPath(releasePackageRoot, "release.zip"), "archive"), "write release archive");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(releasePackageRoot, "bin"), "Final"), "mods"), "StaminaSystem.dll"), "dll"), "write release dll");
    require(WriteFileText(JoinPath(JoinPath(releasePackageRoot, "data"), "Scripts\\stamina.bin"), "resource"), "write release resource");
    require(EnumeratePackageFiles(releasePackageRoot, &packageFiles, &error), "enumerate release package layout");
    require(packageFiles.size() == 2, "release package filters launcher and metadata");
    require(FindPackageDllNames(packageFiles).size() == 1 && FindPackageDllNames(packageFiles)[0] == "StaminaSystem.dll", "release package detects dll");

    const std::string hintPackageRoot = JoinPath(testRoot, "hint_package");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(hintPackageRoot, "bin"), "Final"), "mods")), "create hint package mods dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(hintPackageRoot, "bin"), "Final"), "mods"), "HintA.dll"), "a"), "write hint package a");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(hintPackageRoot, "bin"), "Final"), "mods"), "HintB.dll"), "b"), "write hint package b");
    WriteIniStringToFile("Mods", "LoadOrder", "HintB.dll@engine+500, HintA.dll@suspended, MissingHint.dll@ui", JoinPath(JoinPath(JoinPath(hintPackageRoot, "bin"), "Final"), "GameModLauncher.ini"));
    WriteIniStringToFile("SharedDlls", "Names", "HintA.dll", JoinPath(JoinPath(JoinPath(hintPackageRoot, "bin"), "Final"), "GameModLauncher.ini"));
    WriteIniStringToFile("Mod:HintB.dll", "Name", "Hint B", JoinPath(JoinPath(JoinPath(hintPackageRoot, "bin"), "Final"), "GameModLauncher.ini"));
    require(EnumeratePackageFiles(hintPackageRoot, &packageFiles, &error), "enumerate hint package");
    std::vector<std::string> hintWarnings;
    const std::vector<PackageDllInstallHint> hintEntries = GetPackageDllInstallHintsForGameRoot(LauncherConfig(), hintPackageRoot, packageFiles, gameRoot, &hintWarnings);
    require(hintEntries.size() == 2, "package loadorder skips missing dependency");
    require(!hintWarnings.empty(), "package loadorder reports missing dependency");
    require(hintEntries[0].dllName == "HintB.dll" && hintEntries[0].stage == InjectionStage::Engine && hintEntries[0].delayMs == 500, "package loadorder hint b");
    require(hintEntries[1].dllName == "HintA.dll" && hintEntries[1].stage == InjectionStage::Suspended, "package loadorder hint a");
    require(hintEntries[1].sharedDependency, "package marks shared dependency");
    require(hintEntries[0].displayName == "Hint B", "package mod display name hint");

    const std::string builtInSharedPackageRoot = JoinPath(testRoot, "builtin_shared_package");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(builtInSharedPackageRoot, "bin"), "Final"), "mods")), "create builtin shared package mods dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(builtInSharedPackageRoot, "bin"), "Final"), "mods"), "OynonTools.dll"), "shared"), "write builtin shared package oynon");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(builtInSharedPackageRoot, "bin"), "Final"), "mods"), "StaminaSystem.dll"), "dll"), "write builtin shared package stamina");
    require(EnumeratePackageFiles(builtInSharedPackageRoot, &packageFiles, &error), "enumerate builtin shared package");
    const std::vector<PackageDllInstallHint> builtInSharedHints = GetPackageDllInstallHintsForGameRoot(LauncherConfig(), builtInSharedPackageRoot, packageFiles, gameRoot, nullptr);
    bool foundBuiltInOynonShared = false;
    for (const PackageDllInstallHint& hint : builtInSharedHints) {
        if (hint.dllName == "OynonTools.dll") {
            foundBuiltInOynonShared = hint.sharedDependency;
        }
    }
    require(foundBuiltInOynonShared, "OynonTools package dll is always shared dependency");

    const std::string skipGameRoot = JoinPath(testRoot, "skip_game");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(skipGameRoot, "bin"), "Final"), "mods")), "create skip game mods dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(skipGameRoot, "bin"), "Final"), "mods"), "Existing.dll"), "old dll"), "write old dll");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(skipGameRoot, "bin"), "Final"), "mods"), "Existing.ini"), "old ini"), "write old ini");
    const std::string skipPackageRoot = JoinPath(testRoot, "skip_package");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(skipPackageRoot, "bin"), "Final"), "mods")), "create skip package mods dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(skipPackageRoot, "bin"), "Final"), "mods"), "Existing.dll"), "new dll"), "write new dll");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(skipPackageRoot, "bin"), "Final"), "mods"), "Existing.ini"), "new ini"), "write new ini");
    require(EnumeratePackageFiles(skipPackageRoot, &packageFiles, &error), "enumerate skip package");
    PackageInstallResult skipInstallResult;
    const std::vector<std::string> skipPaths = {
        JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), "Existing.dll"),
        JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), "Existing.ini")
    };
    require(InstallModPackageFiles(packageFiles, skipGameRoot, "Existing.dll", &skipInstallResult, &error, skipPaths), "install package with skipped dll");
    require(skipInstallResult.skippedRelativePaths.size() == 2, "reported skipped dll files");
    require(ReadFileText(JoinPath(JoinPath(JoinPath(JoinPath(skipGameRoot, "bin"), "Final"), "mods"), "Existing.dll"), &restoredText) && restoredText == "old dll", "skipped dll preserved");
    require(ReadFileText(JoinPath(JoinPath(JoinPath(JoinPath(skipGameRoot, "bin"), "Final"), "mods"), "Existing.ini"), &restoredText) && restoredText == "old ini", "skipped ini preserved");

    PackageInstallResult installResult;
    const std::string dependencyDeleteGameRoot = JoinPath(testRoot, "dependency_delete_game");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(dependencyDeleteGameRoot, "bin"), "Final"), "mods")), "create dependency delete game mods dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(dependencyDeleteGameRoot, "data"), "Scripts")), "create dependency delete game data dir");
    require(InstallModPackageFromDirectory(packageRoot, dependencyDeleteGameRoot, "Test.dll", &installResult, &error), "install package for protected dependency delete");
    const std::vector<std::string> protectedDependencyPaths = {
        JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), "Dependency.dll"),
        JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), "Dependency.ini")
    };
    ModDeleteResult protectedDeleteResult;
    require(DeleteInstalledModFiles(dependencyDeleteGameRoot, "Test.dll", true, &protectedDeleteResult, &error, protectedDependencyPaths), "delete package with protected dependency");
    require(!FileExists(JoinPath(JoinPath(JoinPath(JoinPath(dependencyDeleteGameRoot, "bin"), "Final"), "mods"), "Test.dll").c_str()), "primary dll deleted with protected dependency");
    require(FileExists(JoinPath(JoinPath(JoinPath(JoinPath(dependencyDeleteGameRoot, "bin"), "Final"), "mods"), "Dependency.dll").c_str()), "protected dependency dll remains");

    require(InstallModPackageFromDirectory(releasePackageRoot, gameRoot, "StaminaSystem.dll", &installResult, &error), "install release package layout");
    require(FileExists(JoinPath(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods"), "StaminaSystem.dll").c_str()), "installed release dll");
    require(FileExists(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\stamina.bin").c_str()), "installed release data");
    ModDeleteResult releaseDeleteResult;
    require(DeleteInstalledModFiles(gameRoot, "StaminaSystem.dll", true, &releaseDeleteResult, &error), "delete release package layout");

    require(InstallModPackageFromDirectory(packageRoot, gameRoot, "Test.dll", &installResult, &error), "install package");
    require(FileExists(JoinPath(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods"), "Test.dll").c_str()), "installed dll exists");
    require(FileExists(JoinPath(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods"), "Dependency.dll").c_str()), "installed dependency dll exists");
    require(FileExists(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\foo.bin").c_str()), "installed data exists");

    std::vector<std::string> manifestPaths;
    require(ReadInstallManifest(gameRoot, "Test.dll", &manifestPaths), "read install manifest");
    require(manifestPaths.size() >= 2, "manifest path count");
    ModDeleteResult deleteResult;
    require(DeleteInstalledModFiles(gameRoot, "Test.dll", true, &deleteResult, &error), "delete installed files");
    require(!FileExists(JoinPath(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods"), "Test.dll").c_str()), "deleted dll");
    require(ReadFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\foo.bin"), &restoredText) && restoredText == "vanilla", "restored vanilla data");
    require(deleteResult.deletedRelativePaths.size() == 2, "deleted created file count");
    require(deleteResult.restoredRelativePaths.size() == 1, "restored overwritten file count");

    require(InstallModPackageFromDirectory(packageRoot, gameRoot, "Test.dll", &installResult, &error), "reinstall package");
    require(WriteFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\foo.bin"), "changed later"), "change installed data");
    require(DeleteInstalledModFiles(gameRoot, "Test.dll", true, &deleteResult, &error), "delete with conflict");
    std::string skippedText;
    require(ReadFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\foo.bin"), &skippedText) && skippedText == "changed later", "skipped changed data");
    require(!deleteResult.skippedRelativePaths.empty(), "reported skipped changed file");

    const std::string weatherGameRoot = JoinPath(testRoot, "weather_stack_game");
    const std::string winterPackageRoot = JoinPath(testRoot, "winter_package");
    const std::string defogPackageRoot = JoinPath(testRoot, "defog_package");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(weatherGameRoot, "data"), "Scripts")), "create weather game scripts dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(winterPackageRoot, "data"), "Scripts")), "create winter scripts dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(defogPackageRoot, "data"), "Scripts")), "create defog scripts dir");
    require(WriteFileText(JoinPath(JoinPath(weatherGameRoot, "data"), "Scripts\\weather.bin"), "vanilla weather"), "write vanilla weather");
    require(WriteFileText(JoinPath(JoinPath(winterPackageRoot, "data"), "Scripts\\weather.bin"), "winter weather"), "write winter weather");
    require(WriteFileText(JoinPath(JoinPath(defogPackageRoot, "data"), "Scripts\\weather.bin"), "defog weather"), "write defog weather");
    require(EnumeratePackageFiles(winterPackageRoot, &packageFiles, &error), "enumerate winter package");
    require(InstallModPackageFiles(packageFiles, weatherGameRoot, "winter_in_gorkhonsk", &installResult, &error), "install winter package");
    require(EnumeratePackageFiles(defogPackageRoot, &packageFiles, &error), "enumerate defog package");
    require(InstallModPackageFiles(packageFiles, weatherGameRoot, "defog", &installResult, &error), "install defog package");
    require(ReadFileText(JoinPath(JoinPath(weatherGameRoot, "data"), "Scripts\\weather.bin"), &restoredText) && restoredText == "defog weather", "defog weather installed");
    const std::vector<std::string> protectedWeatherPaths = {JoinPath(JoinPath("data", "Scripts"), "weather.bin")};
    require(DeleteInstalledModFiles(weatherGameRoot, "defog", false, &deleteResult, &error, protectedWeatherPaths), "delete defog package with winter protected path");
    require(ReadFileText(JoinPath(JoinPath(weatherGameRoot, "data"), "Scripts\\weather.bin"), &restoredText) && restoredText == "winter weather", "defog delete restores winter weather");
    require(deleteResult.restoredRelativePaths.size() == 1, "defog delete reports restored weather");

    const std::string cleanupPackageRoot = JoinPath(testRoot, "cleanup_package");
    const std::string cleanupGameRoot = JoinPath(testRoot, "cleanup_game");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(cleanupPackageRoot, "bin"), "Final"), "mods")), "create cleanup package mods dir");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(cleanupGameRoot, "data"), "UI")), "create cleanup game ui dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(cleanupPackageRoot, "bin"), "Final"), "mods"), "Cleanup.dll"), "dll"), "write cleanup dll");
    require(WriteFileText(JoinPath(JoinPath(cleanupGameRoot, "data"), "UI\\playerstat_base.xml"), "vanilla ui"), "write cleanup vanilla target");
    require(EnumeratePackageFiles(cleanupPackageRoot, &packageFiles, &error), "enumerate cleanup package");
    const std::vector<std::string> cleanupPaths = {JoinPath(JoinPath("data", "UI"), "playerstat_base.xml")};
    PackageInstallResult cleanupInstallResult;
    require(InstallModPackageFiles(packageFiles, cleanupGameRoot, "Cleanup", &cleanupInstallResult, &error, {}, cleanupPaths), "install package with cleanup restore");
    std::vector<InstallManifestEntryInfo> cleanupEntries;
    require(ReadInstallManifestEntriesInfo(cleanupGameRoot, "Cleanup", &cleanupEntries), "read cleanup manifest");
    bool foundCleanupRestore = false;
    for (const InstallManifestEntryInfo& cleanupEntry : cleanupEntries) {
        foundCleanupRestore = foundCleanupRestore || cleanupEntry.action == ManifestInstallAction::CleanupRestore;
    }
    require(foundCleanupRestore, "cleanup restore entry recorded");
    require(WriteFileText(JoinPath(JoinPath(cleanupGameRoot, "data"), "UI\\playerstat_base.xml"), "dynamic changed ui"), "change cleanup target");
    require(DeleteInstalledModFiles(cleanupGameRoot, "Cleanup", true, &deleteResult, &error), "delete cleanup restore package");
    require(ReadFileText(JoinPath(JoinPath(cleanupGameRoot, "data"), "UI\\playerstat_base.xml"), &restoredText) && restoredText == "vanilla ui", "cleanup restore restores original");

    const std::string generatedCleanupRoot = JoinPath(testRoot, "generated_cleanup_game");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(generatedCleanupRoot, "data"), "UI")), "create generated cleanup game ui dir");
    require(InstallModPackageFiles(packageFiles, generatedCleanupRoot, "GeneratedCleanup", &cleanupInstallResult, &error, {}, cleanupPaths), "install package with cleanup delete");
    require(ReadInstallManifestEntriesInfo(generatedCleanupRoot, "GeneratedCleanup", &cleanupEntries), "read generated cleanup manifest");
    bool foundCleanupDelete = false;
    for (const InstallManifestEntryInfo& cleanupEntry : cleanupEntries) {
        foundCleanupDelete = foundCleanupDelete || cleanupEntry.action == ManifestInstallAction::CleanupDelete;
    }
    require(foundCleanupDelete, "cleanup delete entry recorded");
    require(WriteFileText(JoinPath(JoinPath(generatedCleanupRoot, "data"), "UI\\playerstat_base.xml"), "generated ui"), "write generated cleanup target");
    require(DeleteInstalledModFiles(generatedCleanupRoot, "GeneratedCleanup", true, &deleteResult, &error), "delete cleanup generated package");
    require(!FileExists(JoinPath(JoinPath(generatedCleanupRoot, "data"), "UI\\playerstat_base.xml").c_str()), "cleanup delete removes generated target");

    const std::vector<std::string> unsafeCleanupPaths = {"..\\bad.xml"};
    require(!InstallModPackageFiles(packageFiles, generatedCleanupRoot, "BadCleanup", &cleanupInstallResult, &error, {}, unsafeCleanupPaths), "reject unsafe cleanup path");

    require(WriteFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\vanilla.bin"), "do not delete"), "write legacy data");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods")), "create legacy mods dir");
    require(WriteFileText(JoinPath(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods"), "Legacy.dll"), "legacy"), "write legacy dll");
    require(DeleteInstalledModFiles(gameRoot, "Legacy.dll", true, &deleteResult, &error), "delete without manifest safely");
    require(FileExists(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\vanilla.bin").c_str()), "unmanifested data preserved");
    require(!FileExists(JoinPath(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods"), "Legacy.dll").c_str()), "unmanifested dll deleted");
    require(GetLegacyDeleteRelativePaths("Legacy.dll").size() == 2, "unmanifested delete plan");

    require(EnsureDirectoryForTest(JoinPath(JoinPath(gameRoot, "data"), "Conflicts")), "create conflict game dir");
    require(WriteFileText(JoinPath(JoinPath(gameRoot, "data"), "Conflicts\\shared.bin"), "vanilla shared"), "write conflict vanilla");
    const std::vector<std::string> conflictPaths = {JoinPath(JoinPath("data", "Conflicts"), "shared.bin")};
    require(WriteInstallManifest(gameRoot, "ConflictA", conflictPaths, &error), "write conflict manifest a");
    require(WriteInstallManifest(gameRoot, "ConflictB", conflictPaths, &error), "write conflict manifest b");
    LauncherConfig conflictConfig;
    ResourceModEntry conflictA;
    conflictA.id = "ConflictA";
    conflictA.name = "Conflict A";
    conflictA.manifestOwner = "ConflictA";
    ResourceModEntry conflictB;
    conflictB.id = "ConflictB";
    conflictB.name = "Conflict B";
    conflictB.manifestOwner = "ConflictB";
    conflictConfig.resourceMods.push_back(conflictA);
    conflictConfig.resourceMods.push_back(conflictB);
    require(!GetInstalledModConflictsForGameRoot(conflictConfig, gameRoot).empty(), "installed manifest conflict detected");

    const std::string conflictPackageRoot = JoinPath(testRoot, "conflict_package");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(conflictPackageRoot, "data"), "Conflicts")), "create conflict package dir");
    require(WriteFileText(JoinPath(JoinPath(conflictPackageRoot, "data"), "Conflicts\\shared.bin"), "package shared"), "write conflict package data");
    require(EnumeratePackageFiles(conflictPackageRoot, &packageFiles, &error), "enumerate conflict package");
    require(!GetPackageConflictsForGameRoot(conflictConfig, packageFiles, gameRoot).empty(), "package conflict detected");

    LauncherConfig vanillaOnlyConfig;
    ResourceModEntry conflictOnlyA = conflictA;
    vanillaOnlyConfig.resourceMods.push_back(conflictOnlyA);
    require(GetInstalledModConflictsForGameRoot(vanillaOnlyConfig, gameRoot).empty(), "single vanilla overwrite is not conflict");

    const std::string resourcePackageRoot = JoinPath(testRoot, "resource_package");
    require(EnsureDirectoryForTest(JoinPath(JoinPath(resourcePackageRoot, "data"), "Scripts")), "create resource package dir");
    require(WriteFileText(JoinPath(JoinPath(resourcePackageRoot, "data"), "Scripts\\resource.bin"), "resource mod"), "write resource package data");
    require(WriteFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\resource.bin"), "vanilla resource"), "write resource vanilla data");
    require(EnumeratePackageFiles(resourcePackageRoot, &packageFiles, &error), "enumerate resource package");
    require(FindPackageDllNames(packageFiles).empty(), "resource package has no dlls");
    require(PackageHasResourceFiles(packageFiles), "resource package has data files");
    require(InstallModPackageFromDirectory(resourcePackageRoot, gameRoot, "resource_mod", &installResult, &error), "install resource package");
    require(ReadFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\resource.bin"), &restoredText) && restoredText == "resource mod", "installed resource data");
    require(DeleteInstalledModFiles(gameRoot, "resource_mod", false, &deleteResult, &error), "delete resource package");
    require(ReadFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\resource.bin"), &restoredText) && restoredText == "vanilla resource", "restored resource vanilla data");

    require(InstallModPackageFromDirectory(resourcePackageRoot, gameRoot, "resource_mod", &installResult, &error), "reinstall resource package");
    require(WriteFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\resource.bin"), "changed resource"), "change resource data");
    std::vector<InstallManifestEntryInfo> changedAudit;
    require(ReadInstallManifestEntriesInfo(gameRoot, "resource_mod", &changedAudit), "read resource manifest audit");
    bool foundChanged = false;
    for (const InstallManifestEntryInfo& auditEntry : changedAudit) {
        foundChanged = foundChanged || auditEntry.currentState == ManifestCurrentState::Changed;
    }
    require(foundChanged, "vanilla audit detects changed overwritten file");
    require(DeleteInstalledModFiles(gameRoot, "resource_mod", false, &deleteResult, &error), "delete changed resource package");
    require(ReadFileText(JoinPath(JoinPath(gameRoot, "data"), "Scripts\\resource.bin"), &restoredText) && restoredText == "changed resource", "changed resource data preserved");
    require(!deleteResult.skippedRelativePaths.empty(), "changed resource reported skipped");

    const std::string invalidPackageRoot = JoinPath(testRoot, "invalid_package");
    require(EnsureDirectoryForTest(JoinPath(invalidPackageRoot, "other")), "create invalid package dir");
    require(WriteFileText(JoinPath(invalidPackageRoot, "other\\file.bin"), "bad"), "write invalid package file");
    require(!EnumeratePackageFiles(invalidPackageRoot, &packageFiles, &error), "reject unsupported package path");

    ::DeleteFileA(iniPath.c_str());
    if (ok) {
        std::printf("SELFTEST OK\n");
    }
    return ok;
}
}
