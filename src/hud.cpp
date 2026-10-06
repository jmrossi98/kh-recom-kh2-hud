// Native side of the Re:CoM KH2 HUD mod. Loaded from kh2hud.lua with
// package.loadlib and driven once per game frame through rc_hud().
// Steam build only: every address below is an RVA into the game exe.
#include "overlay.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <share.h>

namespace rva {
constexpr uintptr_t kPlayerPtr   = 0x87B390;  // -> player battle object
constexpr uintptr_t kBattleFlag  = 0x87B858;  // 0 field, 1 battle
constexpr uintptr_t kSavePtr     = 0x87CBF8;  // -> save/status block: HP, max HP, ...
constexpr uintptr_t kWorld       = 0x87B862;  // 0xFF in menus
constexpr uintptr_t kLoading     = 0xAF70CA;  // 0xFF while loading
constexpr uintptr_t kGetFlags    = 0x1B08C0;  // uint (int group, int id)
constexpr uintptr_t kSetFlags    = 0x1B16C0;  // void (int group, int id, uint value)
constexpr uintptr_t kMapCardGate = 0x1E4818;  // and eax,8: hide test for the map card panel
}

// Flag word the battle HUD checks. Bit 0 hides the player's portrait and HP ring.
constexpr int kFlagGroup = 1;
constexpr int kFlagCards = 0xE2;
constexpr uint32_t kFlagHidePlayerGauge = 0x01;

// player battle object
constexpr uintptr_t kPlHp    = 0x42C;
constexpr uintptr_t kPlHpMax = 0x430;

using GetFlagsFn = uint32_t (*)(int group, int id);
using SetFlagsFn = void (*)(int group, int id, uint32_t value);

static uintptr_t g_base = 0;
static FILE* g_log = nullptr;
static bool g_disabled = false;
static bool g_hide = true;
static bool g_hideKeyDown = false;

template <typename T>
static T& at(uintptr_t addr) { return *reinterpret_cast<T*>(addr); }

static void logf(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void open_log() {
    char path[MAX_PATH];
    HMODULE self = nullptr;
    // Pin the module. Reloading scripts closes the Lua state, which would unload
    // us and leave the game calling into a hook that is no longer there.
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCSTR>(&open_log), &self);
    DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
    if (n == 0 || n + 4 >= MAX_PATH) return;
    lstrcatA(path, ".log");
    g_log = _fsopen(path, "w", _SH_DENYWR);
}

static bool write_code(uint8_t* at_addr, const uint8_t* bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect(at_addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at_addr, bytes, n);
    VirtualProtect(at_addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at_addr, n);
    return true;
}

static void install_patches() {
    // Outside battle the game shows the current map card bottom right, where
    // the status gauges now are. The room map and this panel share one hide
    // bit; make only the panel's test read it as set.  and eax,8 -> or eax,8
    uint8_t* panel = reinterpret_cast<uint8_t*>(g_base + rva::kMapCardGate);
    const uint8_t panel_expect[3] = {0x83, 0xE0, 0x08};
    const uint8_t panel_hide[3]   = {0x83, 0xC8, 0x08};
    if (memcmp(panel, panel_hide, 3) == 0) logf("map card panel already hidden");   // the combat mod got there first
    else if (memcmp(panel, panel_expect, 3) == 0 && write_code(panel, panel_hide, 3)) logf("map card panel hidden");
    else logf("map card panel: unexpected bytes, leaving it");
}

// What the combat mod and the KH2 HUD mod tell each other, in a small named
// block of shared memory. Neither needs the other; each stamps its tick every
// frame so the other can tell it is there. When both are, the HUD mod draws
// the command menu in KH2's style from what the combat mod puts here, and
// magic costs MP, which the HUD mod shows.
struct SharedState {
    ULONGLONG hudTick;
    ULONGLONG combatTick;
    float mp;          // 0..1
    LONG mpCharging;
    // the command menu, written by the combat mod
    LONG selected;     // row of the main menu
    LONG subOpen;      // a list is open over it
    LONG subCount;
    LONG subSelected;
    char subNames[16][24];
};
constexpr char kSharedName[] = "Local\\recom_mods_v2";

// Whichever mod loads first makes the block.
static volatile SharedState* shared_state() {
    static volatile SharedState* block = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedState),
                                          kSharedName))
            block = static_cast<SharedState*>(MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedState)));
    }
    return block;
}

