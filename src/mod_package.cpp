#include "mod_package.h"

#include "path_utils.h"
#include "string_utils.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>

namespace uml
{
namespace
{
enum class InstallAction
{
    Created,
    Overwritten
};

struct InstallManifestEntry
{
    std::string relativePath;
    InstallAction action = InstallAction::Created;
    std::string beforeSha256;
    std::string installedSha256;
    std::string backupRelativePath;
};

bool StartsWithNoCase(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size()
        && _strnicmp(value.c_str(), prefix.c_str(), prefix.size()) == 0;
}

bool HasExtensionNoCase(const std::string& value, const char* extension)
{
    const std::size_t extensionLength = std::strlen(extension);
    return value.size() >= extensionLength
        && _stricmp(value.c_str() + value.size() - extensionLength, extension) == 0;
}

bool CreateDirectoryRecursive(const std::string& directory)
{
    if (directory.empty() || DirectoryExists(directory.c_str())) {
        return true;
    }

    const std::string parent = DirectoryName(directory);
    if (parent != directory && !DirectoryExists(parent.c_str()) && !CreateDirectoryRecursive(parent)) {
        return false;
    }

    return ::CreateDirectoryA(directory.c_str(), nullptr) != FALSE || ::GetLastError() == ERROR_ALREADY_EXISTS;
}

bool DeleteFileIfExists(const std::string& path, std::string* error)
{
    if (!FileExists(path.c_str())) {
        return true;
    }

    if (::DeleteFileA(path.c_str()) == FALSE) {
        if (error) {
            *error = "Failed to delete " + path;
        }
        return false;
    }

    return true;
}

bool MoveFileIfExists(const std::string& source, const std::string& target, std::string* error)
{
    if (!FileExists(source.c_str())) {
        return true;
    }

    if (!CreateDirectoryRecursive(DirectoryName(target))) {
        if (error) {
            *error = "Failed to create directory for " + target;
        }
        return false;
    }

    if (::MoveFileExA(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        if (error) {
            *error = "Failed to move " + source + " to " + target;
        }
        return false;
    }

    return true;
}

std::string BytesToHex(const unsigned char* bytes, std::size_t size)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        stream << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return stream.str();
}

bool ComputeFileSha256(const std::string& path, std::string* hash, std::string* error)
{
    if (!hash) {
        return false;
    }

    HANDLE file = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = "Failed to open file for hashing: " + path;
        }
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    DWORD objectLength = 0;
    DWORD dataLength = 0;
    DWORD hashLength = 0;
    std::vector<unsigned char> hashObject;
    std::vector<unsigned char> hashBytes;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        if (error) {
            *error = "Failed to open SHA256 provider.";
        }
        goto cleanup;
    }

    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &dataLength, 0) < 0
        || BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &dataLength, 0) < 0) {
        if (error) {
            *error = "Failed to query SHA256 provider.";
        }
        goto cleanup;
    }

    hashObject.resize(objectLength);
    hashBytes.resize(hashLength);
    if (BCryptCreateHash(algorithm, &hashHandle, hashObject.data(), objectLength, nullptr, 0, 0) < 0) {
        if (error) {
            *error = "Failed to create SHA256 hash.";
        }
        goto cleanup;
    }

    for (;;) {
        unsigned char buffer[65536] = {};
        DWORD bytesRead = 0;
        if (::ReadFile(file, buffer, sizeof(buffer), &bytesRead, nullptr) == FALSE) {
            if (error) {
                *error = "Failed to read file for hashing: " + path;
            }
            goto cleanup;
        }

        if (bytesRead == 0) {
            break;
        }

        if (BCryptHashData(hashHandle, buffer, bytesRead, 0) < 0) {
            if (error) {
                *error = "Failed to hash file: " + path;
            }
            goto cleanup;
        }
    }

    if (BCryptFinishHash(hashHandle, hashBytes.data(), hashLength, 0) < 0) {
        if (error) {
            *error = "Failed to finish SHA256 hash.";
        }
        goto cleanup;
    }

    *hash = BytesToHex(hashBytes.data(), hashBytes.size());
    ok = true;

