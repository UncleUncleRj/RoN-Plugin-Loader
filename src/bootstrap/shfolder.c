/*
 * The start of shfolder.dll, the loader's only file (user decision 2026-11-24: one file). It stands in
 * for the system's shfolder.dll in the game folder: the game's network library CrossplayNetLib.dll
 * imports shfolder.dll, so this DLL loads at process start, even when an AppCompat layer is set. Its
 * two exports are forwarded to shell32 (shfolder.def); the loader (src/loader/) is built into it.
 *
 * Under the loader lock it does only three things, and only in the known game exe: the bcrypt
 * warm-up, a check of Main::main's first bytes, and a 5-byte jmp there. The loader runs later, from
 * that hook, on the game's main thread, outside the loader lock.
 */
#include <windows.h>
#include <bcrypt.h>
#include <string.h>
#include "bootstrap/decide.h"
#include "bootstrap/start.h"
#include "common/identity.h"
#include "loader/loader.h"

/* Main::main, ?main@Main@@SAHPAUHINSTANCE__@@0PADH@Z. Despite the name it takes the HINSTANCE in ECX,
   so its hook is the assembly stub in start.c, never a C function. */
#define MAIN_MAIN_VA 0x560CF0u

static const uint8_t k_main_sig[5] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};  /* push ebp; mov ebp,esp; push -1 */

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
    bs_start_orig = tramp;
    bs_start_fn = loader_start;

    uint8_t jmp[5] = {0xE9};
    int32_t to = (int32_t)((uintptr_t)bs_start_stub - (uintptr_t)(at + 5));
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
        DisableThreadLibraryCalls(inst);
        const uint8_t *image = (const uint8_t *)GetModuleHandleW(NULL);
        if (bs_should_run(image)) {
            warm_up_bcrypt();
            patch_main(identity_delta(image));
        }
    }
    return TRUE;
}
