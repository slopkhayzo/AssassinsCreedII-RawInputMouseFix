// Assassin's Creed II raw mouse input fix (ASI plugin, x86).
//
// The game turns mouse counts per frame into a "right stick" (clamped to +-150/+-100) and
// the third-person polar camera squares the stick magnitude (FUN_00756370), so the turn grows
// with the square of the counts per frame: accelerated, and inversely proportional to the
// frame rate. The camera's mouse branch has no frame-time factor of its own.
//
// The fix accumulates raw DirectInput counts at the pad's mouse poll (0x0148A930) and, when
// the polar camera takes its mouse branch, replaces the squared stick with that frame's counts
// times the configured sensitivity (LinearCamera=1). Everything after that is the game's own
// code: look-sensitivity sliders, invert options, pitch limits. Gamepad and keyboard-camera
// input still use the original curve, and a camera whose input the game has locked stays locked.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ac2_counts.h"
#include "ac2_layout.h"

#ifndef AC2_RAW_MOUSE_VERSION
#define AC2_RAW_MOUSE_VERSION "1.0.0"
#endif

// Development tools (right-stick reader trace): build-msvc.cmd dev defines AC2_DEV_TOOLS=1.
#ifndef AC2_DEV_TOOLS
#define AC2_DEV_TOOLS 0
#endif

namespace {

constexpr char kLogFilename[] = "ac2-raw-mouse-fix.log";
constexpr char kConfigFilename[] = "ac2-raw-mouse-fix.ini";
constexpr char kFallbackDirectory[] = "AC2RawMouseFix";
constexpr LONGLONG kLogRotateBytes = 4 * 1024 * 1024;
constexpr DWORD kReportIntervalMs = 5000;
// Counts that waited longer than this were collected while the camera was not running
// (menus, cutscenes, other camera modes) and are dropped instead of replayed.
constexpr DWORD kStaleCountsMs = 250;
constexpr float kDefaultSensitivity = 50.0f;
constexpr float kMinSensitivity = 0.1f;
constexpr float kMaxSensitivity = 1000.0f;
constexpr float kMinMultiplier = 0.05f;
constexpr float kMaxMultiplier = 20.0f;
constexpr float kHotkeyMultiplierStep = 1.1f;
constexpr double kPi = 3.14159265358979323846;

struct MouseState {  // DIMOUSESTATE2
    LONG x;
    LONG y;
    LONG z;
    BYTE buttons[8];
};

using PollMouseFn = bool(__thiscall*)(void* pad, MouseState* state);
using CameraInputLockedFn = bool(__fastcall*)(void* camera);

HMODULE g_self = nullptr;
HANDLE g_instanceMutex = nullptr;
volatile LONG g_initializationStarted = 0;
PollMouseFn g_originalPollMouse = nullptr;
void* g_originalCurve = nullptr;  // trampoline of FUN_00756370 (jumped to from the stub)

bool g_enabled = true;
bool g_diagnostics = false;
bool g_hotkeys = true;
volatile LONG g_linearCamera = 1;
std::atomic<float> g_sensitivityX{kDefaultSensitivity};
std::atomic<float> g_sensitivityY{kDefaultSensitivity};
std::atomic<float> g_sensitivityMultiplier{1.0f};

// Raw counts not yet consumed by the camera (written by the pad thread, taken by the camera
// update).
ac2::FrameCounts g_counts;
long long g_staleTicks = 0;

// Polar camera statistics.
std::atomic<std::uint32_t> g_curveMouseCalls{0};
std::atomic<std::uint32_t> g_curvePadCalls{0};
std::atomic<std::uint32_t> g_curveLinear{0};
std::atomic<std::uint32_t> g_staleDrops{0};
std::atomic<std::uint32_t> g_singleAxisFrames{0};
std::atomic<std::uint32_t> g_nonIntegerSticks{0};
std::atomic<std::uint32_t> g_lockedFrames{0};
std::atomic<std::uint32_t> g_blendUpdates{0};      // another camera state in the same frame
std::atomic<std::uint32_t> g_repeatUpdates{0};     // same state again before the next poll
std::atomic<std::uint32_t> g_zeroStickFrames{0};   // counts arrived but the native stick is 0
std::atomic<long long> g_zeroStickCounts{0};
std::atomic<long long> g_appliedX{0};
std::atomic<long long> g_appliedY{0};
std::atomic<void*> g_camera{nullptr};
std::atomic<std::uint32_t> g_cameraSwitches{0};
std::atomic<float> g_cameraYawSpeed{0.0f};
std::atomic<float> g_cameraPitchSpeed{0.0f};
std::atomic<std::uint32_t> g_cameraBothAxes{0};
bool g_traceStickReaders = false;
DWORD g_traceSeconds = 180;
std::uint32_t g_traceExtraAddress = 0;

// Written by the game's input thread only, read and reset by the report thread.
std::atomic<std::uint32_t> g_polls{0};
std::atomic<std::uint32_t> g_failedPolls{0};  // GetDeviceState failed (e.g. not foreground)
std::atomic<std::uint32_t> g_movingPolls{0};
std::atomic<long long> g_absX{0};
std::atomic<long long> g_absY{0};
std::atomic<long> g_maxX{0};
std::atomic<long> g_maxY{0};
std::atomic<std::uint32_t> g_clampHitsX{0};
std::atomic<std::uint32_t> g_clampHitsY{0};
std::atomic<long long> g_clampLostX{0};
std::atomic<long long> g_clampLostY{0};
std::atomic<long long> g_intervalSumTicks{0};
std::atomic<long long> g_intervalMaxTicks{0};
std::atomic<long long> g_intervalMinTicks{LLONG_MAX};
std::atomic<void*> g_pad{nullptr};
long long g_lastPollTicks = 0;
LARGE_INTEGER g_qpcFrequency{};

// ---------------------------------------------------------------------------------------
// Files and logging

bool BuildSiblingPath(const char* filename, char (&path)[MAX_PATH]) {
    const DWORD length = GetModuleFileNameA(g_self, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) return false;
    slash[1] = '\0';
    return strcat_s(path, filename) == 0;
}

bool BuildFallbackPath(const char* filename, char (&path)[MAX_PATH]) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH - 32) return false;
    strcat_s(directory, "\\");
    strcat_s(directory, kFallbackDirectory);
    if (!CreateDirectoryA(directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    strcpy_s(path, directory);
    strcat_s(path, "\\");
    return strcat_s(path, filename) == 0;
}

void RotateLogIfLarge(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) return;
    const LONGLONG size = (static_cast<LONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    if (size < kLogRotateBytes) return;
    char old[MAX_PATH]{};
    strcpy_s(old, path);
    strcat_s(old, ".old");
    MoveFileExA(path, old, MOVEFILE_REPLACE_EXISTING);
}

bool OpenLogFile(FILE*& file) {
    static bool rotated = false;
    char path[MAX_PATH]{};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool built = attempt == 0 ? BuildSiblingPath(kLogFilename, path)
                                        : BuildFallbackPath(kLogFilename, path);
        if (!built) continue;
        if (!rotated) RotateLogIfLarge(path);
        if (fopen_s(&file, path, "a") == 0 && file != nullptr) {
            rotated = true;
            return true;
        }
    }
    return false;
}

