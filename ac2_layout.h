// Executable layout of Assassin's Creed II (Ubisoft Connect build) used by the raw mouse fix.
//
// Shared by the plugin (reads the running image) and layout-profile-test (reads an exe from
// disk), so both apply exactly the same checks. The image is not relocatable (no
// DYNAMIC_BASE), so absolute addresses inside the signatures are stable.
//
// SHA-256 values only name the build in the log; the signatures and values below decide
// whether the plugin may hook. Any mismatch fails closed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ac2 {

constexpr std::uint32_t kImageBase = 0x00400000;
constexpr std::uint32_t kTimeDateStamp = 0x5925BA62;
constexpr std::uint32_t kSizeOfImage = 0x02170000;
constexpr char kHostExecutable[] = "AssassinsCreedIIGame.exe";

struct KnownBuild {
    const char* sha256;  // upper-case hex
    const char* name;
};

constexpr KnownBuild kKnownBuilds[] = {
    {"7749D96CE04D7383352A36327EA3910A076704A8E0273206D938574658EE717B",
     "Ubisoft Connect build (original)"},
    {"577FDAE3EBAA4295D8AD865762641AA94374BCABB01022A6DF1D4E6994A3E277",
     "Ubisoft Connect build + Unlocked FPS 1.0 (Kamzik123)"},
};

// Hook sites and code the fix depends on. Patterns are hex bytes; "??" matches any byte.
constexpr std::uint32_t kPollMouseVa = 0x0148A930;       // bool __thiscall Pad::PollMouse(DIMOUSESTATE2*)
constexpr std::size_t kPollMousePatchLength = 5;          // push ebp / mov ebp,esp / push esi / push edi
constexpr std::uint32_t kMouseToStickVa = 0x0148BF13;     // mouse branch of Pad::UpdateAxes (0x0148BC20)

// Polar (third-person orbit) camera: FUN_00757580 calls the stick response curve
// FUN_00756370 (x,y -> direction * |v|^2) with bl = 1 when it takes its mouse branch.
constexpr std::uint32_t kPolarCurveVa = 0x00756370;
constexpr std::size_t kPolarCurvePatchLength = 6;         // push ebp / mov ebp,esp / sub esp,0Ch
constexpr std::uint32_t kPolarStickX = 0x1AC;             // camera control: stick x after FUN_00755640
constexpr std::uint32_t kPolarStickY = 0x1B0;
constexpr std::uint32_t kPolarYawSpeed = 0x70;
constexpr std::uint32_t kPolarPitchSpeed = 0xC4;
constexpr std::uint32_t kPolarBothAxes = 0xD5;            // 0: only the dominant axis is kept
constexpr std::uint32_t kPolarIgnoresLock = 0xD4;         // 0: the input lock below applies
constexpr std::uint32_t kCameraInputLockedVa = 0x007F27B0;  // true: FUN_00755640 skips the stick
// The polar camera's yaw is in radians (the angle normalizer FUN_014E9230 wraps at pi); its
// mouse branch turns by yaw speed * look sensitivity * 1e-5 * stick x.
constexpr float kPolarMouseYawConstant = 1e-5f;
constexpr float kPolarMousePitchConstant = 3e-5f;
constexpr std::uint32_t kOptionsPointerVa = 0x0220E9E8;   // game options object
constexpr std::uint32_t kOptionsSensitivityX = 0x98;
constexpr std::uint32_t kOptionsSensitivityY = 0x90;
constexpr std::uint32_t kOptionsInvertX = 0xBE;
constexpr std::uint32_t kOptionsInvertY = 0xBF;

// Offsets in the keyboard/mouse pad object (0x22030 bytes, vtable 0x01E33B20).
constexpr std::uint32_t kPadVtable = 0x01E33B20;
constexpr std::uint32_t kPadMouseDevice = 0x590;
constexpr std::uint32_t kPadMouseState = 0x21DE8;        // DIMOUSESTATE2 lX, lY, lZ, rgbButtons[8]
constexpr std::uint32_t kPadRightStick = 0x1A0;          // float x, y

// Native per-frame clamp applied to the mouse before it becomes the right stick.
constexpr long kNativeClampX = 150;
constexpr long kNativeClampY = 100;

struct CodeSignature {
    const char* name;
    std::uint32_t va;
    const char* pattern;
};