cleanup:
    if (hashHandle) {
        BCryptDestroyHash(hashHandle);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    ::CloseHandle(file);
    return ok;
}

std::string EscapeBackupPath(const std::string& relativePath)
{
    std::string escaped;
    char buffer[4] = {};
    for (char ch : NormalizeRelativePath(relativePath)) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_') {
            escaped.push_back(static_cast<char>(value));
        }
        else {
            std::snprintf(buffer, sizeof(buffer), "%02X", value);
            escaped.push_back('%');
            escaped += buffer;
        }
    }
    return escaped;
}

std::string GetBackupRelativePath(const std::string& manifestOwner, const std::string& relativePath)
{
    return JoinPath(JoinPath(JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), ".launcher"), "backups"), JoinPath(manifestOwner, EscapeBackupPath(relativePath) + ".bak"));
}

const char* ActionToString(InstallAction action)
{
    return action == InstallAction::Overwritten ? "overwritten" : "created";
}

ManifestInstallAction ToPublicAction(InstallAction action)
{
    return action == InstallAction::Overwritten ? ManifestInstallAction::Overwritten : ManifestInstallAction::Created;
}

bool ParseAction(const std::string& value, InstallAction* action)
{
    if (!action) {
        return false;
    }

    if (value == "created") {
        *action = InstallAction::Created;
        return true;
    }
    if (value == "overwritten") {
        *action = InstallAction::Overwritten;
        return true;
    }
    return false;
}

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> parts;
    std::string current;
    for (char ch : line) {
        if (ch == '\t') {
            parts.push_back(current);
            current.clear();
        }
        else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

bool ReadInstallManifestEntries(const std::string& gameRoot, const std::string& manifestOwner, std::vector<InstallManifestEntry>* entries)
{
    if (!entries) {
        return false;
    }

    entries->clear();
    std::string text;
    if (!ReadFileText(GetInstallManifestPath(gameRoot, manifestOwner), &text)) {
        return false;
    }

    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (Trim(line).empty()) {
            continue;
        }

        const std::vector<std::string> parts = SplitTabs(line);
        if (parts.size() != 5) {
            return false;
        }

        InstallManifestEntry entry;
        entry.relativePath = NormalizeRelativePath(parts[0]);
        if (!IsSafeRelativePath(entry.relativePath) || !ParseAction(parts[1], &entry.action)) {
            return false;
        }
        entry.beforeSha256 = parts[2];
        entry.installedSha256 = parts[3];
        entry.backupRelativePath = NormalizeRelativePath(parts[4]);
        if (!entry.backupRelativePath.empty() && !IsSafeRelativePath(entry.backupRelativePath)) {
            return false;
        }
        entries->push_back(entry);
    }

    return true;
}

bool WriteInstallManifestEntries(const std::string& gameRoot, const std::string& manifestOwner, const std::vector<InstallManifestEntry>& entries, std::string* error)
{
    const std::string manifestDirectory = GetManifestDirectory(gameRoot);
    if (!CreateDirectoryRecursive(manifestDirectory)) {
        if (error) {
            *error = "Failed to create manifest directory: " + manifestDirectory;
        }
        return false;
    }

    std::string text;
    for (const InstallManifestEntry& entry : entries) {
        if (!IsSafeRelativePath(entry.relativePath)
            || (!entry.backupRelativePath.empty() && !IsSafeRelativePath(entry.backupRelativePath))) {
            if (error) {
                *error = "Unsafe manifest path: " + entry.relativePath;
            }
            return false;
        }

        text += NormalizeRelativePath(entry.relativePath);
        text += "\t";
        text += ActionToString(entry.action);
        text += "\t";
        text += entry.beforeSha256;
        text += "\t";
        text += entry.installedSha256;
        text += "\t";
        text += NormalizeRelativePath(entry.backupRelativePath);
        text += "\r\n";
    }

    const std::string manifestPath = GetInstallManifestPath(gameRoot, manifestOwner);
    if (!WriteFileText(manifestPath, text)) {
        if (error) {
            *error = "Failed to write manifest: " + manifestPath;
        }
        return false;
    }

    return true;
}

