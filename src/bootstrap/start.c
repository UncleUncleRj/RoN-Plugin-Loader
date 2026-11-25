/* The hook at Main::main (start.h). */
#include <stddef.h>
#include "bootstrap/start.h"

void (*bs_start_fn)(void);
void *bs_start_orig;

void bs_start_once(void);

void bs_start_once(void)
{
    void (*fn)(void) = bs_start_fn;
    if (fn) {
        bs_start_fn = NULL;
        fn();
    }
}

/* Save the flags and every register, run the loader once, restore them all and jump on: the game
   gets exactly the ECX (its HINSTANCE), registers and stack WinMain set up, and returns straight to
   WinMain. */
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl _bs_start_stub\n"
        ".p2align 4\n"
        "_bs_start_stub:\n"
        "    pushfd\n"
        "    pushad\n"
        "    cld\n"
        "    call _bs_start_once\n"
        "    popad\n"
        "    popfd\n"
        "    jmp dword ptr [_bs_start_orig]\n"
        ".att_syntax prefix\n");