constexpr CodeSignature kCodeSignatures[] = {
    {"mouse poll (GetDeviceState wrapper)", kPollMouseVa,
     "55 8B EC 56 57 8B 7D 08 8B F1 8B 86 90 05 00 00 8B 08 8B 51 24 57 6A 14 50 FF D2"},
    {"mouse poll call in pad update", 0x0148C49F,
     "50 8B CB E8 89 E4 FF FF 84 C0"},
    {"mouse to right stick (clamp 150/100)", kMouseToStickVa,
     "39 0A 75 05 39 4A 04 74 07 C6 05 D9 DB 23 02 01 F3 0F 2A 02 F2 0F 10 15 00 3C E3 01 "
     "0F 5A C8 66 0F 2F D1 72 08 F3 0F 10 05 F8 3B E3 01 0F 5A C8 66 0F 2F 0D F0 3B E3 01 "
     "72 08 F3 0F 10 05 E8 3B E3 01 F2 0F 10 15 E0 3B E3 01 F3 0F 11 87 A0 01 00 00 F3 0F "
     "2A 42 04 0F 5A C8 66 0F 2F D1 72 08 F3 0F 10 05 AC DB DB 01 0F 5A C8 66 0F 2F 0D F0 "
     "D7 CB 01 72 08 F3 0F 10 05 D8 BA CA 01 0F 5A C0 F2 0F 59 05 80 BF CA 01 66 0F 5A C0 "
     "F3 0F 11 87 A4 01 00 00 5F 5E 5B 5D"},
    {"keyboard/mouse pad constructor", 0x0148B850,
     "55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 51 53 56 8B F1 "
     "57 89 75 F0 E8 ?? ?? ?? ?? 8B 45 0C 8B 55 08 0F 57 C0 33 DB 89 86 94 05 00 00 89 5D FC "
     "C7 06 20 3B E3 01 89 96 90 05 00 00"},
    {"mouse device creation (GUID_SysMouse)", 0x0148D754,
     "53 8D 4D EC 51 68 9C 16 E9 01 50 FF D2 85 C0"},
    {"polar camera stick curve (pow 2)", kPolarCurveVa,
     "55 8B EC 83 EC 0C 56 8B F1 D9 86 AC 01 00 00 D9 86 B0 01 00 00 E8 40 45 F8 00 D9 5D F8 "
     "D9 45 F8 D9 5D FC D9 86 B0 01 00 00 D9 86 AC 01 00 00 DC C8 D9 C1 DE CA DE C1 D9 5D F8 "
     "D9 45 F8 E8 0E 3C F8 00 D9 5D F8 D9 45 F8 DD 05 A0 9F CA 01 E8 AD 4B F8 00 D9 5D F8 D9 "
     "45 F8 D9 5D F8 D9 45 FC E8 1C 3E F8 00 D9 5D F4 D9 45 F4 D8 4D F8 D9 9E AC 01 00 00 D9 "
     "45 FC E8 D5 3C F8 00 D9 5D F4 D9 45 F4 D8 4D F8 D9 9E B0 01 00 00 5E 8B E5 5D C3"},
    // bl = 1 on the mouse branch (pad type 0 and [0x0223DBD9] set), 0 on the gamepad branch;
    // both reach the curve call with ecx = camera control.
    {"polar camera mouse branch and curve call", 0x00757752,
     "8B 0D C0 DB 23 02 E8 83 55 D3 00 85 C0 75 32 38 05 D9 DB 23 02 74 2A B3 01 EB 2F"},
    {"polar camera gamepad branch and curve call", 0x00757793,
     "8B CE 32 DB E8 E4 E9 FF FF 8B CE E8 CD EB FF FF"},
    {"options getter", 0x0041EEB0, "A1 E8 E9 20 02 C3"},
    // The hook reads the camera data pointer from this function's frame ([ebp+8]).
    {"polar camera update frame", 0x00757580,
     "55 8B EC 83 EC 38 A1 A0 E9 20 02 53 56 8B F1 8B 48 08 F3 0F 10 41 18 57 8B 7D 08"},
    {"camera input lock check", kCameraInputLockedVa,
     "E8 AB 4B CD FF 85 C0 74 2C 8B 10 8B C8 8B 42 10 56 8B 35 48 33 0B 02 FF D0 66 8B 48 54 66 3B 4E 54"},
    {"camera input lock use", 0x00755648,
     "E8 63 D1 09 00 33 DB 84 C0 74 16 38 9E D4 00 00 00 75 0E 88 9E AA 01 00 00"},
};

constexpr CodeSignature kDataSignatures[] = {
    {"GUID_SysMouse", 0x01E9169C, "60 2B 1D 6F A0 D5 CF 11 BF C7 44 45 53 54 00 00"},
    {"c_dfDIMouse2 header", 0x01E906CC, "18 00 00 00 10 00 00 00 02 00 00 00 14 00 00 00 0B 00 00 00"},
};