ManifestCurrentState GetCurrentState(const std::string& gameRoot, const InstallManifestEntry& entry)
{
    const std::string targetPath = JoinPath(gameRoot, entry.relativePath);
    if (!FileExists(targetPath.c_str())) {
        return ManifestCurrentState::Missing;
    }

    std::string currentHash;
    std::string ignoredError;
    if (!ComputeFileSha256(targetPath, &currentHash, &ignoredError)) {
        return ManifestCurrentState::Changed;
    }

    return _stricmp(currentHash.c_str(), entry.installedSha256.c_str()) == 0
        ? ManifestCurrentState::Unchanged
        : ManifestCurrentState::Changed;
}

void EnumeratePackageFilesRecursive(
    const std::string& root,
    const std::string& directory,
    std::vector<PackageFile>* files)
{
    const std::string pattern = JoinPath(directory, "*");
    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = ::FindFirstFileA(pattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") {
            continue;
        }

        const std::string fullPath = JoinPath(directory, name);
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            EnumeratePackageFilesRecursive(root, fullPath, files);
            continue;
        }

        std::string relativePath = fullPath.substr(root.size());
        if (!relativePath.empty() && (relativePath[0] == '\\' || relativePath[0] == '/')) {
            relativePath.erase(relativePath.begin());
        }

        files->push_back({fullPath, NormalizeRelativePath(relativePath)});
    } while (::FindNextFileA(findHandle, &findData));

    ::FindClose(findHandle);
}

bool ValidatePackageFiles(const std::vector<PackageFile>& files, std::string* invalidPath)
{
    for (const PackageFile& file : files) {
        if (!IsAllowedPackageRelativePath(file.relativePath)) {
            if (invalidPath) {
                *invalidPath = file.relativePath;
            }
            return false;
        }
    }
    return true;
}

bool IsRootPackageMetadata(const std::string& relativePath)
{
    const std::string normalized = NormalizeRelativePath(relativePath);
    if (normalized.find('\\') != std::string::npos) {
        return false;
    }

    return HasExtensionNoCase(normalized, ".txt")
        || HasExtensionNoCase(normalized, ".md")
        || HasExtensionNoCase(normalized, ".ps1")
        || HasExtensionNoCase(normalized, ".bat")
        || HasExtensionNoCase(normalized, ".cmd")
        || HasExtensionNoCase(normalized, ".zip")
        || HasExtensionNoCase(normalized, ".rar")
        || _stricmp(normalized.c_str(), "install") == 0
        || _stricmp(normalized.c_str(), "readme") == 0;
}

bool IsIgnoredLauncherPackageFile(const std::string& relativePath)
{
    const std::string normalized = NormalizeRelativePath(relativePath);
    return _stricmp(normalized.c_str(), "bin\\Final\\GameModLauncher.exe") == 0
        || _stricmp(normalized.c_str(), "bin\\Final\\GameModLauncher.ini") == 0
        || _stricmp(normalized.c_str(), "bin\\Final\\banner.txt") == 0
        || _stricmp(normalized.c_str(), "bin\\Final\\banner.bmp") == 0
        || _stricmp(normalized.c_str(), "bin\\Final\\mods\\.launcher\\banner.txt") == 0
        || _stricmp(normalized.c_str(), "bin\\Final\\mods\\.launcher\\banner.bmp") == 0
        || _stricmp(normalized.c_str(), "bin\\Final\\mods\\.launcher\\banner.png") == 0;
}

bool IsIgnoredPackageFile(const std::string& relativePath)
{
    return IsRootPackageMetadata(relativePath) || IsIgnoredLauncherPackageFile(relativePath);
}

