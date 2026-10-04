// ============================================================================
// user_protect.c — Stack protector runtime для userland (Ring 3)
// ============================================================================
// При -fstack-protector-strong компилятор ссылается на __stack_chk_guard и
// __stack_chk_fail. libc нет — даём свой рантайм: при разрушении канарейки
// выходим через sys_exit(134) вместо молчаливого продолжения.
// ============================================================================
#include "../syscall.h"
#include <stdint.h>

uintptr_t __stack_chk_guard = 0xC0FFEE1234567890ULL;

void __stack_chk_fail(void)
{
    sys_print("\n[user] *** STACK SMASHING DETECTED, exiting ***\n");
    sys_exit(134);
    for (;;) {
    }
}