void Log(const char* format, ...) {
    FILE* file = nullptr;
    if (!OpenLogFile(file)) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fprintf(file, "%02u:%02u:%02u.%03u ", time.wHour, time.wMinute, time.wSecond,
                 time.wMilliseconds);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(file, format, arguments);
    va_end(arguments);
    std::fputc('\n', file);
    std::fclose(file);
}

// Reads a positive number, limited to [low, high]. A missing or unreadable value gives fallback.
float ReadPositiveFloat(const char* path, const char* key, float fallback, float low, float high) {
    char text[32]{};
    GetPrivateProfileStringA("Sensitivity", key, "", text, sizeof(text), path);
    if (text[0] == '\0') return fallback;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    while (end != nullptr && (*end == ' ' || *end == '\t')) ++end;
    if (end == text || end == nullptr || *end != '\0' || !(value > 0.0)) {
        Log("config: %s=%s is not a positive number; using %.2f", key, text, fallback);
        return fallback;
    }
    const float limited = std::clamp(static_cast<float>(value), low, high);
    if (limited != static_cast<float>(value)) {
        Log("config: %s=%s is outside %.2f..%.2f; using %.2f", key, text, low, high, limited);
    }
    return limited;
}

void LoadConfig() {
    char path[MAX_PATH]{};
    if (!BuildSiblingPath(kConfigFilename, path) ||
        GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        Log("config: %s not found beside the plugin; using defaults", kConfigFilename);
        return;
    }
    g_enabled = GetPrivateProfileIntA("Fix", "Enabled", 1, path) != 0;
    g_diagnostics = GetPrivateProfileIntA("Fix", "Diagnostics", 0, path) != 0;
    g_hotkeys = GetPrivateProfileIntA("Fix", "Hotkeys", 1, path) != 0;
    InterlockedExchange(&g_linearCamera, GetPrivateProfileIntA("Fix", "LinearCamera", 1, path) != 0 ? 1 : 0);
    g_sensitivityX.store(ReadPositiveFloat(path, "SensitivityX", kDefaultSensitivity, kMinSensitivity, kMaxSensitivity));
    g_sensitivityY.store(ReadPositiveFloat(path, "SensitivityY", kDefaultSensitivity, kMinSensitivity, kMaxSensitivity));
    g_sensitivityMultiplier.store(ReadPositiveFloat(path, "SensitivityMultiplier", 1.0f, kMinMultiplier, kMaxMultiplier));
    Log("config: Enabled=%d LinearCamera=%ld SensitivityX=%.2f SensitivityY=%.2f "
        "SensitivityMultiplier=%.2f Hotkeys=%d Diagnostics=%d",
        g_enabled ? 1 : 0, g_linearCamera, g_sensitivityX.load(), g_sensitivityY.load(),
        g_sensitivityMultiplier.load(), g_hotkeys ? 1 : 0, g_diagnostics ? 1 : 0);
#if AC2_DEV_TOOLS
    char extra[32]{};
    g_traceStickReaders = GetPrivateProfileIntA("Trace", "StickReaders", 0, path) != 0;
    g_traceSeconds = GetPrivateProfileIntA("Trace", "Seconds", 180, path);
    GetPrivateProfileStringA("Trace", "ExtraAddress", "", extra, sizeof(extra), path);
    g_traceExtraAddress = static_cast<std::uint32_t>(std::strtoul(extra, nullptr, 16));
    if (g_traceStickReaders) {
        Log("config: Trace StickReaders=1 Seconds=%lu ExtraAddress=%08X", g_traceSeconds,
            static_cast<unsigned>(g_traceExtraAddress));
    }
#endif
}

// ---------------------------------------------------------------------------------------
// Executable identification and layout validation

bool HostIsGame() {
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return false;
    const char* name = std::strrchr(path, '\\');
    name = name != nullptr ? name + 1 : path;
    return _stricmp(name, ac2::kHostExecutable) == 0;
}

bool ReadOwnMemory(std::uint32_t va, void* destination, std::size_t length) {
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(va), destination,
                             length, &bytesRead) != FALSE &&
           bytesRead == length;
}