void FilterIgnoredPackageFiles(std::vector<PackageFile>* files)
{
    if (!files) {
        return;
    }

    files->erase(
        std::remove_if(
            files->begin(),
            files->end(),
            [](const PackageFile& file) {
                return IsIgnoredPackageFile(file.relativePath);
            }),
        files->end());
}

bool GetSingleChildDirectory(const std::string& directory, std::string* childDirectory)
{
    const std::string pattern = JoinPath(directory, "*");
    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = ::FindFirstFileA(pattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::string singleDirectory;
    bool hasFile = false;
    int directoryCount = 0;
    do {
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") {
            continue;
        }

        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            ++directoryCount;
            singleDirectory = JoinPath(directory, name);
        }
        else {
            hasFile = true;
        }
    } while (::FindNextFileA(findHandle, &findData));

    ::FindClose(findHandle);
    if (hasFile || directoryCount != 1) {
        return false;
    }

    if (childDirectory) {
        *childDirectory = singleDirectory;
    }
    return true;
}

bool IsKnownLooseRootDirectory(const std::string& name)
{
    static const char* kKnownNames[] = {
        "Actors",
        "Scripts",
        "Sounds",
        "Textures",
        "UI",
        "Video",
        "Scenes",
        "Geometries",
        "World",
        "data",
        "bin"
    };

    for (const char* knownName : kKnownNames) {
        if (_stricmp(name.c_str(), knownName) == 0) {
            return true;
        }
    }
    return false;
}

bool ShouldStripLooseWrapper(const std::string& childDirectory, const std::string& targetRelativeDirectory)
{
    const std::string childName = FileNamePart(childDirectory);
    if (!StartsWithNoCase(targetRelativeDirectory, "data\\")) {
        return !IsKnownLooseRootDirectory(childName);
    }

    const std::string targetName = FileNamePart(targetRelativeDirectory);
    if (_stricmp(childName.c_str(), targetName.c_str()) == 0) {
        return true;
    }

    return !IsKnownLooseRootDirectory(childName);
}

bool EnumerateLoosePackageFiles(
    const std::string& packageRoot,
    const std::string& targetRelativeDirectory,
    std::vector<PackageFile>* files,
    std::string* error)
{
    if (!files) {
        return false;
    }

    files->clear();
    std::string normalizedTarget = NormalizeRelativePath(targetRelativeDirectory);
    if (!IsSafeRelativePath(normalizedTarget)
        || (_stricmp(normalizedTarget.c_str(), "data") != 0 && !StartsWithNoCase(normalizedTarget, "data\\"))) {
        if (error) {
            *error = "Unsupported target folder: " + targetRelativeDirectory;
        }
        return false;
    }

    std::string sourceRoot = packageRoot;
    std::string childDirectory;
    if (GetSingleChildDirectory(packageRoot, &childDirectory) && ShouldStripLooseWrapper(childDirectory, normalizedTarget)) {
        sourceRoot = childDirectory;
    }

    std::vector<PackageFile> rawFiles;
    EnumeratePackageFilesRecursive(sourceRoot, sourceRoot, &rawFiles);
    if (rawFiles.empty()) {
        if (error) {
            *error = "Package folder contains no files.";
        }
        return false;
    }

    for (PackageFile& file : rawFiles) {
        if (!IsSafeRelativePath(file.relativePath)) {
            if (error) {
                *error = "Unsafe package path: " + file.relativePath;
            }
            return false;
        }
        file.relativePath = NormalizeRelativePath(JoinPath(normalizedTarget, file.relativePath));
    }

    *files = rawFiles;
    return true;
}
}

std::string GetDefaultGameRootDirectory()
{
    return DirectoryName(DirectoryName(GetModuleDirectory()));
}

std::string GetManifestDirectory(const std::string& gameRoot)
{
    return JoinPath(JoinPath(JoinPath(JoinPath(gameRoot, "bin"), "Final"), "mods"), ".launcher");
}

