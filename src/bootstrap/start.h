/*
 * The hook at Main::main: it runs the loader once, then enters the game with every register, the
 * flags and the stack exactly as WinMain left them.
 *
 * WinMain does not call Main::main the way its symbol name says (__cdecl, four stack arguments):
 * it passes the program's HINSTANCE in ECX and two stack arguments (`mov ecx,[ebp+8]` before the call
 * at 0x5609D0; Main::main stores ECX at 0x560D11). A C detour may change ECX, and then the game keeps
 * a wrong HINSTANCE: d3dgl.dll loads its cursors from the exe's resources with it, so the mouse
 * pointer is invisible (found 2026-11-24). Hence a stub in assembly.
 */
#ifndef RON_START_H
#define RON_START_H

extern void (*bs_start_fn)(void);  /* run on the first entry, then cleared */
extern void *bs_start_orig;        /* where the stub goes next: the trampoline into Main::main */
void bs_start_stub(void);          /* the target of the jmp at Main::main; not callable from C */

#endif