bool InspectImage(char* failure, std::size_t failureSize) {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    if (base != ac2::kImageBase) {
        std::snprintf(failure, failureSize, "image base %08X, expected %08X",
                      static_cast<unsigned>(base), static_cast<unsigned>(ac2::kImageBase));
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!ReadOwnMemory(ac2::kImageBase, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !ReadOwnMemory(ac2::kImageBase + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        std::snprintf(failure, failureSize, "not an x86 PE image");
        return false;
    }
    if (nt.OptionalHeader.SizeOfImage != ac2::kSizeOfImage) {
        std::snprintf(failure, failureSize, "image size %08lX, expected %08X",
                      nt.OptionalHeader.SizeOfImage, static_cast<unsigned>(ac2::kSizeOfImage));
        return false;
    }
    Log("executable: timestamp=%08lX image_size=%08lX (%s)", nt.FileHeader.TimeDateStamp,
        nt.OptionalHeader.SizeOfImage,
        nt.FileHeader.TimeDateStamp == ac2::kTimeDateStamp ? "known Ubisoft Connect build"
                                                           : "unknown timestamp, signatures decide");
    return true;
}

bool HashFileSha256(const char* path, char (&hex)[65]) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool ok = false;
    std::uint8_t digest[32]{};
    static std::uint8_t buffer[1 << 20];
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            ok = true;
            for (;;) {
                DWORD read = 0;
                if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
                    ok = false;
                    break;
                }
                if (read == 0) break;
                if (BCryptHashData(hash, buffer, read, 0) != 0) {
                    ok = false;
                    break;
                }
            }
            ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
        }
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash != nullptr) BCryptDestroyHash(hash);
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return false;
    for (int index = 0; index < 32; ++index) {
        std::snprintf(hex + index * 2, 3, "%02X", digest[index]);
    }
    return true;
}

void LogBuildIdentity() {
    char path[MAX_PATH]{};
    char hex[65]{};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0 || !HashFileSha256(path, hex)) {
        Log("executable SHA-256: unavailable");
        return;
    }
    const char* name = ac2::KnownBuildName(hex);
    Log("executable SHA-256: %s (%s)", hex, name != nullptr ? name : "unknown build");
}

bool AcquireInstanceGuard() {
    char name[64]{};
    std::snprintf(name, sizeof(name), "Local\\AC2RawMouseFix-%lu", GetCurrentProcessId());
    HANDLE mutex = CreateMutexA(nullptr, FALSE, name);
    if (mutex == nullptr) {
        Log("initialization failed: instance guard error=%lu", GetLastError());
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        Log("initialization refused: another copy of the fix is already loaded");
        return false;
    }
    g_instanceMutex = mutex;
    return true;
}

// ---------------------------------------------------------------------------------------
// Passive mouse observation

void StoreMax(std::atomic<long>& slot, long value) {
    if (value > slot.load(std::memory_order_relaxed)) slot.store(value, std::memory_order_relaxed);
}

void ObservePoll(void* pad, const MouseState& state) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (g_lastPollTicks != 0) {
        const long long interval = now.QuadPart - g_lastPollTicks;
        g_intervalSumTicks.fetch_add(interval, std::memory_order_relaxed);
        if (interval > g_intervalMaxTicks.load(std::memory_order_relaxed)) {
            g_intervalMaxTicks.store(interval, std::memory_order_relaxed);
        }
        if (interval < g_intervalMinTicks.load(std::memory_order_relaxed)) {
            g_intervalMinTicks.store(interval, std::memory_order_relaxed);
        }
    }
    g_lastPollTicks = now.QuadPart;
    g_pad.store(pad, std::memory_order_relaxed);
    g_polls.fetch_add(1, std::memory_order_relaxed);
    g_counts.AddPoll(state.x, state.y, now.QuadPart);
    if (state.x == 0 && state.y == 0) return;

    const long ax = std::labs(state.x);
    const long ay = std::labs(state.y);
    g_movingPolls.fetch_add(1, std::memory_order_relaxed);
    g_absX.fetch_add(ax, std::memory_order_relaxed);
    g_absY.fetch_add(ay, std::memory_order_relaxed);
    StoreMax(g_maxX, ax);
    StoreMax(g_maxY, ay);
    if (ax > ac2::kNativeClampX) {
        g_clampHitsX.fetch_add(1, std::memory_order_relaxed);
        g_clampLostX.fetch_add(ax - ac2::kNativeClampX, std::memory_order_relaxed);
    }
    if (ay > ac2::kNativeClampY) {
        g_clampHitsY.fetch_add(1, std::memory_order_relaxed);
        g_clampLostY.fetch_add(ay - ac2::kNativeClampY, std::memory_order_relaxed);
    }
}

bool __fastcall HookPollMouse(void* pad, void* /*edx*/, MouseState* state) {
    const bool ok = g_originalPollMouse(pad, state);
    if (ok && state != nullptr) ObservePoll(pad, *state);
    else g_failedPolls.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

// ---------------------------------------------------------------------------------------
// Polar camera: linear raw counts instead of the squared stick

void DiscardPendingCounts() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    g_counts.Discard(now.QuadPart, g_staleTicks);
}

bool IsWhole(float value) {
    return value == static_cast<float>(static_cast<long>(value));
}