static void set_gauge_hidden(bool hidden) {
    uint32_t f = reinterpret_cast<GetFlagsFn>(g_base + rva::kGetFlags)(kFlagGroup, kFlagCards);
    uint32_t want = hidden ? f | kFlagHidePlayerGauge : f & ~kFlagHidePlayerGauge;
    if (want != f) reinterpret_cast<SetFlagsFn>(g_base + rva::kSetFlags)(kFlagGroup, kFlagCards, want);
}

static void frame() {
    OverlayState& hud = overlay_state();
    hud.gauge = true;
    hud.kh2Hud = true;
    hud.mpBar = false;
    hud.menu = false;
    if (volatile SharedState* b = shared_state()) {
        b->hudTick = GetTickCount64();
        // With the combat mod running there is a command menu to draw and MP
        // to show. It keeps both; this only copies them out.
        if (GetTickCount64() - b->combatTick < 500) {
            static char names[16][24];
            hud.mpBar = true;
            hud.mp = b->mp;
            hud.mpCharging = b->mpCharging != 0;
            hud.menu = true;
            hud.selected = b->selected;
            hud.subOpen = b->subOpen != 0;
            hud.subCount = b->subCount < 0 ? 0 : (b->subCount > 16 ? 16 : b->subCount);
            hud.subSelected = b->subSelected;
            for (int i = 0; i < hud.subCount; ++i) {
                memcpy(names[i], const_cast<char*>(b->subNames[i]), 24);
                names[i][23] = 0;
                hud.subNames[i] = names[i];
            }
        }
    }
    hud.world = at<uint8_t>(g_base + rva::kWorld);
    uintptr_t player = at<uintptr_t>(g_base + rva::kPlayerPtr);
    hud.battle = at<uint8_t>(g_base + rva::kBattleFlag) == 1 && player;
    if (hud.battle) {
        hud.visible = true;
        hud.hp = at<int32_t>(player + kPlHp);
        hud.hpMax = at<int32_t>(player + kPlHpMax);
    } else {
        // Walking around: HP from the save block, since the battle object is
        // not there.
        uintptr_t save = at<uintptr_t>(g_base + rva::kSavePtr);
        hud.visible = player && save && at<uint8_t>(g_base + rva::kWorld) != 0xFF &&
                      at<uint8_t>(g_base + rva::kLoading) != 0xFF;
        if (hud.visible) {
            hud.hp = at<int32_t>(save);
            hud.hpMax = at<int32_t>(save + 4);
        }
    }
    if (!hud.visible) return;

    // F8 shows the game's own portrait and gauge again, for comparing
    bool hideKey = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (hideKey && !g_hideKeyDown) {
        g_hide = !g_hide;
        logf("game gauge %s", g_hide ? "hidden" : "shown");
        if (!g_hide) set_gauge_hidden(false);
    }
    g_hideKeyDown = hideKey;
    if (g_hide) set_gauge_hidden(true);
}

// Safety net for a hang at start-up, which in exclusive fullscreen takes the
// whole desktop with it: for the first few minutes, if the game stops calling
// in for 20 seconds, note how far things got and end the process.
static volatile LONG g_beat = 0;
static const char* volatile g_stage = "start";

static DWORD WINAPI watchdog(void*) {
    const ULONGLONG began = GetTickCount64();
    LONG seen = g_beat;
    ULONGLONG since = began;
    while (GetTickCount64() - began < 180000) {
        Sleep(1000);
        if (g_beat != seen) {
            seen = g_beat;
            since = GetTickCount64();
        } else if (GetTickCount64() - since > 20000) {
            logf("watchdog: no frame for 20 s after frame %ld, last stage: %s. ending the game", seen, g_stage);
            TerminateProcess(GetCurrentProcess(), 1);
        }
    }
    logf("watchdog: start-up went fine (%ld frames), standing down", g_beat);
    return 0;
}

static int guarded_frame() {
    __try {
        frame();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

extern "C" {

// lua_CFunction. Takes no arguments and returns nothing, so it never touches the Lua state.
__declspec(dllexport) int rc_hud(void*) {
    if (g_disabled) return 0;
    if (!g_base) {
        g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        open_log();
        logf("loaded, exe base %p", reinterpret_cast<void*>(g_base));
        CloseHandle(CreateThread(nullptr, 0, &watchdog, nullptr, 0, nullptr));
        g_stage = "installing";
        install_patches();
        overlay_install(&logf);
    }
    // the first frames are logged one by one, so a hang shows where it stopped
    const LONG n = InterlockedIncrement(&g_beat);
    if (n <= 5 || n == 60 || n == 600) logf("frame %ld", n);
    g_stage = "frame";
    if (int code = guarded_frame()) {
        logf("exception %08X, disabling", code);
        g_disabled = true;
    }
    g_stage = "between frames";
    return 0;
}

}
