// ============================================================================
// exceptions.c — C-обработчик исключений CPU: красивый дамп + panic()
// ============================================================================
#include "exceptions.h"
#include "idt.h"
#include "panic.h"
#include "vga.h"
#include <stdint.h>

// Имена исключений 0-31 (Intel SDM Vol.3).
static const char *exc_names[32] = {
    "#DE Divide Error",
    "#DB Debug Exception",
    "NMI Interrupt",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reserved (15)",
    "#MF x87 FPU Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point",
    "#VE Virtualization",
    "#CP Control Protection",
    "Reserved (22)",
    "Reserved (23)",
    "Reserved (24)",
    "Reserved (25)",
    "Reserved (26)",
    "Reserved (27)",
    "#HV Hypervisor Injection",
    "#VC VMM Communication",
    "#SX Security Exception",
    "Reserved (31)",
};

// Стабы из exceptions.asm.
extern void exc_stub_0(void);  extern void exc_stub_1(void);
extern void exc_stub_2(void);  extern void exc_stub_3(void);
extern void exc_stub_4(void);  extern void exc_stub_5(void);
extern void exc_stub_6(void);  extern void exc_stub_7(void);
extern void exc_stub_8(void);  extern void exc_stub_9(void);
extern void exc_stub_10(void); extern void exc_stub_11(void);
extern void exc_stub_12(void); extern void exc_stub_13(void);
extern void exc_stub_14(void); extern void exc_stub_15(void);
extern void exc_stub_16(void); extern void exc_stub_17(void);
extern void exc_stub_18(void); extern void exc_stub_19(void);
extern void exc_stub_20(void); extern void exc_stub_21(void);
extern void exc_stub_22(void); extern void exc_stub_23(void);
extern void exc_stub_24(void); extern void exc_stub_25(void);
extern void exc_stub_26(void); extern void exc_stub_27(void);
extern void exc_stub_28(void); extern void exc_stub_29(void);
extern void exc_stub_30(void); extern void exc_stub_31(void);

static void *exc_stubs[32] = {
    exc_stub_0,  exc_stub_1,  exc_stub_2,  exc_stub_3,
    exc_stub_4,  exc_stub_5,  exc_stub_6,  exc_stub_7,
    exc_stub_8,  exc_stub_9,  exc_stub_10, exc_stub_11,
    exc_stub_12, exc_stub_13, exc_stub_14, exc_stub_15,
    exc_stub_16, exc_stub_17, exc_stub_18, exc_stub_19,
    exc_stub_20, exc_stub_21, exc_stub_22, exc_stub_23,
    exc_stub_24, exc_stub_25, exc_stub_26, exc_stub_27,
    exc_stub_28, exc_stub_29, exc_stub_30, exc_stub_31,
};

void exceptions_init(void)
{
    __asm__ __volatile__ ("cli");
    for (uint8_t i = 0; i < 32; i++)
        idt_set_entry(i, (uint64_t)exc_stubs[i]);
    __asm__ __volatile__ ("sti");
}

// Вспомогательная печать "NAME   = 0x...".
static void print_reg(const char *name, uint64_t val, uint8_t color)
{
    vga_print("  ", color);
    vga_print(name, color);
    vga_print(" = ", color);
    vga_print_hex64(val, color);
    vga_print("\n", color);
}

void exception_handler(exc_regs_t *regs)
{
    __asm__ __volatile__ ("cli");

    uint64_t vec = regs->vector & 0xFF;
    const char *name = (vec < 32) ? exc_names[vec] : "Unknown";

    uint64_t cr2 = 0, cr3 = 0;
    __asm__ __volatile__ ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ __volatile__ ("mov %%cr3, %0" : "=r"(cr3));

    // Фактический RSP в момент исключения (ring0->ring0, без смены стека).
    uint64_t fault_rsp = (uint64_t)regs + sizeof(exc_regs_t);

    uint8_t C = 0x4F; // белый на красном

    vga_print("\n", C);
    vga_print("===============================================================================\n", C);
    vga_print("  *** CPU EXCEPTION ***\n", C);
    vga_print("  ", C);
    vga_print(name, C);
    vga_print(" (vector ", C);
    vga_print_u64(vec, C);
    vga_print(")\n", C);
    vga_print("-------------------------------------------------------------------------------\n", C);

    vga_print("  Error Code = ", C);
    vga_print_hex64(regs->error, C);
    vga_print("\n", C);
    vga_print("-------------------------------------------------------------------------------\n", C);

    print_reg("RAX", regs->rax, C);
    print_reg("RBX", regs->rbx, C);
    print_reg("RCX", regs->rcx, C);
    print_reg("RDX", regs->rdx, C);
    print_reg("RSI", regs->rsi, C);
    print_reg("RDI", regs->rdi, C);
    print_reg("RBP", regs->rbp, C);
    print_reg("R8 ", regs->r8,  C);
    print_reg("R9 ", regs->r9,  C);
    print_reg("R10", regs->r10, C);
    print_reg("R11", regs->r11, C);
    print_reg("R12", regs->r12, C);
    print_reg("R13", regs->r13, C);
    print_reg("R14", regs->r14, C);
    print_reg("R15", regs->r15, C);
    vga_print("-------------------------------------------------------------------------------\n", C);
    print_reg("RIP", regs->rip, C);
    print_reg("CS ", regs->cs,  C);
    print_reg("FLG", regs->rflags, C);
    print_reg("RSP", fault_rsp, C);
    print_reg("CR2", cr2, C);
    print_reg("CR3", cr3, C);
    vga_print("===============================================================================\n", C);

    panic("CPU exception - system halted");
}