// Called from the stub at the start of FUN_00756370. mouseBranch is the caller's bl: 1 when
// FUN_00757580 took its mouse branch. cameraData is that function's camera data argument, which
// holds the yaw and pitch being turned. Returns true when the stick was replaced (the original
// curve is then skipped).
bool __stdcall PolarCurve(std::uint8_t* camera, std::uint32_t mouseBranch, const void* cameraData) {
    if ((mouseBranch & 0xFF) == 0) {
        g_curvePadCalls.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_curveMouseCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_camera.exchange(camera, std::memory_order_relaxed) != camera) {
        g_cameraSwitches.fetch_add(1, std::memory_order_relaxed);
    }
    // The report thread reads these copies: the camera object itself may be gone by then.
    g_cameraYawSpeed.store(*reinterpret_cast<const float*>(camera + ac2::kPolarYawSpeed), std::memory_order_relaxed);
    g_cameraPitchSpeed.store(*reinterpret_cast<const float*>(camera + ac2::kPolarPitchSpeed), std::memory_order_relaxed);
    g_cameraBothAxes.store(camera[ac2::kPolarBothAxes], std::memory_order_relaxed);

    auto& stickX = *reinterpret_cast<float*>(camera + ac2::kPolarStickX);
    auto& stickY = *reinterpret_cast<float*>(camera + ac2::kPolarStickY);
    if (!IsWhole(stickX) || !IsWhole(stickY)) g_nonIntegerSticks.fetch_add(1, std::memory_order_relaxed);
    if (camera[ac2::kPolarBothAxes] == 0) g_singleAxisFrames.fetch_add(1, std::memory_order_relaxed);

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    long long countsX = 0;
    long long countsY = 0;
    bool dropped = false;
    const auto result = g_counts.Take(cameraData, now.QuadPart, g_staleTicks, countsX, countsY, dropped);
    if (dropped) g_staleDrops.fetch_add(1, std::memory_order_relaxed);
    if (result == ac2::FrameCounts::Result::kOtherState) {
        g_blendUpdates.fetch_add(1, std::memory_order_relaxed);
    } else if (result == ac2::FrameCounts::Result::kAlreadyServed) {
        g_repeatUpdates.fetch_add(1, std::memory_order_relaxed);
    }
    if (InterlockedCompareExchange(&g_linearCamera, 0, 0) == 0) return false;

    // While the game locks camera input, FUN_00755640 does not read the stick at all, so the
    // mouse must not turn the camera either. The counts taken above are forgotten.
    const auto inputLocked = reinterpret_cast<CameraInputLockedFn>(
        static_cast<std::uintptr_t>(ac2::kCameraInputLockedVa));
    if (camera[ac2::kPolarIgnoresLock] == 0 && inputLocked(camera)) {
        g_lockedFrames.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (stickX == 0.0f && stickY == 0.0f && (countsX != 0 || countsY != 0)) {
        g_zeroStickFrames.fetch_add(1, std::memory_order_relaxed);
        g_zeroStickCounts.fetch_add(std::llabs(countsX) + std::llabs(countsY), std::memory_order_relaxed);
    }

    // Same signs as the native path: stick x = -lX, stick y = +lY, each negated again by the
    // third-person invert options. Both axes are kept even where the native code keeps only
    // the dominant one, so diagonal mouse movement is not snapped to an axis.
    const auto* options = *reinterpret_cast<std::uint8_t* const*>(
        static_cast<std::uintptr_t>(ac2::kOptionsPointerVa));
    const float multiplier = g_sensitivityMultiplier.load(std::memory_order_relaxed);
    float x = -static_cast<float>(countsX) * g_sensitivityX.load(std::memory_order_relaxed) * multiplier;
    float y = static_cast<float>(countsY) * g_sensitivityY.load(std::memory_order_relaxed) * multiplier;
    if (options != nullptr && options[ac2::kOptionsInvertX] != 0) x = -x;
    if (options != nullptr && options[ac2::kOptionsInvertY] != 0) y = -y;
    stickX = x;
    stickY = y;
    g_appliedX.fetch_add(std::llabs(countsX), std::memory_order_relaxed);
    g_appliedY.fetch_add(std::llabs(countsY), std::memory_order_relaxed);
    g_curveLinear.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Entered by a jump from FUN_00756370: ecx = camera control, ebx = the caller's bl flag, ebp =
// the caller's frame, so [ebp+8] is its camera data argument.
__declspec(naked) void HookPolarCurve() {
    __asm {
        push ecx
        movzx eax, bl
        push dword ptr [ebp + 8]
        push eax
        push ecx
        call PolarCurve
        pop ecx
        test al, al
        jnz replaced
        jmp dword ptr [g_originalCurve]
    replaced:
        ret
    }
}

void LogSensitivity(const char* source) {
    const float x = g_sensitivityX.load();
    const float y = g_sensitivityY.load();
    const float multiplier = g_sensitivityMultiplier.load();
    Log("%s: SensitivityMultiplier=%.2f (SensitivityX=%.2f SensitivityY=%.2f, effective x=%.2f y=%.2f)",
        source, multiplier, x, y, x * multiplier, y * multiplier);
}

// Turn per mouse count of the camera seen last, from the copies PolarCurve keeps.
void LogCameraParameters() {
    const auto* options = *reinterpret_cast<std::uint8_t* const*>(
        static_cast<std::uintptr_t>(ac2::kOptionsPointerVa));
    if (g_camera.load(std::memory_order_relaxed) == nullptr || options == nullptr) return;
    const float yawSpeed = g_cameraYawSpeed.load();
    const float pitchSpeed = g_cameraPitchSpeed.load();
    const float sliderX = *reinterpret_cast<const float*>(options + ac2::kOptionsSensitivityX);
    const float sliderY = *reinterpret_cast<const float*>(options + ac2::kOptionsSensitivityY);
    const float multiplier = g_sensitivityMultiplier.load();
    const double radiansPerCount = std::fabs(static_cast<double>(yawSpeed) * sliderX *
                                             ac2::kPolarMouseYawConstant * g_sensitivityX.load() * multiplier);
    const double pitchPerCount = std::fabs(static_cast<double>(pitchSpeed) * sliderY *
                                           ac2::kPolarMousePitchConstant * g_sensitivityY.load() * multiplier);
    Log("polar camera: yaw speed=%g pitch speed=%g game sliders x=%g y=%g invert x=%u y=%u both axes=%u -> "
        "%.4f deg of yaw per count (%.0f counts per 360), %.6f of the pitch range per count",
        yawSpeed, pitchSpeed, sliderX, sliderY, options[ac2::kOptionsInvertX],
        options[ac2::kOptionsInvertY], g_cameraBothAxes.load(), radiansPerCount * 180.0 / kPi,
        radiansPerCount > 0.0 ? 2.0 * kPi / radiansPerCount : 0.0, pitchPerCount);
}

// ---------------------------------------------------------------------------------------
// Hooking

constexpr std::size_t kMaxThreads = 512;

struct SuspendedThreads {
    HANDLE handles[kMaxThreads]{};
    std::size_t count = 0;
};

void SuspendOtherThreads(SuspendedThreads& threads) {
    const DWORD process = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        if (threads.count == kMaxThreads) break;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                                   FALSE, entry.th32ThreadID);
        if (thread == nullptr) continue;
        if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
            CloseHandle(thread);
            continue;
        }
        threads.handles[threads.count++] = thread;
    }
    CloseHandle(snapshot);
}

