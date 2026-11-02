// Validates ac2_layout.h against an AssassinsCreedIIGame.exe on disk, mapped like the loader
// maps it, then checks that a single corrupted signature byte is rejected.
//   layout-profile-test.exe <AssassinsCreedIIGame.exe> <label>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ac2_layout.h"

namespace {

bool LoadImage(const wchar_t* path, std::vector<std::uint8_t>& image) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    std::vector<std::uint8_t> raw;
    DWORD read = 0;
    bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart < (1LL << 30);
    if (ok) {
        raw.resize(static_cast<std::size_t>(size.QuadPart));
        ok = ReadFile(file, raw.data(), static_cast<DWORD>(raw.size()), &read, nullptr) != FALSE &&
             read == raw.size();
    }
    CloseHandle(file);
    if (!ok || raw.size() < sizeof(IMAGE_DOS_HEADER)) return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > raw.size()) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.ImageBase != ac2::kImageBase) {
        return false;
    }
    image.assign(nt->OptionalHeader.SizeOfImage, 0);
    std::memcpy(image.data(), raw.data(), std::min<std::size_t>(nt->OptionalHeader.SizeOfHeaders, raw.size()));
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        const std::size_t count = std::min<std::size_t>(section->SizeOfRawData, section->Misc.VirtualSize);
        if (section->PointerToRawData + count > raw.size() || section->VirtualAddress + count > image.size()) {
            return false;
        }
        std::memcpy(image.data() + section->VirtualAddress, raw.data() + section->PointerToRawData, count);
    }
    return true;
}

}  // namespace

int wmain(int argumentCount, wchar_t** arguments) {
    if (argumentCount != 3) {
        std::fwprintf(stderr, L"Usage: layout-profile-test.exe <AssassinsCreedIIGame.exe> <label>\n");
        return 2;
    }
    std::vector<std::uint8_t> image;
    if (!LoadImage(arguments[1], image)) {
        std::fwprintf(stderr, L"[%ls] could not map %ls as an x86 image at %08X\n", arguments[2],
                      arguments[1], static_cast<unsigned>(ac2::kImageBase));
        return 3;
    }
    auto reader = [&image](std::uint32_t va, void* destination, std::size_t length) {
        if (va < ac2::kImageBase || va - ac2::kImageBase + length > image.size()) return false;
        std::memcpy(destination, image.data() + (va - ac2::kImageBase), length);
        return true;
    };

    char failure[256]{};
    if (!ac2::ValidateLayout(reader, failure, sizeof(failure))) {
        std::fprintf(stderr, "[%ls] layout rejected: %s\n", arguments[2], failure);
        return 4;
    }

    // Negative check: flip one byte of every code signature in turn; each must be rejected.
    for (const ac2::CodeSignature& signature : ac2::kCodeSignatures) {
        std::uint8_t& byte = image[signature.va - ac2::kImageBase];
        byte ^= 0xFF;
        const bool accepted = ac2::ValidateLayout(reader, failure, sizeof(failure));
        byte ^= 0xFF;
        if (accepted) {
            std::fprintf(stderr, "[%ls] corrupted signature was accepted: %s\n", arguments[2],
                         signature.name);
            return 5;
        }
    }
    std::wprintf(L"[%ls] layout accepted; corrupted signatures rejected.\n", arguments[2]);
    return 0;
}