std::string GetInstallManifestPath(const std::string& gameRoot, const std::string& manifestOwner)
{
    return JoinPath(GetManifestDirectory(gameRoot), manifestOwner + ".install.txt");
}

std::string NormalizeRelativePath(std::string path)
{
    for (char& ch : path) {
        if (ch == '/') {
            ch = '\\';
        }
    }

    while (!path.empty() && (path.front() == '\\' || path.front() == '/')) {
        path.erase(path.begin());
    }

    return path;
}

bool IsSafeRelativePath(const std::string& relativePath)
{
    const std::string normalized = NormalizeRelativePath(relativePath);
    if (normalized.empty() || IsAbsolutePath(normalized)) {
        return false;
    }

    std::istringstream stream(normalized);
    std::string segment;
    while (std::getline(stream, segment, '\\')) {
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
    }

    return true;
}

bool IsAllowedPackageRelativePath(const std::string& relativePath)
{
    const std::string normalized = NormalizeRelativePath(relativePath);
    return IsSafeRelativePath(normalized)
        && (StartsWithNoCase(normalized, "bin\\Final\\mods\\") || StartsWithNoCase(normalized, "data\\"));
}

std::string SanitizeManifestOwner(const std::string& name)
{
    std::string result;
    bool lastWasSeparator = false;
    for (char ch : name) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (std::isalnum(value) != 0 || ch == '.' || ch == '-' || ch == '_') {
            result.push_back(ch);
            lastWasSeparator = false;
        }
        else if (!lastWasSeparator) {
            result.push_back('_');
            lastWasSeparator = true;
        }
    }

    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    if (result.empty()) {
        return "resource_mod";
    }
    return result;
}

bool EnumeratePackageFiles(const std::string& packageRoot, std::vector<PackageFile>* files, std::string* error)
{
    if (!files) {
        return false;
    }

    files->clear();
    if (!DirectoryExists(packageRoot.c_str())) {
        if (error) {
            *error = "Package folder not found: " + packageRoot;
        }
        return false;
    }

    EnumeratePackageFilesRecursive(packageRoot, packageRoot, files);
    FilterIgnoredPackageFiles(files);
    if (files->empty()) {
        if (error) {
            *error = "Package folder contains no installable files.";
        }
        return false;
    }

    std::string invalidPath;
    if (ValidatePackageFiles(*files, &invalidPath)) {
        return true;
    }

    std::string nestedRoot;
    if (GetSingleChildDirectory(packageRoot, &nestedRoot)) {
        std::vector<PackageFile> nestedFiles;
        EnumeratePackageFilesRecursive(nestedRoot, nestedRoot, &nestedFiles);
        FilterIgnoredPackageFiles(&nestedFiles);
        std::string nestedInvalidPath;
        if (!nestedFiles.empty() && ValidatePackageFiles(nestedFiles, &nestedInvalidPath)) {
            *files = nestedFiles;
            return true;
        }
    }

    if (error) {
        *error = "Unsupported package path: " + invalidPath
            + "\r\n\r\nPackages must contain files under bin\\Final\\mods or data. "
              "A single top-level wrapper folder is allowed.";
    }
    return false;
}

bool EnumeratePackageFilesForTarget(
    const std::string& packageRoot,
    const std::string& targetRelativeDirectory,
    std::vector<PackageFile>* files,
    std::string* error)
{
    if (!DirectoryExists(packageRoot.c_str())) {
        if (error) {
            *error = "Package folder not found: " + packageRoot;
        }
        return false;
    }

    return EnumerateLoosePackageFiles(packageRoot, targetRelativeDirectory, files, error);
}

