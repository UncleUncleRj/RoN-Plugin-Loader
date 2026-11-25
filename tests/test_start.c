/*
 * The hook at Main::main. WinMain passes the program's HINSTANCE to Main::main in ECX (a
 * compiler-private convention; the symbol says __cdecl) and two stack arguments. The hook must run
 * the loader once and enter the game with every register and the stack exactly as WinMain left them.
 *
 * call_via() plays WinMain: known values in ECX, EDX, EBX, ESI, EDI, two pushed arguments, then a
 * call to the hook. probe_main() plays Main::main: it records what arrives and returns 0x5EED. The
 * loader stand-in, clobber_start(), changes ECX and EDX the way any C function may.
 */
#include <stdint.h>
#include "check.h"
#include "bootstrap/start.h"

uint32_t g_start_seen[9];  /* ecx edx ebx esi edi arg1 arg2 | esp after the call, esp expected */
static int g_start_runs;

__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl _probe_main\n"
        ".p2align 4\n"
        "_probe_main:\n"
        "    mov [_g_start_seen], ecx\n"
        "    mov [_g_start_seen+4], edx\n"
        "    mov [_g_start_seen+8], ebx\n"
        "    mov [_g_start_seen+12], esi\n"
        "    mov [_g_start_seen+16], edi\n"
        "    mov eax, [esp+4]\n"
        "    mov [_g_start_seen+20], eax\n"
        "    mov eax, [esp+8]\n"
        "    mov [_g_start_seen+24], eax\n"
        "    mov eax, 0x5EED\n"
        "    ret\n"
        ".globl _call_via\n"
        ".p2align 4\n"
        "_call_via:\n"                       /* uint32_t call_via(void *hook), cdecl */
        "    push ebp\n"
        "    mov ebp, esp\n"
        "    push ebx\n"
        "    push esi\n"
        "    push edi\n"
        "    mov eax, [ebp+8]\n"
        "    mov ecx, 0x11110001\n"
        "    mov edx, 0x22220002\n"
        "    mov ebx, 0x33330003\n"
        "    mov esi, 0x44440004\n"
        "    mov edi, 0x55550005\n"
        "    push 0xA2A2A2A2\n"
        "    push 0xA1A1A1A1\n"
        "    call eax\n"
        "    add esp, 8\n"
        "    mov [_g_start_seen+28], esp\n"
        "    lea ecx, [ebp-12]\n"
        "    mov [_g_start_seen+32], ecx\n"
        "    pop edi\n"
        "    pop esi\n"
        "    pop ebx\n"
        "    pop ebp\n"
        "    ret\n"
        ".att_syntax prefix\n");

int probe_main(void);
uint32_t call_via(void *hook);

static void clobber_start(void)
{
    g_start_runs++;
    __asm__ volatile("movl $0xDEAD0001, %%ecx\n\tmovl $0xDEAD0002, %%edx" ::: "ecx", "edx");
}

static void check_arrival(void)
{
    CHECK_EQ(g_start_seen[0], 0x11110001u);  /* ECX: the HINSTANCE in the game */
    CHECK_EQ(g_start_seen[1], 0x22220002u);
    CHECK_EQ(g_start_seen[2], 0x33330003u);
    CHECK_EQ(g_start_seen[3], 0x44440004u);
    CHECK_EQ(g_start_seen[4], 0x55550005u);
    CHECK_EQ(g_start_seen[5], 0xA1A1A1A1u);  /* the stack arguments, where WinMain put them */
    CHECK_EQ(g_start_seen[6], 0xA2A2A2A2u);
    CHECK_EQ(g_start_seen[7], g_start_seen[8]);  /* the stack is balanced afterwards */
}

TEST(start_keeps_registers_and_stack)
{
    g_start_runs = 0;
    bs_start_fn = clobber_start;
    bs_start_orig = (void *)probe_main;
    memset(g_start_seen, 0, sizeof g_start_seen);
    CHECK_EQ(call_via((void *)bs_start_stub), 0x5EED);  /* Main::main's return value comes back */
    CHECK_EQ(g_start_runs, 1);
    check_arrival();
}

TEST(start_runs_loader_once)
{
    g_start_runs = 0;
    bs_start_fn = clobber_start;
    bs_start_orig = (void *)probe_main;
    call_via((void *)bs_start_stub);
    memset(g_start_seen, 0, sizeof g_start_seen);
    CHECK_EQ(call_via((void *)bs_start_stub), 0x5EED);
    CHECK_EQ(g_start_runs, 1);  /* the second entry goes straight to the game */
    check_arrival();
}
