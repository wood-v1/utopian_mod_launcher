#include "launcher_cli.h"

#include "launcher_config.h"
#include "launcher_runtime.h"
#include "launcher_services.h"
#include "launcher_version.h"
#include "load_order.h"
#include "path_utils.h"
#include "string_utils.h"
#include "windows_ui/file_dialogs.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace uml
{
namespace
{
bool EqualsNoCase(const char* left, const char* right)
{
    return ::_stricmp(left, right) == 0;
}

bool EqualsNoCase(const std::string& left, const char* right)
{
    return ::_stricmp(left.c_str(), right) == 0;
}

bool GetOption(int argc, char** argv, const char* name, std::string* value)
{
    for (int i = 2; i + 1 < argc; ++i) {
        if (EqualsNoCase(argv[i], name)) {
            if (value) {
                *value = argv[i + 1];
            }
            return true;
        }
    }
    return false;
}

std::vector<std::string> GetOptions(int argc, char** argv, const char* name)
{
    std::vector<std::string> values;
    for (int i = 2; i + 1 < argc; ++i) {
        if (EqualsNoCase(argv[i], name)) {
            values.push_back(argv[i + 1]);
        }
    }
    return values;
}

bool HasOption(int argc, char** argv, const char* name)
{
    for (int i = 2; i < argc; ++i) {
        if (EqualsNoCase(argv[i], name)) {
            return true;
        }
    }
    return false;
}

std::string RemoveExtension(const std::string& path)
{
    const std::string fileName = FileNamePart(path);
    const std::size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos) {
        return fileName;
    }
    return fileName.substr(0, dot);
}

bool LoadConfig(LauncherConfig* config)
{
    std::string error;
    if (!LoadLauncherConfig(GetLauncherIniPath(), config, &error)) {
        std::printf("%s\n", error.c_str());
        return false;
    }
    return true;
}

bool SaveConfig(const LauncherConfig& config)
{
    std::string error;
    if (!SaveLauncherConfig(GetLauncherIniPath(), config, &error)) {
        std::printf("%s\n", error.c_str());
        return false;
    }
    return true;
}

int RunHeadlessLaunch()
{
    LauncherConfig config;
    std::string error;
    if (!LoadLauncherConfig(GetLauncherIniPath(), &config, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    if (!LaunchGame(config, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    return 0;
}

const char* TypeName(ModType type)
{
    if (type == ModType::SharedDll) {
        return "DLL Mod Shared Dependency";
    }
    if (type == ModType::DllDependency) {
        return "DLL Mod Dependency";
    }
    return type == ModType::Dll ? "DLL Mod" : "Resource Mod";
}

const char* CurrentStateName(ManifestCurrentState state)
{
    switch (state) {
    case ManifestCurrentState::Missing:
        return "missing";
    case ManifestCurrentState::Unchanged:
        return "unchanged";
    case ManifestCurrentState::Changed:
        return "changed";
    default:
        return "unknown";
    }
}

int PrintVersion()
{
    std::printf("UtopianModLauncher %s\n", kLauncherVersion);
    return 0;
}

int PrintUsage()
{
    std::printf(
        "UtopianModLauncher %s\n"
        "\n"
        "Usage:\n"
        "  GameModLauncher.exe --ui\n"
        "  GameModLauncher.exe --noui\n"
        "  GameModLauncher.exe help\n"
        "  GameModLauncher.exe --version\n"
        "  GameModLauncher.exe list\n"
        "  GameModLauncher.exe install --zip <path> [--name <name>] [--dll <dllName>] [--overwrite-dll <dll>|--skip-dll <dll>]\n"
        "  GameModLauncher.exe install --rar <path> [--name <name>] [--dll <dllName>] [--overwrite-dll <dll>|--skip-dll <dll>]\n"
        "  GameModLauncher.exe install --7z <path> [--name <name>] [--dll <dllName>] [--overwrite-dll <dll>|--skip-dll <dll>]\n"
        "  GameModLauncher.exe install --folder <path> [--name <name>] [--dll <dllName>] [--overwrite-dll <dll>|--skip-dll <dll>]\n"
        "  GameModLauncher.exe delete --mod <name|id|dll>\n"
        "  GameModLauncher.exe rename --mod <name|id|dll> --name <newName>\n"
        "  GameModLauncher.exe set-logging --enabled 0|1\n"
        "  GameModLauncher.exe set-stage --mod <name|id|dll> --stage suspended|resume|engine|ui [--delay-ms N]\n"
        "  GameModLauncher.exe move --mod <name|id|dll> --up|--down\n"
        "  GameModLauncher.exe vanilla-files [--all|--changed|--backed-up]\n"
        "  GameModLauncher.exe conflicts [--mod <name|id|dll>|--package-folder <path>]\n",
        kLauncherVersion);
    std::printf(
        "\n"
        "Install notes:\n"
        "  DLL packages may include bin\\Final\\GameModLauncher.ini.\n"
        "  Only [Mods] LoadOrder and [Mod:<dll>] Name are used as install hints.\n"
        "  [SharedDlls] Names marks DLLs such as hook libraries as shared dependencies.\n"
        "  Non-shared DLL dependencies are grouped into an installed package and deleted together.\n"
        "  If a selected DLL already exists, choose --overwrite-dll <dll> or --skip-dll <dll>.\n");
    return 0;
}

int CommandList()
{
    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    std::printf("Order  Type          Stage      Delay  Name                          Identity        Manifest\n");
    for (std::size_t i = 0; i < config.mods.size(); ++i) {
        const ModEntry& mod = config.mods[i];
        const ModType type = GetDllModType(config, mod.dllName);
        std::printf(
            "%-5lu  %-12s  %-9s  %-5lu  %-28s  %-14s  %s\n",
            static_cast<unsigned long>(i + 1),
            type == ModType::SharedDll ? "DLL Shared" : (type == ModType::DllDependency ? "DLL Dep" : "DLL Mod"),
            GetStageName(mod.stage),
            static_cast<unsigned long>(mod.delayMs),
            (type == ModType::SharedDll ? GetSharedDllDisplayName(config, mod.dllName) : GetDllModDisplayName(mod)).c_str(),
            mod.dllName.c_str(),
            GetModManifestOwner(config, ModMatch{type, i}).c_str());
    }
    for (const ResourceModEntry& mod : config.resourceMods) {
        const std::string manifestOwner = mod.manifestOwner.empty() ? mod.id : mod.manifestOwner;
        std::printf(
            "%-5s  %-12s  %-9s  %-5lu  %-28s  %-14s  %s\n",
            "#",
            "Resource Mod",
            GetStageName(mod.stage),
            static_cast<unsigned long>(mod.delayMs),
            GetResourceModDisplayName(mod).c_str(),
            mod.id.c_str(),
            manifestOwner.c_str());
    }
    return 0;
}

int CommandInstall(int argc, char** argv)
{
    std::string packageRoot;
    std::string archivePath;
    std::string tempDirectory;
    std::string error;
    if (GetOption(argc, argv, "--zip", &archivePath) || GetOption(argc, argv, "--rar", &archivePath) || GetOption(argc, argv, "--7z", &archivePath)) {
        if (!windows_ui::CreateTempDirectory(&tempDirectory, &error) || !windows_ui::ExtractArchiveToDirectory(archivePath, tempDirectory, &error)) {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        packageRoot = tempDirectory;
    }
    else if (!GetOption(argc, argv, "--folder", &packageRoot)) {
        std::printf("install requires --zip, --rar, --7z or --folder.\n");
        return 1;
    }

    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    InstallModOptions options;
    options.packageRoot = packageRoot;
    if (!GetOption(argc, argv, "--name", &options.name)) {
        options.name = RemoveExtension(archivePath.empty() ? packageRoot : archivePath);
    }
    GetOption(argc, argv, "--dll", &options.dllName);
    options.overwriteDllNames = GetOptions(argc, argv, "--overwrite-dll");
    options.skipDllNames = GetOptions(argc, argv, "--skip-dll");

    InstallModResult result;
    if (!InstallModFromPackage(&config, options, &result, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    if (!SaveConfig(config)) {
        return 1;
    }

    std::printf("Installed %s (%s).\n", result.displayName.c_str(), TypeName(result.type));
    return 0;
}

int CommandDelete(int argc, char** argv)
{
    std::string query;
    if (!GetOption(argc, argv, "--mod", &query)) {
        std::printf("delete requires --mod.\n");
        return 1;
    }

    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    ModMatch match;
    std::string error;
    if (!FindSingleInstalledMod(config, query, &match, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }

    const std::string displayName = GetModDisplayName(config, match);
    ModDeleteResult result;
    if (!DeleteInstalledMod(&config, match, &result, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    if (!SaveConfig(config)) {
        return 1;
    }

    std::printf(
        "Deleted %s: %lu removed, %lu restored, %lu skipped.\n",
        displayName.c_str(),
        static_cast<unsigned long>(result.deletedRelativePaths.size()),
        static_cast<unsigned long>(result.restoredRelativePaths.size()),
        static_cast<unsigned long>(result.skippedRelativePaths.size()));
    return result.skippedRelativePaths.empty() ? 0 : 2;
}

int CommandRename(int argc, char** argv)
{
    std::string query;
    std::string name;
    if (!GetOption(argc, argv, "--mod", &query) || !GetOption(argc, argv, "--name", &name)) {
        std::printf("rename requires --mod and --name.\n");
        return 1;
    }

    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    ModMatch match;
    std::string error;
    if (!FindSingleInstalledMod(config, query, &match, &error)
        || !RenameInstalledMod(&config, match, name, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    return SaveConfig(config) ? 0 : 1;
}

int CommandSetLogging(int argc, char** argv)
{
    std::string enabled;
    if (!GetOption(argc, argv, "--enabled", &enabled) || (enabled != "0" && enabled != "1")) {
        std::printf("set-logging requires --enabled 0|1.\n");
        return 1;
    }

    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }
    config.loggingEnabled = enabled == "1";
    return SaveConfig(config) ? 0 : 1;
}

int CommandSetStage(int argc, char** argv)
{
    std::string query;
    std::string stageText;
    std::string delayText;
    if (!GetOption(argc, argv, "--mod", &query) || !GetOption(argc, argv, "--stage", &stageText)) {
        std::printf("set-stage requires --mod and --stage.\n");
        return 1;
    }

    InjectionStage stage = InjectionStage::Resume;
    uint32_t ignoredDelay = 0;
    if (!ParseInjectionStage(stageText, &stage, &ignoredDelay)) {
        std::printf("Invalid stage: %s\n", stageText.c_str());
        return 1;
    }

    uint32_t delayMs = 0;
    if (GetOption(argc, argv, "--delay-ms", &delayText) && !ParseUint32(delayText, &delayMs)) {
        std::printf("Invalid --delay-ms value.\n");
        return 1;
    }

    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    ModMatch match;
    std::string error;
    if (!FindSingleInstalledMod(config, query, &match, &error)
        || !SetInstalledModStage(&config, match, stage, delayMs, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    return SaveConfig(config) ? 0 : 1;
}

int CommandMove(int argc, char** argv)
{
    std::string query;
    if (!GetOption(argc, argv, "--mod", &query)) {
        std::printf("move requires --mod.\n");
        return 1;
    }

    const int direction = HasOption(argc, argv, "--up") ? -1 : (HasOption(argc, argv, "--down") ? 1 : 0);
    if (direction == 0) {
        std::printf("move requires --up or --down.\n");
        return 1;
    }

    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    ModMatch match;
    std::string error;
    if (!FindSingleInstalledMod(config, query, &match, &error)
        || !MoveInstalledMod(&config, match, direction, &error)) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    return SaveConfig(config) ? 0 : 1;
}

int CommandVanillaFiles(int argc, char** argv)
{
    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    const bool changedOnly = HasOption(argc, argv, "--changed");
    const bool backedUpOnly = HasOption(argc, argv, "--backed-up");
    const std::vector<ManifestAuditEntry> entries = GetVanillaFileAudit(config, changedOnly, backedUpOnly);
    std::printf("State      Mod                           Owner                         Path\n");
    for (const ManifestAuditEntry& audit : entries) {
        std::printf(
            "%-10s %-29s %-29s %s\n",
            CurrentStateName(audit.entry.currentState),
            audit.modName.c_str(),
            audit.owner.c_str(),
            audit.entry.relativePath.c_str());
    }
    return 0;
}

int CommandConflicts(int argc, char** argv)
{
    LauncherConfig config;
    if (!LoadConfig(&config)) {
        return 1;
    }

    std::string packageFolder;
    std::vector<ModConflictEntry> conflicts;
    if (GetOption(argc, argv, "--package-folder", &packageFolder)) {
        std::vector<PackageFile> files;
        std::string error;
        if (!EnumeratePackageFiles(packageFolder, &files, &error)) {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        conflicts = GetPackageConflicts(config, files);
    }
    else {
        conflicts = GetInstalledModConflicts(config);
    }

    std::string modQuery;
    if (GetOption(argc, argv, "--mod", &modQuery)) {
        ModMatch match;
        std::string error;
        if (!FindSingleInstalledMod(config, modQuery, &match, &error)) {
            std::printf("%s\n", error.c_str());
            return 1;
        }

        const std::string owner = GetModManifestOwner(config, match);
        std::vector<ModConflictEntry> filtered;
        for (const ModConflictEntry& conflict : conflicts) {
            if (::_stricmp(conflict.owner.c_str(), owner.c_str()) == 0
                || ::_stricmp(conflict.otherOwner.c_str(), owner.c_str()) == 0) {
                filtered.push_back(conflict);
            }
        }
        conflicts = filtered;
    }

    std::printf("Path                          Mod                           Conflicts with\n");
    for (const ModConflictEntry& conflict : conflicts) {
        std::printf(
            "%-29s %-29s %s\n",
            conflict.relativePath.c_str(),
            conflict.modName.c_str(),
            conflict.otherModName.c_str());
    }
    return conflicts.empty() ? 0 : 2;
}
}

bool IsCliCommand(int argc, char** argv)
{
    if (argc <= 1) {
        return false;
    }

    const char* command = argv[1];
    return EqualsNoCase(command, "--noui")
        || EqualsNoCase(command, "--noresourcemods")
        || EqualsNoCase(command, "--launch")
        || EqualsNoCase(command, "list")
        || EqualsNoCase(command, "install")
        || EqualsNoCase(command, "delete")
        || EqualsNoCase(command, "rename")
        || EqualsNoCase(command, "set-logging")
        || EqualsNoCase(command, "set-stage")
        || EqualsNoCase(command, "move")
        || EqualsNoCase(command, "vanilla-files")
        || EqualsNoCase(command, "conflicts")
        || EqualsNoCase(command, "help")
        || EqualsNoCase(command, "--help")
        || EqualsNoCase(command, "--version")
        || EqualsNoCase(command, "/?");
}

int RunLauncherCli(int argc, char** argv)
{
    if (argc <= 1) {
        return PrintUsage();
    }

    const char* command = argv[1];
    if (EqualsNoCase(command, "help") || EqualsNoCase(command, "--help") || EqualsNoCase(command, "/?")) {
        return PrintUsage();
    }
    if (EqualsNoCase(command, "--version")) {
        return PrintVersion();
    }
    if (EqualsNoCase(command, "--noui") || EqualsNoCase(command, "--noresourcemods") || EqualsNoCase(command, "--launch")) {
        return RunHeadlessLaunch();
    }
    if (EqualsNoCase(command, "list")) {
        return CommandList();
    }
    if (EqualsNoCase(command, "install")) {
        return CommandInstall(argc, argv);
    }
    if (EqualsNoCase(command, "delete")) {
        return CommandDelete(argc, argv);
    }
    if (EqualsNoCase(command, "rename")) {
        return CommandRename(argc, argv);
    }
    if (EqualsNoCase(command, "set-logging")) {
        return CommandSetLogging(argc, argv);
    }
    if (EqualsNoCase(command, "set-stage")) {
        return CommandSetStage(argc, argv);
    }
    if (EqualsNoCase(command, "move")) {
        return CommandMove(argc, argv);
    }
    if (EqualsNoCase(command, "vanilla-files")) {
        return CommandVanillaFiles(argc, argv);
    }
    if (EqualsNoCase(command, "conflicts")) {
        return CommandConflicts(argc, argv);
    }
    return PrintUsage();
}
}
