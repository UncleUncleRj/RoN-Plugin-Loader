/*
 * The bootstrap: a stand-in for the system's shfolder.dll in the game folder. The game's network
 * library CrossplayNetLib.dll imports shfolder.dll, so this DLL loads at process start, even when an
 * AppCompat layer is set. Its two exports are forwarded to shell32 (shfolder.def).
 *
 * Under the loader lock it does only three things, and only in the known game exe: the bcrypt
 * warm-up, a check of Main::main's first bytes, and a 5-byte jmp there. The loader DLL is loaded
 * later, from that hook, on the game's main thread, outside the loader lock.
 */
#include <windows.h>
#include <bcrypt.h>
#include <string.h>
#include "bootstrap/decide.h"
#include "common/identity.h"

#define MAIN_MAIN_VA 0x560CF0u  /* Main::main, ?main@Main@@SAHPAUHINSTANCE__@@0PADH@Z */

typedef int(__cdecl *MainFn)(HINSTANCE, HINSTANCE, char *, int);

static const uint8_t k_main_sig[5] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};  /* push ebp; mov ebp,esp; push -1 */
static MainFn g_orig_main;
static HMODULE g_self;

/*
 * Start-up hang (seen since the Windows updates of 2026-10): SteamAPI_Init loads steamclient.dll,
 * whose DllMain asks bcrypt for random bytes while holding the loader lock, while a PartyWin.dll
 * thread is inside bcrypt's first-use setup, which waits for the loader. Neither goes on. bcrypt's
 * setup runs once per process, so doing it here, before any other thread exists, removes the wait.
 */
static void warm_up_bcrypt(void)
{
    unsigned char b[16];
    BCryptGenRandom(NULL, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

static void start_loader(void)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    wchar_t *slash = n && n < MAX_PATH ? wcsrchr(path, L'\\') : NULL;
    if (!slash || (size_t)(slash - path) + 21 >= MAX_PATH) {
        OutputDebugStringW(L"RoN Plugin Loader: no path for RonPluginLoader.dll; the game runs without plugins\n");
        return;
    }
    wcscpy(slash + 1, L"RonPluginLoader.dll");
    HMODULE loader = LoadLibraryW(path);
    if (!loader) {
        OutputDebugStringW(L"RoN Plugin Loader: RonPluginLoader.dll did not load; the game runs without plugins\n");
        return;
    }
    void(__cdecl * start)(void) = (void(__cdecl *)(void))(void *)GetProcAddress(loader, "RonLoaderStart");
    if (!start) {
        OutputDebugStringW(L"RoN Plugin Loader: RonPluginLoader.dll has no RonLoaderStart; the game runs without plugins\n");
        return;
    }
    start();
}

static int __cdecl main_detour(HINSTANCE inst, HINSTANCE prev, char *cmd, int show)
{
    static int started;
    if (!started) {
        started = 1;
        start_loader();
    }
    return g_orig_main(inst, prev, cmd, show);
}

static void patch_main(uintptr_t delta)
{
    uint8_t *at = (uint8_t *)(MAIN_MAIN_VA + delta);
    if (memcmp(at, k_main_sig, sizeof k_main_sig) != 0)
        return;  /* not the code we know: leave the game alone */
    uint8_t *tramp = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp)
        return;
    /* trampoline: the 5 stolen bytes, then jmp back to Main::main + 5 */
    memcpy(tramp, at, 5);
    int32_t back = (int32_t)((uintptr_t)(at + 5) - (uintptr_t)(tramp + 10));
    tramp[5] = 0xE9;
    memcpy(tramp + 6, &back, 4);
    FlushInstructionCache(GetCurrentProcess(), tramp, 10);
    g_orig_main = (MainFn)(void *)tramp;

    uint8_t jmp[5] = {0xE9};
    int32_t to = (int32_t)((uintptr_t)main_detour - (uintptr_t)(at + 5));
    memcpy(jmp + 1, &to, 4);
    DWORD old;
    if (!VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(at, jmp, 5);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        const uint8_t *image = (const uint8_t *)GetModuleHandleW(NULL);
        if (bs_should_run(image)) {
            warm_up_bcrypt();
            patch_main(identity_delta(image));
        }
    }
    return TRUE;
}
