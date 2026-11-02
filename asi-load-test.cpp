// Loads the ASI into a host that is not the game and checks that it stays inactive.
//   asi-load-test.exe <plugin.asi>                 host name differs: expects the host diagnostic
//   asi-load-test.exe <plugin.asi> --layout-host   run as build\layout-host\AssassinsCreedIIGame.exe:
//                                                  expects the layout-mismatch diagnostic
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

bool GetFileSize(const wchar_t* path, LARGE_INTEGER& size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        size.QuadPart = 0;
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    const bool ok = GetFileSizeEx(file, &size) != FALSE;
    CloseHandle(file);
    return ok;
}

bool AppendedLogContains(const wchar_t* path, LONGLONG offset, const char* expected) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER position{};
    position.QuadPart = offset;
    char appended[8192]{};
    DWORD bytesRead = 0;
    const bool ok = SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE &&
                    ReadFile(file, appended, sizeof(appended) - 1, &bytesRead, nullptr) != FALSE;
    CloseHandle(file);
    return ok && std::strstr(appended, expected) != nullptr;
}

bool BuildSiblingPath(const wchar_t* modulePath, const wchar_t* filename, wchar_t (&output)[MAX_PATH]) {
    const DWORD length = GetFullPathNameW(modulePath, MAX_PATH, output, nullptr);
    if (length == 0 || length >= MAX_PATH) return false;
    wchar_t* slash = std::wcsrchr(output, L'\\');
    if (slash == nullptr) return false;
    slash[1] = L'\0';
    return wcscat_s(output, filename) == 0;
}

}  // namespace

int wmain(int argumentCount, wchar_t** arguments) {
    const bool layoutHost = argumentCount == 3 && std::wcscmp(arguments[2], L"--layout-host") == 0;
    if (argumentCount != 2 && !layoutHost) {
        std::fwprintf(stderr, L"Usage: asi-load-test.exe <plugin.asi> [--layout-host]\n");
        return 2;
    }
    const char* expected = layoutHost ? "Executable layout does not match the supported layout"
                                      : "Host executable is not AssassinsCreedIIGame.exe";

    wchar_t logPath[MAX_PATH]{};
    if (!BuildSiblingPath(arguments[1], L"ac2-raw-mouse-fix.log", logPath)) {
        std::fwprintf(stderr, L"Unable to derive the plugin's log path.\n");
        return 3;
    }
    LARGE_INTEGER initialSize{};
    if (!GetFileSize(logPath, initialSize)) {
        std::fwprintf(stderr, L"Unable to inspect the log: %lu\n", GetLastError());
        return 4;
    }

    HMODULE plugin = LoadLibraryW(arguments[1]);
    if (plugin == nullptr) {
        std::fwprintf(stderr, L"LoadLibraryW failed for the ASI: %lu\n", GetLastError());
        return 5;
    }
    if (GetProcAddress(plugin, "DirectInput8Create") != nullptr) {
        std::fwprintf(stderr, L"The ASI unexpectedly exports DirectInput8Create.\n");
        return 6;
    }

    for (int attempt = 0; attempt < 100; ++attempt) {
        LARGE_INTEGER size{};
        if (GetFileSize(logPath, size) && size.QuadPart > initialSize.QuadPart &&
            AppendedLogContains(logPath, initialSize.QuadPart, expected)) {
            std::wprintf(L"ASI load test passed (%ls).\n", layoutHost ? L"layout host" : L"foreign host");
            return 0;
        }
        Sleep(50);
    }
    std::fwprintf(stderr, L"The ASI did not log the expected diagnostic: %hs\n", expected);
    return 8;
}