void ResumeThreads(SuspendedThreads& threads) {
    for (std::size_t index = 0; index < threads.count; ++index) {
        ResumeThread(threads.handles[index]);
        CloseHandle(threads.handles[index]);
    }
    threads.count = 0;
}

// A thread suspended inside the bytes being replaced is moved to the same instruction in the
// trampoline, which holds an exact copy of those whole instructions.
void MoveThreadsOutOfPatch(SuspendedThreads& threads, std::uintptr_t target, std::size_t length,
                           std::uintptr_t trampoline) {
    for (std::size_t index = 0; index < threads.count; ++index) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(threads.handles[index], &context)) continue;
        if (context.Eip >= target && context.Eip < target + length) {
            context.Eip = static_cast<DWORD>(trampoline + (context.Eip - target));
            SetThreadContext(threads.handles[index], &context);
            Log("hook: moved a thread out of the patched bytes");
        }
    }
}

// Detours `length` bytes of whole, position-independent instructions at `va` to `replacement`.
template <typename Function>
bool InstallDetour(std::uint32_t va, std::size_t length, const void* replacement,
                   Function& original, const char* name) {
    auto* target = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(va));
    auto* trampoline = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        Log("hook failed: %s trampoline allocation error=%lu", name, GetLastError());
        return false;
    }
    std::memcpy(trampoline, target, length);
    trampoline[length] = 0xE9;
    *reinterpret_cast<std::int32_t*>(trampoline + length + 1) =
        static_cast<std::int32_t>((target + length) - (trampoline + length + 5));
    FlushInstructionCache(GetCurrentProcess(), trampoline, length + 5);

    SuspendedThreads threads;
    SuspendOtherThreads(threads);
    DWORD oldProtection = 0;
    bool ok = VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtection) != FALSE;
    if (ok) {
        MoveThreadsOutOfPatch(threads, va, length, reinterpret_cast<std::uintptr_t>(trampoline));
        original = reinterpret_cast<Function>(trampoline);
        target[0] = 0xE9;
        *reinterpret_cast<std::int32_t*>(target + 1) = static_cast<std::int32_t>(
            reinterpret_cast<const std::uint8_t*>(replacement) - (target + 5));
        for (std::size_t index = 5; index < length; ++index) target[index] = 0x90;
        FlushInstructionCache(GetCurrentProcess(), target, length);
        DWORD ignored = 0;
        VirtualProtect(target, length, oldProtection, &ignored);
    }
    ResumeThreads(threads);
    if (!ok) {
        Log("hook failed: %s VirtualProtect error=%lu", name, GetLastError());
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    Log("hook installed: %s at %08X", name, static_cast<unsigned>(va));
    return true;
}

bool GameHasForeground() {
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == GetCurrentProcessId();
}

#if AC2_DEV_TOOLS
// ---------------------------------------------------------------------------------------
// Right-stick reader trace (development only)
//
// Hardware data breakpoints (DR0-DR3, read/write, 4 bytes) on the stick fields. Slot 0 watches
// the keyboard/mouse pad's stick X. The composite pad update copies it with
// "movq xmm0,[eax+0x10]" at 0x0148B084; the trap after it (EIP 0x0148B089) has the composite
// pad in ESI, and slots 1/2 then watch the composite's stick X/Y, which the gameplay code
// reads. Slot 3 watches [Trace] ExtraAddress if set. Every distinct reading instruction is
// logged once per arming with its registers and the return addresses found on its stack.

constexpr std::uint32_t kCompositeCopyVa = 0x0148B084;
constexpr std::uint32_t kCompositeCopyTrapEip = 0x0148B089;
constexpr ac2::CodeSignature kCompositeCopySignature = {
    "composite pad stick copy", kCompositeCopyVa, "F3 0F 7E 40 10 66 0F D6 86 A0 01 00 00"};
constexpr std::size_t kTraceEntries = 512;
constexpr std::size_t kTraceStack = 6;
const char* const kTraceSlotNames[4] = {"km pad stick X", "composite stick X",
                                        "composite stick Y", "extra address"};

struct TraceEntry {
    volatile LONG state;  // 0 free, 1 being filled, 2 ready
    std::uint32_t slot;
    std::uint32_t eip;
    volatile LONG hits;
    LONG hitsAtArm;
    LONG logged;
    std::uint32_t regs[8];  // eax ecx edx ebx esp ebp esi edi
    std::uint32_t callers[kTraceStack];
};

