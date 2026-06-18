#include "mod_ini.h"

#include "path_utils.h"
#include "string_utils.h"

#include <sstream>

namespace uml
{
bool ParseModIniText(const std::string& text, std::vector<ModIniEntry>* entries)
{
    if (!entries) {
        return false;
    }

    entries->clear();
    std::string currentSection;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']' && trimmed.size() >= 2) {
            currentSection = Trim(trimmed.substr(1, trimmed.size() - 2));
            if (currentSection.empty()) {
                return false;
            }
            continue;
        }

        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            return false;
        }

        ModIniEntry entry;
        entry.section = currentSection;
        entry.key = Trim(trimmed.substr(0, equals));
        entry.value = Trim(trimmed.substr(equals + 1));
        if (entry.key.empty()) {
            return false;
        }

        entries->push_back(entry);
    }

    return true;
}

std::string SerializeModIniEntries(const std::vector<ModIniEntry>& entries)
{
    std::string result;
    std::string currentSection;
    bool firstSection = true;

    for (const ModIniEntry& entry : entries) {
        if (entry.section != currentSection || firstSection) {
            if (!firstSection) {
                result += "\r\n";
            }

            currentSection = entry.section;
            firstSection = false;
            if (!currentSection.empty()) {
                result += "[";
                result += currentSection;
                result += "]\r\n";
            }
        }

        result += entry.key;
        result += "=";
        result += entry.value;
        result += "\r\n";
    }

    return result;
}

ModIniDocument LoadModIniDocument(const std::string& path)
{
    ModIniDocument document;
    document.path = path;
    document.exists = FileExists(path.c_str());
    if (!document.exists) {
        document.parseOk = true;
        return document;
    }

    if (!ReadFileText(path, &document.rawText)) {
        document.parseOk = false;
        return document;
    }

    document.parseOk = ParseModIniText(document.rawText, &document.entries);
    return document;
}
}