bool FindPackageFileByRelativePath(
    const std::string& packageRoot,
    const std::string& relativePath,
    std::string* sourcePath)
{
    const std::string normalized = NormalizeRelativePath(relativePath);
    if (!IsSafeRelativePath(normalized)) {
        return false;
    }

    const std::string directPath = JoinPath(packageRoot, normalized);
    if (FileExists(directPath.c_str())) {
        if (sourcePath) {
            *sourcePath = directPath;
        }
        return true;
    }

    std::string nestedRoot;
    if (GetSingleChildDirectory(packageRoot, &nestedRoot)) {
        const std::string nestedPath = JoinPath(nestedRoot, normalized);
        if (FileExists(nestedPath.c_str())) {
            if (sourcePath) {
                *sourcePath = nestedPath;
            }
            return true;
        }
    }

    return false;
}

std::vector<std::string> FindPackageDllNames(const std::vector<PackageFile>& files)
{
    std::set<std::string> seen;
    std::vector<std::string> dllNames;
    for (const PackageFile& file : files) {
        if (!StartsWithNoCase(file.relativePath, "bin\\Final\\mods\\") || !HasExtensionNoCase(file.relativePath, ".dll")) {
            continue;
        }

        const std::string dllName = FileNamePart(file.relativePath);
        const std::string key = ToLower(dllName);
        if (seen.insert(key).second) {
            dllNames.push_back(dllName);
        }
    }

    return dllNames;
}

bool PackageHasResourceFiles(const std::vector<PackageFile>& files)
{
    for (const PackageFile& file : files) {
        if (StartsWithNoCase(file.relativePath, "data\\")) {
            return true;
        }
    }
    return false;
}

bool InstallModPackageFromDirectory(
    const std::string& packageRoot,
    const std::string& gameRoot,
    const std::string& manifestOwner,
    PackageInstallResult* result,
    std::string* error)
{
    std::vector<PackageFile> files;
    if (!EnumeratePackageFiles(packageRoot, &files, error)) {
        return false;
    }

    return InstallModPackageFiles(files, gameRoot, manifestOwner, result, error);
}

bool InstallModPackageFiles(
    const std::vector<PackageFile>& files,
    const std::string& gameRoot,
    const std::string& manifestOwner,
    PackageInstallResult* result,
    std::string* error,
    const std::vector<std::string>& skippedRelativePaths)
{
    if (result) {
        *result = PackageInstallResult();
    }

    const std::vector<std::string> dllNames = FindPackageDllNames(files);
    const bool hasResourceFiles = PackageHasResourceFiles(files);
    if (dllNames.empty() && !hasResourceFiles) {
        if (error) {
            *error = "Package must contain a DLL under bin\\Final\\mods or resource files under data.";
        }
        return false;
    }

    if (manifestOwner.empty()) {
        if (error) {
            *error = "Package manifest owner is empty.";
        }
        return false;
    }

    std::set<std::string> skippedPathKeys;
    for (const std::string& skippedRelativePath : skippedRelativePaths) {
        const std::string normalized = NormalizeRelativePath(skippedRelativePath);
        if (!IsSafeRelativePath(normalized)) {
            if (error) {
                *error = "Unsafe skipped package path: " + skippedRelativePath;
            }
            return false;
        }
        skippedPathKeys.insert(ToLower(normalized));
    }

    std::vector<InstallManifestEntry> manifestEntries;
    for (const PackageFile& file : files) {
        const std::string normalizedRelativePath = NormalizeRelativePath(file.relativePath);
        if (skippedPathKeys.find(ToLower(normalizedRelativePath)) != skippedPathKeys.end()) {
            if (result) {
                result->skippedRelativePaths.push_back(normalizedRelativePath);
            }
            continue;
        }

        const std::string targetPath = JoinPath(gameRoot, file.relativePath);
        const std::string targetDirectory = DirectoryName(targetPath);
        if (!CreateDirectoryRecursive(targetDirectory)) {
            if (error) {
                *error = "Failed to create directory for " + targetPath;
            }
            return false;
        }

        InstallManifestEntry entry;
        entry.relativePath = normalizedRelativePath;
        if (FileExists(targetPath.c_str())) {
            entry.action = InstallAction::Overwritten;
            if (!ComputeFileSha256(targetPath, &entry.beforeSha256, error)) {
                return false;
            }
            entry.backupRelativePath = GetBackupRelativePath(manifestOwner, entry.relativePath);
            const std::string backupPath = JoinPath(gameRoot, entry.backupRelativePath);
            if (!CreateDirectoryRecursive(DirectoryName(backupPath))) {
                if (error) {
                    *error = "Failed to create backup directory for " + backupPath;
                }
                return false;
            }
            if (::CopyFileA(targetPath.c_str(), backupPath.c_str(), FALSE) == FALSE) {
                if (error) {
                    *error = "Failed to backup existing file before overwrite: " + targetPath;
                }
                return false;
            }
        }
        else {
            entry.action = InstallAction::Created;
        }

        if (::CopyFileA(file.sourcePath.c_str(), targetPath.c_str(), FALSE) == FALSE) {
            if (error) {
                *error = "Failed to install file: " + targetPath;
            }
            return false;
        }

        if (!ComputeFileSha256(targetPath, &entry.installedSha256, error)) {
            return false;
        }
        manifestEntries.push_back(entry);
        if (result) {
            result->installedRelativePaths.push_back(entry.relativePath);
        }
    }

    if (!WriteInstallManifestEntries(gameRoot, manifestOwner, manifestEntries, error)) {
        return false;
    }

    if (result) {
        result->dllNames = dllNames;
        result->hasResourceFiles = hasResourceFiles;
    }
    return true;
}

