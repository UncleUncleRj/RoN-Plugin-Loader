/* Hook targets with known bytes, for the hook manager tests.
 *   hk_add1:   55 8B EC 8B 45 08 83 C0 01 5D C3   returns x + 1 (cdecl); 6 bytes can be stolen
 *   hk_caller: FF 74 24 04 E8 <rel32> 83 C4 04 C3  calls hk_add1; the E8 is at offset 4
 *   hk_slot:   a pointer to hk_add1
 * hk_add1 is exported from tests.exe so the test plugins can find it with GetProcAddress. */
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl _hk_add1\n"
        ".p2align 4\n"
        "_hk_add1:\n"  /* push ebp; mov ebp, esp (8B EC, as the game's prologues); mov eax, [ebp+8];
                          add eax, 1; pop ebp; ret -- spelled out, the assembler prefers 89 E5 */
        "    .byte 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x83, 0xC0, 0x01, 0x5D, 0xC3\n"
        ".globl _hk_caller\n"
        ".p2align 4\n"
        "_hk_caller:\n"
        "    push dword ptr [esp+4]\n"
        "    call _hk_add1\n"
        "    add esp, 4\n"
        "    ret\n"
        ".section .drectve\n"
        ".ascii \" -export:hk_add1\"\n"
        ".text\n"
        ".att_syntax prefix\n");

int hk_add1(int x);
int (*hk_slot)(int) = hk_add1;
