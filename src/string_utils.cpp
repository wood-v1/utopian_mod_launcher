#include "string_utils.h"

#include <cctype>
#include <cstdio>

namespace uml
{
std::string Trim(const std::string& value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string ToLower(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool ParseUint32(const std::string& value, uint32_t* parsedValue)
{
    if (!parsedValue || value.empty()) {
        return false;
    }

    unsigned long result = 0;
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }

        result = result * 10 + static_cast<unsigned long>(ch - '0');
    }

    *parsedValue = static_cast<uint32_t>(result);
    return true;
}

std::string Uint32ToString(uint32_t value)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(value));
    return buffer;
}
}
