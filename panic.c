// ============================================================================
// panic.c — Kernel Panic (красный экран смерти)
// ============================================================================
#include "panic.h"
#include "vga.h"

void panic(const char *msg)
{
    // Запрещаем прерывания: дальше только HLT.
    __asm__ __volatile__ ("cli");

    vga_print("\n", 0x4F);
    vga_print("================================================================================\n", 0x4F);
    vga_print("  *** KERNEL PANIC ***\n", 0x4F);
    vga_print("  ", 0x4F);
    if (msg)
        vga_print(msg, 0x4F);
    else
        vga_print("(no message)", 0x4F);
    vga_print("\n", 0x4F);
    vga_print("  System halted. Please reboot.\n", 0x4F);
    vga_print("================================================================================\n", 0x4F);

    for (;;)
        __asm__ __volatile__ ("hlt");
}