struct ValueCheck {
    const char* name;
    std::uint32_t va;
    bool isDouble;
    double expected;
};

constexpr ValueCheck kValueChecks[] = {
    {"mouse X clamp low (double)", 0x01E33C00, true, -150.0},
    {"mouse X clamp low (float)", 0x01E33BF8, false, -150.0},
    {"mouse X clamp high (double)", 0x01E33BF0, true, 150.0},
    {"mouse X clamp high (float)", 0x01E33BE8, false, 150.0},
    {"mouse Y clamp low (double)", 0x01E33BE0, true, -100.0},
    {"mouse Y clamp low (float)", 0x01DBDBAC, false, -100.0},
    {"mouse Y clamp high (double)", 0x01CBD7F0, true, 100.0},
    {"mouse Y clamp high (float)", 0x01CABAD8, false, 100.0},
    {"mouse Y sign", 0x01CABF80, true, -1.0},
    {"polar camera stick curve exponent", 0x01CA9FA0, true, 2.0},
    {"polar camera mouse yaw constant", 0x01CCBDB0, true, static_cast<double>(1e-5f)},
    {"polar camera mouse pitch constant", 0x01CCBDA8, true, static_cast<double>(3e-5f)},
};

// Parses "AA ?? BB" into bytes and a mask. Returns the byte count, 0 on a malformed pattern.
inline std::size_t ParsePattern(const char* pattern, std::uint8_t* bytes, bool* mask,
                                std::size_t capacity) {
    std::size_t count = 0;
    for (const char* p = pattern; *p != '\0';) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        if (count == capacity || p[1] == '\0') return 0;
        if (p[0] == '?' && p[1] == '?') {
            bytes[count] = 0;
            mask[count] = false;
        } else {
            auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int high = nibble(p[0]);
            const int low = nibble(p[1]);
            if (high < 0 || low < 0) return 0;
            bytes[count] = static_cast<std::uint8_t>(high * 16 + low);
            mask[count] = true;
        }
        ++count;
        p += 2;
    }
    return count;
}

// Reader: bool(std::uint32_t va, void* destination, std::size_t length).
template <typename Reader>
bool PatternMatches(Reader& read, const CodeSignature& signature, char* failure,
                    std::size_t failureSize) {
    std::uint8_t expected[256]{};
    bool mask[256]{};
    std::uint8_t actual[256]{};
    const std::size_t length = ParsePattern(signature.pattern, expected, mask, sizeof(expected));
    if (length == 0) {
        std::snprintf(failure, failureSize, "malformed pattern: %s", signature.name);
        return false;
    }
    if (!read(signature.va, actual, length)) {
        std::snprintf(failure, failureSize, "unreadable: %s at %08X", signature.name,
                      static_cast<unsigned>(signature.va));
        return false;
    }
    for (std::size_t index = 0; index < length; ++index) {
        if (mask[index] && actual[index] != expected[index]) {
            std::snprintf(failure, failureSize,
                          "signature mismatch: %s at %08X (+%u: %02X, expected %02X)",
                          signature.name, static_cast<unsigned>(signature.va),
                          static_cast<unsigned>(index), actual[index], expected[index]);
            return false;
        }
    }
    return true;
}

// Checks every signature and value the fix depends on. On failure, writes the first problem
// into `failure` and returns false.
template <typename Reader>
bool ValidateLayout(Reader& read, char* failure, std::size_t failureSize) {
    for (const CodeSignature& signature : kCodeSignatures) {
        if (!PatternMatches(read, signature, failure, failureSize)) return false;
    }
    for (const CodeSignature& signature : kDataSignatures) {
        if (!PatternMatches(read, signature, failure, failureSize)) return false;
    }
    for (const ValueCheck& check : kValueChecks) {
        double actual = 0.0;
        bool readOk = false;
        if (check.isDouble) {
            readOk = read(check.va, &actual, sizeof(actual));
        } else {
            float value = 0.0f;
            readOk = read(check.va, &value, sizeof(value));
            actual = value;
        }
        if (!readOk || actual != check.expected) {
            std::snprintf(failure, failureSize, "value mismatch: %s at %08X (%g, expected %g)",
                          check.name, static_cast<unsigned>(check.va), readOk ? actual : 0.0,
                          check.expected);
            return false;
        }
    }
    return true;
}

inline const char* KnownBuildName(const char* sha256) {
    for (const KnownBuild& build : kKnownBuilds) {
        if (std::strcmp(build.sha256, sha256) == 0) return build.name;
    }
    return nullptr;
}

}  // namespace ac2
