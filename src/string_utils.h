#pragma once

#include <cstdint>
#include <string>

namespace uml
{
std::string Trim(const std::string& value);
std::string ToLower(std::string value);
bool ParseUint32(const std::string& value, uint32_t* parsedValue);
std::string Uint32ToString(uint32_t value);
}
