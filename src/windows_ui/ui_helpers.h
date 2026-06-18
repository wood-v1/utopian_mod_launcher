#pragma once

#include <windows.h>
#include <commctrl.h>

#include <string>

namespace uml::windows_ui
{
std::string GetWindowTextString(HWND window);
void SetWindowTextString(HWND window, const std::string& value);
void ShowError(HWND parent, const std::string& message);
void ShowInfo(HWND parent, const std::string& message);
void AddListViewColumn(HWND list, int column, const char* text, int width);
void InsertListViewText(HWND list, int item, int subItem, const std::string& text);
}