TraceEntry g_trace[kTraceEntries]{};
std::uint32_t g_watch[4]{};
volatile LONG g_traceArmed = 0;
volatile LONG g_watchGeneration = 0;
volatile LONG g_tableFull = 0;
volatile LONG g_traceExceptions = 0;
volatile std::uint32_t g_compositePad = 0;
std::uint32_t g_textBegin = 0;
std::uint32_t g_textEnd = 0;

bool LooksLikeReturnAddress(std::uint32_t value) {
    if (value < g_textBegin + 8 || value >= g_textEnd) return false;
    const auto* code = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(value));
    return code[-5] == 0xE8 || code[-2] == 0xFF || code[-3] == 0xFF || code[-6] == 0xFF;
}

// Plain C helper so __try can be used (no C++ unwinding in this function).
void ScanCallers(std::uint32_t esp, std::uint32_t* callers) {
    std::size_t found = 0;
    __try {
        const auto* stack = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(esp));
        for (std::size_t index = 0; index < 96 && found < kTraceStack; ++index) {
            if (LooksLikeReturnAddress(stack[index])) callers[found++] = stack[index];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void RecordTraceHit(std::uint32_t slot, const CONTEXT& context) {
    for (std::size_t index = 0; index < kTraceEntries; ++index) {
        TraceEntry& entry = g_trace[index];
        if (entry.state == 2) {
            if (entry.slot == slot && entry.eip == context.Eip) {
                InterlockedIncrement(&entry.hits);
                return;
            }
            continue;
        }
        if (entry.state == 1) continue;
        if (InterlockedCompareExchange(&entry.state, 1, 0) != 0) continue;
        entry.slot = slot;
        entry.eip = context.Eip;
        entry.hits = 1;
        entry.hitsAtArm = 0;
        entry.logged = 0;
        const std::uint32_t regs[8] = {context.Eax, context.Ecx, context.Edx, context.Ebx,
                                       context.Esp, context.Ebp, context.Esi, context.Edi};
        std::memcpy(entry.regs, regs, sizeof(regs));
        ScanCallers(context.Esp, entry.callers);
        InterlockedExchange(&entry.state, 2);
        return;
    }
    InterlockedExchange(&g_tableFull, 1);
}

LONG CALLBACK TraceExceptionHandler(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT& context = *info->ContextRecord;
    const DWORD triggered = context.Dr6 & 0xF;
    if (triggered == 0) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&g_traceExceptions);
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
        if ((triggered & (1u << slot)) != 0 && g_watch[slot] != 0) RecordTraceHit(slot, context);
    }
    if ((triggered & 1u) != 0 && context.Eip == kCompositeCopyTrapEip && g_compositePad == 0) {
        g_compositePad = context.Esi;
        g_watch[1] = context.Esi + ac2::kPadRightStick;
        g_watch[2] = context.Esi + ac2::kPadRightStick + 4;
        InterlockedIncrement(&g_watchGeneration);
    }
    context.Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Writes the current watches (or none) into the debug registers of every other thread.
void ApplyWatches(bool armed) {
    DWORD dr7 = 0;
    if (armed) {
        for (std::uint32_t slot = 0; slot < 4; ++slot) {
            if (g_watch[slot] == 0) continue;
            dr7 |= 1u << (slot * 2);              // local enable
            dr7 |= 3u << (16 + slot * 4);         // break on read or write
            dr7 |= 3u << (18 + slot * 4);         // 4 bytes
        }
    }
    const DWORD process = GetCurrentProcessId();
    const DWORD self = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                                   FALSE, entry.th32ThreadID);
        if (thread == nullptr) continue;
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context)) {
                context.Dr0 = armed ? g_watch[0] : 0;
                context.Dr1 = armed ? g_watch[1] : 0;
                context.Dr2 = armed ? g_watch[2] : 0;
                context.Dr3 = armed ? g_watch[3] : 0;
                context.Dr7 = dr7;
                SetThreadContext(thread, &context);
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
}

void ReportTrace(bool all) {
    for (std::size_t index = 0; index < kTraceEntries; ++index) {
        TraceEntry& entry = g_trace[index];
        if (entry.state != 2) continue;
        const LONG hits = entry.hits - entry.hitsAtArm;
        if (hits <= 0 || (entry.logged != 0 && !all)) continue;
        Log("trace %s eip=%08X hits=%ld eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X "
            "esi=%08X edi=%08X callers %08X %08X %08X %08X %08X %08X",
            kTraceSlotNames[entry.slot], entry.eip, hits, entry.regs[0], entry.regs[1],
            entry.regs[2], entry.regs[3], entry.regs[4], entry.regs[5], entry.regs[6],
            entry.regs[7], entry.callers[0], entry.callers[1], entry.callers[2],
            entry.callers[3], entry.callers[4], entry.callers[5]);
        entry.logged = 1;
    }
    if (InterlockedExchange(&g_tableFull, 0) != 0) Log("trace: table full, some readers not recorded");
    const LONG exceptions = InterlockedExchange(&g_traceExceptions, 0);
    if (exceptions != 0) Log("trace: %ld breakpoint hits since the last report", exceptions);
}

void RearmTrace(unsigned round) {
    for (std::size_t index = 0; index < kTraceEntries; ++index) {
        TraceEntry& entry = g_trace[index];
        if (entry.state != 2) continue;
        entry.hitsAtArm = entry.hits;
        entry.logged = 0;
    }
    InterlockedExchange(&g_traceArmed, 1);
    InterlockedIncrement(&g_watchGeneration);
    Log("trace armed (round %u, %lu s): readers seen from now on are logged", round, g_traceSeconds);
}

