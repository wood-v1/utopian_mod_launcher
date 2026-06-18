#include "ui_helpers.h"

#include <vector>

namespace uml::windows_ui
{
std::string GetWindowTextString(HWND window)
{
    const int length = ::GetWindowTextLengthA(window);
    std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
    ::GetWindowTextA(window, buffer.data(), length + 1);
    return std::string(buffer.data());
}

void SetWindowTextString(HWND window, const std::string& value)
{
    ::SetWindowTextA(window, value.c_str());
}

void ShowError(HWND parent, const std::string& message)
{
    ::MessageBoxA(parent, message.c_str(), "UtopianModLauncher", MB_ICONERROR | MB_OK);
}

void ShowInfo(HWND parent, const std::string& message)
{
    ::MessageBoxA(parent, message.c_str(), "UtopianModLauncher", MB_ICONINFORMATION | MB_OK);
}

void AddListViewColumn(HWND list, int column, const char* text, int width)
{
    LVCOLUMNA lvColumn = {};
    lvColumn.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    lvColumn.pszText = const_cast<char*>(text);
    lvColumn.cx = width;
    lvColumn.iSubItem = column;
    ListView_InsertColumn(list, column, &lvColumn);
}

void InsertListViewText(HWND list, int item, int subItem, const std::string& text)
{
    if (subItem == 0) {
        LVITEMA lvItem = {};
        lvItem.mask = LVIF_TEXT;
        lvItem.iItem = item;
        lvItem.iSubItem = 0;
        lvItem.pszText = const_cast<char*>(text.c_str());
        ListView_InsertItem(list, &lvItem);
    }
    else {
        ListView_SetItemText(list, item, subItem, const_cast<char*>(text.c_str()));
    }
}
}
