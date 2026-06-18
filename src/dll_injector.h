#pragma once

#include <windows.h>

namespace uml
{
bool InjectDll(HANDLE process, const char* dllPath);
}