bool ReadInstallManifestEntriesInfo(const std::string& gameRoot, const std::string& manifestOwner, std::vector<InstallManifestEntryInfo>* entries)
{
    if (!entries) {
        return false;
    }

    entries->clear();
    std::vector<InstallManifestEntry> internalEntries;
    if (!ReadInstallManifestEntries(gameRoot, manifestOwner, &internalEntries)) {
        return false;
    }

    for (const InstallManifestEntry& entry : internalEntries) {
        InstallManifestEntryInfo info;
        info.owner = manifestOwner;
        info.relativePath = entry.relativePath;
        info.action = ToPublicAction(entry.action);
        info.beforeSha256 = entry.beforeSha256;
        info.installedSha256 = entry.installedSha256;
        info.backupRelativePath = entry.backupRelativePath;
        info.currentState = GetCurrentState(gameRoot, entry);
        entries->push_back(info);
    }

    return true;
}

bool ReadInstallManifest(const std::string& gameRoot, const std::string& manifestOwner, std::vector<std::string>* relativePaths)
{
    if (!relativePaths) {
        return false;
    }

    relativePaths->clear();
    std::vector<InstallManifestEntry> entries;
    if (!ReadInstallManifestEntries(gameRoot, manifestOwner, &entries)) {
        return false;
    }

    for (const InstallManifestEntry& entry : entries) {
        relativePaths->push_back(entry.relativePath);
    }
    return true;
}

bool WriteInstallManifest(const std::string& gameRoot, const std::string& manifestOwner, const std::vector<std::string>& relativePaths, std::string* error)
{
    std::vector<InstallManifestEntry> entries;
    for (const std::string& relativePath : relativePaths) {
        if (!IsSafeRelativePath(relativePath)) {
            if (error) {
                *error = "Unsafe manifest path: " + relativePath;
            }
            return false;
        }

        InstallManifestEntry entry;
        entry.relativePath = NormalizeRelativePath(relativePath);
        entry.action = InstallAction::Created;
        entries.push_back(entry);
    }

    return WriteInstallManifestEntries(gameRoot, manifestOwner, entries, error);
}

std::vector<std::string> GetLegacyDeleteRelativePaths(const std::string& dllName)
{
    return {
        JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), dllName),
        JoinPath(JoinPath(JoinPath("bin", "Final"), "mods"), ReplaceExtension(dllName, ".ini"))
    };
}

