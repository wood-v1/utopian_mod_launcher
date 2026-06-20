#include "launch_overlay.h"
#include "launcher_cli.h"
#include "launcher_version.h"
#include "path_utils.h"
#include "self_tests.h"
#include "windows_ui/launcher_window.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
constexpr DWORD ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004;
#endif

struct RgbColor
{
    int red = 255;
    int green = 180;
    int blue = 64;
};

std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        }
        else {
            current.push_back(ch);
        }
    }

    if (!current.empty()) {
        if (current.back() == '\r') {
            current.pop_back();
        }
        lines.push_back(current);
    }

    return lines;
}

RgbColor InterpolateColor(const RgbColor& from, const RgbColor& to, double amount)
{
    const double clamped = amount < 0.0 ? 0.0 : (amount > 1.0 ? 1.0 : amount);
    RgbColor result;
    result.red = static_cast<int>(from.red + (to.red - from.red) * clamped);
    result.green = static_cast<int>(from.green + (to.green - from.green) * clamped);
    result.blue = static_cast<int>(from.blue + (to.blue - from.blue) * clamped);
    return result;
}

RgbColor AutumnGradient(double amount)
{
    constexpr RgbColor kGold = {255, 211, 94};
    constexpr RgbColor kPumpkin = {255, 142, 36};
    constexpr RgbColor kCopper = {204, 86, 32};
    constexpr RgbColor kBark = {126, 61, 34};

    if (amount < 0.35) {
        return InterpolateColor(kGold, kPumpkin, amount / 0.35);
    }
    if (amount < 0.72) {
        return InterpolateColor(kPumpkin, kCopper, (amount - 0.35) / 0.37);
    }
    return InterpolateColor(kCopper, kBark, (amount - 0.72) / 0.28);
}

void ResizeConsoleToBanner(const std::string& banner)
{
    HWND consoleWindow = ::GetConsoleWindow();
    HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (!consoleWindow || output == INVALID_HANDLE_VALUE || output == nullptr) {
        return;
    }

    const std::vector<std::string> lines = SplitLines(banner);
    if (lines.empty()) {
        return;
    }

    std::size_t maxLineLength = 0;
    for (const std::string& line : lines) {
        maxLineLength = maxLineLength < line.size() ? line.size() : maxLineLength;
    }

    CONSOLE_SCREEN_BUFFER_INFO info = {};
    if (::GetConsoleScreenBufferInfo(output, &info) == FALSE) {
        return;
    }

    const COORD largest = ::GetLargestConsoleWindowSize(output);
    if (largest.X <= 0 || largest.Y <= 0) {
        return;
    }

    const int currentWidth = info.srWindow.Right - info.srWindow.Left + 1;
    const int currentHeight = info.srWindow.Bottom - info.srWindow.Top + 1;
    const int desiredWidth = static_cast<int>(maxLineLength) + 2;
    const int desiredHeight = static_cast<int>(lines.size()) + 1;
    const int targetWidth = std::min<int>(std::max(currentWidth, desiredWidth), largest.X);
    const int targetHeight = std::min<int>(std::max(currentHeight, desiredHeight), largest.Y);

    COORD bufferSize = info.dwSize;
    bufferSize.X = static_cast<SHORT>(std::max<int>(bufferSize.X, targetWidth));
    bufferSize.Y = static_cast<SHORT>(std::max<int>(bufferSize.Y, targetHeight));
    ::SetConsoleScreenBufferSize(output, bufferSize);

    SMALL_RECT windowRect = {};
    windowRect.Left = 0;
    windowRect.Top = 0;
    windowRect.Right = static_cast<SHORT>(targetWidth - 1);
    windowRect.Bottom = static_cast<SHORT>(targetHeight - 1);
    ::SetConsoleWindowInfo(output, TRUE, &windowRect);
}

bool EnableVirtualTerminalColors()
{
    HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE || output == nullptr) {
        return false;
    }

    DWORD mode = 0;
    if (::GetConsoleMode(output, &mode) == FALSE) {
        return false;
    }

    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        return true;
    }

    return ::SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
}

void PrintPlainBannerWithFallbackColor(const std::string& banner)
{
    HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO originalInfo = {};
    const bool hasOriginalInfo = output != INVALID_HANDLE_VALUE
        && output != nullptr
        && ::GetConsoleScreenBufferInfo(output, &originalInfo) != FALSE;

    if (hasOriginalInfo) {
        ::SetConsoleTextAttribute(output, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }

    std::printf("%s", banner.c_str());

    if (hasOriginalInfo) {
        ::SetConsoleTextAttribute(output, originalInfo.wAttributes);
    }
}

void PrintGradientBanner(const std::string& banner)
{
    if (!EnableVirtualTerminalColors()) {
        PrintPlainBannerWithFallbackColor(banner);
        return;
    }

    const std::vector<std::string> lines = SplitLines(banner);
    const std::size_t denominator = lines.size() > 1 ? lines.size() - 1 : 1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const RgbColor color = AutumnGradient(static_cast<double>(i) / static_cast<double>(denominator));
        std::printf("\x1b[38;2;%d;%d;%dm%s\x1b[0m\n", color.red, color.green, color.blue, lines[i].c_str());
    }
}

std::string GetLauncherAssetDirectory()
{
    return uml::JoinPath(uml::JoinPath(uml::GetModuleDirectory(), "mods"), ".launcher");
}

void PrintUiConsoleBanner()
{
    std::string banner;
    const std::string bannerPath = uml::JoinPath(GetLauncherAssetDirectory(), "banner.txt");
    if (uml::ReadFileText(bannerPath, &banner) && !banner.empty()) {
        ResizeConsoleToBanner(banner);
        PrintGradientBanner(banner);
        if (banner.back() != '\n') {
            std::printf("\n");
        }
        return;
    }

    PrintGradientBanner(
        "\n"
        "  UTOPIAN LAUNCHER " + std::string(uml::kLauncherVersion) + "\n"
        "  --------------------\n"
        "\n"
        "  Native UI is open. Use `GameModLauncher.exe help` for CLI commands.\n"
        "\n");
}
}

int main(int argc, char** argv)
{
    if (argc == 5 && ::_stricmp(argv[1], "--launch-overlay") == 0) {
        uml::LaunchOverlayInfo info;
        info.version = uml::kLauncherVersion;
        info.dllModCount = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
        info.resourceModCount = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
        return uml::RunLaunchOverlayProcess(static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)), info);
    }

    if (argc > 1 && ::_stricmp(argv[1], "--self-test") == 0) {
        return uml::RunSelfTests() ? 0 : 1;
    }

    if (uml::IsCliCommand(argc, argv)) {
        return uml::RunLauncherCli(argc, argv);
    }

    if (argc > 1 && ::_stricmp(argv[1], "--ui") != 0) {
        std::printf("Usage: GameModLauncher.exe [--ui|--noui|--noresourcemods|--launch|--self-test|help|--version|list|install|delete|rename|set-logging|set-stage|move|vanilla-files]\n");
        return 1;
    }

    PrintUiConsoleBanner();

    HINSTANCE instance = ::GetModuleHandleA(nullptr);
    uml::windows_ui::LauncherWindow window(instance);
    return window.Run();
}