bool StartTrace() {
    char failure[256]{};
    auto reader = [](std::uint32_t va, void* destination, std::size_t length) {
        return ReadOwnMemory(va, destination, length);
    };
    if (!ac2::PatternMatches(reader, kCompositeCopySignature, failure, sizeof(failure))) {
        Log("trace disabled: %s", failure);
        return false;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    ReadOwnMemory(ac2::kImageBase, &dos, sizeof(dos));
    ReadOwnMemory(ac2::kImageBase + dos.e_lfanew, &nt, sizeof(nt));
    g_textBegin = ac2::kImageBase + nt.OptionalHeader.BaseOfCode;
    g_textEnd = g_textBegin + nt.OptionalHeader.SizeOfCode;
    g_watch[3] = g_traceExtraAddress;
    if (AddVectoredExceptionHandler(1, TraceExceptionHandler) == nullptr) {
        Log("trace disabled: AddVectoredExceptionHandler failed");
        return false;
    }
    Log("trace ready: waiting for the pad; F11 in game re-arms it (e.g. after entering a new mode)");
    return true;
}

// Called every 50 ms by the worker thread.
void ServiceTrace(DWORD now) {
    static bool started = false;
    static unsigned round = 0;
    static DWORD armedAt = 0;
    static DWORD lastApply = 0;
    static LONG appliedGeneration = -1;
    static bool f11WasDown = false;
    static std::uint32_t reportedComposite = 0;
    if (!started) {
        void* pad = g_pad.load(std::memory_order_relaxed);
        if (pad == nullptr) return;
        g_watch[0] = reinterpret_cast<std::uint32_t>(pad) + ac2::kPadRightStick;
        started = true;
        armedAt = now;
        RearmTrace(++round);
    }
    const bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (f11 && !f11WasDown && GameHasForeground()) {
        ReportTrace(false);
        armedAt = now;
        RearmTrace(++round);
    }
    f11WasDown = f11;

    const bool armed = InterlockedCompareExchange(&g_traceArmed, 0, 0) != 0;
    if (armed && now - armedAt >= g_traceSeconds * 1000) {
        InterlockedExchange(&g_traceArmed, 0);
        ApplyWatches(false);
        ReportTrace(false);
        Log("trace round %u finished; press F11 in game to trace again", round);
        return;
    }
    if (!armed) return;
    if (g_compositePad != reportedComposite) {
        reportedComposite = g_compositePad;
        Log("trace: composite pad at %08X (from the copy at %08X); watching its stick",
            static_cast<unsigned>(reportedComposite), static_cast<unsigned>(kCompositeCopyVa));
    }
    const LONG generation = InterlockedCompareExchange(&g_watchGeneration, 0, 0);
    if (generation != appliedGeneration || now - lastApply >= 1000) {
        ApplyWatches(true);  // also covers threads created since the last pass
        appliedGeneration = generation;
        lastApply = now;
    }
}
#endif

// ---------------------------------------------------------------------------------------
// Reporting

void ReportStatistics(DWORD elapsedMs) {
    const std::uint32_t polls = g_polls.exchange(0);
    const std::uint32_t failed = g_failedPolls.exchange(0);
    const std::uint32_t moving = g_movingPolls.exchange(0);
    const long long absX = g_absX.exchange(0);
    const long long absY = g_absY.exchange(0);
    const long maxX = g_maxX.exchange(0);
    const long maxY = g_maxY.exchange(0);
    const std::uint32_t hitsX = g_clampHitsX.exchange(0);
    const std::uint32_t hitsY = g_clampHitsY.exchange(0);
    const long long lostX = g_clampLostX.exchange(0);
    const long long lostY = g_clampLostY.exchange(0);
    const long long sumTicks = g_intervalSumTicks.exchange(0);
    const long long maxTicks = g_intervalMaxTicks.exchange(0);
    const long long minTicks = g_intervalMinTicks.exchange(LLONG_MAX);
    if (polls == 0 && failed == 0) return;

    const double toMs = 1000.0 / static_cast<double>(g_qpcFrequency.QuadPart);
    const double averageMs = polls > 1 ? sumTicks * toMs / (polls - 1) : 0.0;
    Log("input %lus: polls=%lu (%.1f/s) failed=%lu foreground=%u moving=%lu |x|=%lld |y|=%lld "
        "max x=%ld y=%ld clamped polls x=%lu y=%lu lost counts x=%lld y=%lld "
        "interval ms min/avg/max=%.2f/%.2f/%.2f",
        elapsedMs / 1000, polls, polls * 1000.0 / elapsedMs, failed, GameHasForeground() ? 1u : 0u,
        moving, absX, absY, maxX, maxY, hitsX, hitsY, lostX, lostY,
        minTicks == LLONG_MAX ? 0.0 : minTicks * toMs, averageMs, maxTicks * toMs);

    const std::uint32_t mouseCalls = g_curveMouseCalls.exchange(0);
    const std::uint32_t padCalls = g_curvePadCalls.exchange(0);
    const std::uint32_t linear = g_curveLinear.exchange(0);
    const std::uint32_t stale = g_staleDrops.exchange(0);
    const std::uint32_t singleAxis = g_singleAxisFrames.exchange(0);
    const std::uint32_t nonInteger = g_nonIntegerSticks.exchange(0);
    const std::uint32_t switches = g_cameraSwitches.exchange(0);
    const std::uint32_t locked = g_lockedFrames.exchange(0);
    const std::uint32_t blends = g_blendUpdates.exchange(0);
    const std::uint32_t repeats = g_repeatUpdates.exchange(0);
    const std::uint32_t zeroStick = g_zeroStickFrames.exchange(0);
    const long long zeroStickCounts = g_zeroStickCounts.exchange(0);
    const long long appliedX = g_appliedX.exchange(0);
    const long long appliedY = g_appliedY.exchange(0);
    if (mouseCalls + padCalls == 0) return;
    Log("polar camera %lus: updates mouse=%lu gamepad=%lu linear=%lu applied |x|=%lld |y|=%lld "
        "stale drops=%lu locked=%lu blend updates=%lu repeat updates=%lu zero-stick frames=%lu "
        "(counts %lld) single-axis frames=%lu non-integer native sticks=%lu camera switches=%lu",
        elapsedMs / 1000, mouseCalls, padCalls, linear, appliedX, appliedY, stale, locked, blends,
        repeats, zeroStick, zeroStickCounts, singleAxis, nonInteger, switches);
    if (switches != 0) LogCameraParameters();
}

void PollHotkeys() {
    static bool wasDown[4]{};
    const int keys[4] = {VK_F6, VK_F8, VK_F9, VK_F10};
    bool down[4]{};
    for (int index = 0; index < 4; ++index) down[index] = (GetAsyncKeyState(keys[index]) & 0x8000) != 0;
    if (g_hotkeys && GameHasForeground()) {
        if (down[0] && !wasDown[0]) {
            LoadConfig();
            DiscardPendingCounts();
            LogSensitivity("F6 reload");
            LogCameraParameters();
        }
        if (down[1] && !wasDown[1]) {
            const LONG enabled = InterlockedCompareExchange(&g_linearCamera, 0, 0) == 0 ? 1 : 0;
            InterlockedExchange(&g_linearCamera, enabled);
            DiscardPendingCounts();
            Log("F8: linear mouse camera %s", enabled ? "ON" : "OFF (game's own curve)");
        }
        if ((down[2] && !wasDown[2]) || (down[3] && !wasDown[3])) {
            const float factor = down[3] ? kHotkeyMultiplierStep : 1.0f / kHotkeyMultiplierStep;
            g_sensitivityMultiplier.store(
                std::clamp(g_sensitivityMultiplier.load() * factor, kMinMultiplier, kMaxMultiplier));
            LogSensitivity(down[3] ? "F10 (this session; set it in the ini to keep it)"
                                   : "F9 (this session; set it in the ini to keep it)");
            LogCameraParameters();
        }
    }
    for (int index = 0; index < 4; ++index) wasDown[index] = down[index];
}

DWORD WINAPI Initialize(LPVOID) {
    if (InterlockedCompareExchange(&g_initializationStarted, 1, 0) != 0) return 1;
    QueryPerformanceFrequency(&g_qpcFrequency);
    g_staleTicks = g_qpcFrequency.QuadPart * kStaleCountsMs / 1000;
    Log("AC2 Raw Mouse Fix %s loading", AC2_RAW_MOUSE_VERSION);
    if (!HostIsGame()) {
        Log("Host executable is not %s; the fix stays inactive", ac2::kHostExecutable);
        return 1;
    }
    if (!AcquireInstanceGuard()) return 1;

    char failure[256]{};
    auto reader = [](std::uint32_t va, void* destination, std::size_t length) {
        return ReadOwnMemory(va, destination, length);
    };
    if (!InspectImage(failure, sizeof(failure)) ||
        !ac2::ValidateLayout(reader, failure, sizeof(failure))) {
        Log("Executable layout does not match the supported layout: %s; the fix stays inactive",
            failure);
        return 1;
    }
    Log("layout validated: %u code signatures, %u data signatures, %u values",
        static_cast<unsigned>(sizeof(ac2::kCodeSignatures) / sizeof(ac2::kCodeSignatures[0])),
        static_cast<unsigned>(sizeof(ac2::kDataSignatures) / sizeof(ac2::kDataSignatures[0])),
        static_cast<unsigned>(sizeof(ac2::kValueChecks) / sizeof(ac2::kValueChecks[0])));
    LogBuildIdentity();
    LoadConfig();
    if (!g_enabled) {
        Log("disabled by configuration (Enabled=0); the game runs unmodified");
        return 0;
    }
    if (!InstallDetour(ac2::kPollMouseVa, ac2::kPollMousePatchLength,
                       reinterpret_cast<const void*>(&HookPollMouse), g_originalPollMouse,
                       "mouse poll") ||
        !InstallDetour(ac2::kPolarCurveVa, ac2::kPolarCurvePatchLength,
                       reinterpret_cast<const void*>(&HookPolarCurve), g_originalCurve,
                       "polar camera stick curve")) {
        return 1;
    }
    Log("initialized: linear mouse camera %s; F8 toggles it, F9/F10 multiplier -/+ 10%%, F6 reloads the ini",
        InterlockedCompareExchange(&g_linearCamera, 0, 0) != 0 ? "ON" : "OFF (game's own curve)");
#if AC2_DEV_TOOLS
    const bool tracing = g_traceStickReaders && StartTrace();
#endif

    void* reportedPad = nullptr;
    DWORD lastReport = GetTickCount();
    for (;;) {
        Sleep(50);
        void* pad = g_pad.load(std::memory_order_relaxed);
        if (pad != reportedPad) {
            Log("keyboard/mouse pad object at %p", pad);
            reportedPad = pad;
        }
        const DWORD now = GetTickCount();
        PollHotkeys();
#if AC2_DEV_TOOLS
        if (tracing) ServiceTrace(now);
#endif
        if (now - lastReport >= kReportIntervalMs) {
            if (g_diagnostics) ReportStatistics(now - lastReport);
#if AC2_DEV_TOOLS
            if (tracing) ReportTrace(false);
#endif
            lastReport = now;
        }
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, Initialize, nullptr, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
    }
    return TRUE;
}