bool DeleteInstalledModFiles(
    const std::string& gameRoot,
    const std::string& manifestOwner,
    bool allowLegacyDllFallback,
    ModDeleteResult* result,
    std::string* error,
    const std::vector<std::string>& protectedRelativePaths)
{
    if (result) {
        *result = ModDeleteResult();
    }

    std::set<std::string> protectedPathKeys;
    for (const std::string& protectedRelativePath : protectedRelativePaths) {
        const std::string normalized = NormalizeRelativePath(protectedRelativePath);
        if (!IsSafeRelativePath(normalized)) {
            if (error) {
                *error = "Unsafe protected delete path: " + protectedRelativePath;
            }
            return false;
        }
        protectedPathKeys.insert(ToLower(normalized));
    }

    std::vector<InstallManifestEntry> entries;
    if (ReadInstallManifestEntries(gameRoot, manifestOwner, &entries)) {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            const InstallManifestEntry& entry = *it;
            const std::string relativePath = NormalizeRelativePath(entry.relativePath);
            if (!IsSafeRelativePath(relativePath)) {
                if (error) {
                    *error = "Unsafe delete path: " + relativePath;
                }
                return false;
            }

            if (protectedPathKeys.find(ToLower(relativePath)) != protectedPathKeys.end()) {
                if (result) {
                    result->skippedRelativePaths.push_back(relativePath);
                }
                continue;
            }

            const std::string targetPath = JoinPath(gameRoot, relativePath);
            const bool targetExists = FileExists(targetPath.c_str());
            bool currentMatchesInstalled = !targetExists;
            if (targetExists) {
                std::string currentHash;
                if (!ComputeFileSha256(targetPath, &currentHash, error)) {
                    return false;
                }
                currentMatchesInstalled = _stricmp(currentHash.c_str(), entry.installedSha256.c_str()) == 0;
            }

            if (targetExists && !currentMatchesInstalled) {
                if (result) {
                    result->skippedRelativePaths.push_back(relativePath);
                }
                continue;
            }

            if (entry.action == InstallAction::Created) {
                if (targetExists && !DeleteFileIfExists(targetPath, error)) {
                    return false;
                }
                if (result) {
                    result->deletedRelativePaths.push_back(relativePath);
                }
            }
            else {
                const std::string backupPath = JoinPath(gameRoot, entry.backupRelativePath);
                if (!FileExists(backupPath.c_str())) {
                    if (result) {
                        result->skippedRelativePaths.push_back(relativePath);
                    }
                    continue;
                }

                if (targetExists && !DeleteFileIfExists(targetPath, error)) {
                    return false;
                }
                if (!MoveFileIfExists(backupPath, targetPath, error)) {
                    return false;
                }
                if (result) {
                    result->restoredRelativePaths.push_back(relativePath);
                }
            }
        }

        if (result && result->skippedRelativePaths.empty()) {
            const std::string manifestPath = GetInstallManifestPath(gameRoot, manifestOwner);
            if (!DeleteFileIfExists(manifestPath, error)) {
                return false;
            }
        }
        return true;
    }

    if (!allowLegacyDllFallback) {
        return true;
    }

    const std::string dllName = manifestOwner;
    std::vector<std::string> relativePaths = GetLegacyDeleteRelativePaths(dllName);
    for (auto it = relativePaths.rbegin(); it != relativePaths.rend(); ++it) {
        const std::string relativePath = NormalizeRelativePath(*it);
        if (!IsSafeRelativePath(relativePath)) {
            if (error) {
                *error = "Unsafe delete path: " + relativePath;
            }
            return false;
        }

        const std::string targetPath = JoinPath(gameRoot, relativePath);
        if (protectedPathKeys.find(ToLower(relativePath)) != protectedPathKeys.end()) {
            if (result) {
                result->skippedRelativePaths.push_back(relativePath);
            }
            continue;
        }
        if (!DeleteFileIfExists(targetPath, error)) {
            return false;
        }

        if (result) {
            result->deletedRelativePaths.push_back(relativePath);
        }
    }

    return true;
}
}
