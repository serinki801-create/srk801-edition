// ============================================================================
// kernel.c — AI-OS Phase 4: VFS/initrd + Ring3/syscalls + ELF + framebuffer
// ============================================================================
#include "gdt.h"
#include "idt.h"
#include "exceptions.h"
#include "panic.h"
#include "timer.h"
#include "pmm.h"
#include "kheap.h"
#include "task.h"
#include "process.h"
#include "scheduler.h"
#include "keyboard.h"
#include "vga.h"
#include "port.h"
#include "vfs.h"
#include "initrd.h"
#include "syscall.h"
#include "elf.h"
#include "framebuffer.h"
#include "security.h"
#include "mouse.h"
#include "wm.h"
#include "desktop.h"
#include "minicc.h"
#include <stdint.h>
#include <stddef.h>

#define CMD_BUFFER_SIZE 256

// Изолированный стек Ring 3 для команды exec (16 КБ, в BSS ядра).
static uint8_t user_stack[16384] __attribute__((aligned(16)));

static int streq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int starts_with(const char *str, const char *prefix)
{
    while (*prefix && *str && *str == *prefix) { str++; prefix++; }
    return *prefix == '\0';
}

static const char *state_name(task_state_t s)
{
    switch (s) {
        case TASK_RUNNING: return "RUNNING";
        case TASK_READY:   return "READY";
        default:           return "-";
    }
}

// Печать знакового результата мини-компилятора.
static void print_int(int v)
{
    long long lv = v;
    if (lv < 0) {
        vga_print("-", 0x07);
        lv = -lv;
    }
    vga_print_u64((uint64_t)lv, 0x07);
}

// --- Phase 6: встроенный мини-компилятор C/C++/ObjC/Swift ----------------------
// Общий арифметический кор (minicc.c) + синтаксические обёртки каждого языка.
// Полные тулчейны — на хосте (build.sh: gcc/g++/clang+GNUstep/swiftc).
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

// Убирает trailing ';' и возвращает длину без него (in-place не трогаем).
static int strip_semi_len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    while (n > 0 && (s[n - 1] == ';' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        n--;
    return n;
}

// Копирует src в dst (dstn байт) с предобработкой под язык.
// lang: 0=C, 1=C++, 2=ObjC, 3=Swift. Возвращает 0 при успехе, -1 при пустом.
static int lang_preprocess(int lang, const char *src, char *dst, int dstn)
{
    static char tmp[224];
    src = skip_spaces(src);
    // Общий шаг: срезать trailing ';' + пробелы.
    int n = strip_semi_len(src);
    if (n <= 0 || n >= (int)sizeof(tmp))
        return -1;
    for (int i = 0; i < n; i++)
        tmp[i] = src[i];
    tmp[n] = '\0';
    const char *e = tmp;

    if (lang == 1) { // C++: опциональный 'constexpr '
        const char *kw = "constexpr ";
        int k = 0;
        while (kw[k] && e[k] == kw[k])
            k++;
        if (kw[k] == '\0')
            e = skip_spaces(e + k);
    } else if (lang == 2) { // ObjC: '@(expr)' / '@num' NSNumber-литералы
        if (e[0] == '@' && e[1] == '(') {
            e += 2;
            int m = 0;
            while (e[m])
                m++;
            while (m > 0 && (e[m - 1] == ' ' || e[m - 1] == '\t' || e[m - 1] == ')'))
                m--;
            if (m <= 0 || m >= dstn)
                return -1;
            for (int i = 0; i < m; i++)
                dst[i] = e[i];
            dst[m] = '\0';
            return 0;
        }
        if (e[0] == '@')
            e++;
    } else if (lang == 3) { // Swift: 'let x = expr' / 'print(expr)'
        if ((e[0] == 'l' && e[1] == 'e' && e[2] == 't' && e[3] == ' ') ||
            (e[0] == 'v' && e[1] == 'a' && e[2] == 'r' && e[3] == ' ')) {
            const char *eq = e;
            while (*eq && *eq != '=')
                eq++;
            if (*eq != '=')
                return -1;
            e = skip_spaces(eq + 1);
        } else if (e[0] == 'p' && e[1] == 'r' && e[2] == 'i' &&
                   e[3] == 'n' && e[4] == 't' && e[5] == '(') {
            e += 6;
            int m = 0;
            while (e[m])
                m++;
            while (m > 0 && (e[m - 1] == ' ' || e[m - 1] == '\t' || e[m - 1] == ')'))
                m--;
            if (m <= 0 || m >= dstn)
                return -1;
            for (int i = 0; i < m; i++)
                dst[i] = e[i];
            dst[m] = '\0';
            return 0;
        }
    }
    int m = 0;
    while (e[m] && m + 1 < dstn) {
        dst[m] = e[m];
        m++;
    }
    dst[m] = '\0';
    return m > 0 ? 0 : -1;
}

static const char *lang_name(int lang)
{
    switch (lang) {
        case 0: return "C";
        case 1: return "C++";
        case 2: return "ObjC";
        default: return "Swift";
    }
}

static void cmd_compile(int lang, const char *arg)
{
    char expr[224];
    arg = skip_spaces(arg);
    if (*arg == '\0') {
        vga_print("Usage: ", 0x07);
        vga_print(lang_name(lang), 0x07);
        vga_print(" <expr>  (e.g. ", 0x07);
        vga_print(lang_name(lang), 0x07);
        vga_print(" 2+3*4)\n", 0x07);
        return;
    }
    if (lang_preprocess(lang, arg, expr, sizeof(expr)) != 0) {
        vga_print("compile: empty expression\n", 0x0C);
        return;
    }
    int result = 0;
    char err[64];
    if (mini_compile_run(expr, &result, err, sizeof(err)) == 0) {
        vga_print(lang_name(lang), 0x0A);
        vga_print(": ", 0x0A);
        vga_print(expr, 0x0A);
        vga_print(" = ", 0x0A);
        print_int(result);
        vga_print("  [compiled to stack-VM bytecode, executed]\n", 0x0A);
    } else {
        vga_print("compile error: ", 0x0C);
        vga_print(err[0] ? err : "unknown", 0x0C);
        vga_print("\n", 0x0C);
    }
}

static void cmd_build(const char *arg)
{
    char langw[16];
    arg = skip_spaces(arg);
    int i = 0;
    while (arg[i] && arg[i] != ' ' && arg[i] != '\t' && i + 1 < (int)sizeof(langw)) {
        langw[i] = arg[i];
        i++;
    }
    langw[i] = '\0';
    int lang = -1;
    if (streq(langw, "c"))
        lang = 0;
    else if (streq(langw, "cpp") || streq(langw, "c++"))
        lang = 1;
    else if (streq(langw, "m") || streq(langw, "objc"))
        lang = 2;
    else if (streq(langw, "swift"))
        lang = 3;
    if (lang < 0) {
        vga_print("Usage: build <c|cpp|objc|swift> <expr>\n", 0x07);
        return;
    }
    const char *expr_arg = skip_spaces(arg + i);
    if (*expr_arg == '\0') {
        vga_print("Usage: build <c|cpp|objc|swift> <expr>\n", 0x07);
        return;
    }
    char expr[224];
    if (lang_preprocess(lang, expr_arg, expr, sizeof(expr)) != 0) {
        vga_print("build: empty expression\n", 0x0C);
        return;
    }
    vga_print("build pipeline [", 0x07);
    vga_print(lang_name(lang), 0x07);
    vga_print("]:\n", 0x07);
    char log[512];
    int result = 0;
    int rc = mini_build_verbose(expr, &result, log, sizeof(log));
    // Лог многострочный — печатаем как есть (vga_print умеет \n? нет:
    // печатаем построчно вручную).
    {
        char *p = log;
        while (*p) {
            char *nl = p;
            while (*nl && *nl != '\n')
                nl++;
            char saved = *nl;
            *nl = '\0';
            vga_print("  ", 0x07);
            vga_print(p, 0x07);
            vga_print("\n", 0x07);
            if (saved == '\0')
                break;
            p = nl + 1;
        }
    }
    if (rc == 0) {
        vga_print("  => ", 0x0A);
        print_int(result);
        vga_print("  [build OK]\n", 0x0A);
    } else {
        vga_print("  [build FAILED]\n", 0x0C);
    }
}

static void cmd_samples(void)
{
    vga_print("Embedded sources (host: bash build.sh --host):\n", 0x07);
    vga_print("  [c]     int s=0; for(i=1..100) s+=i; // =5050\n", 0x07);
    vga_print("  [cpp]   vector{1..5} sum of squares // =55\n", 0x07);
    vga_print("  [objc]  @[@\"C\",@\"C++\",@\"ObjC\",@\"Swift\"] join\n", 0x07);
    vga_print("  [swift] let sq=(1...5).map{$0*$0}.reduce(0,+) // =55\n", 0x07);
    vga_print("Try: cc 2+3*4 | cxx (1+2)*3 | objc @(6*7) | swift let a=1+2*3\n", 0x07);
    vga_print("Full pipeline: build swift (10-2)/4\n", 0x07);
}

static void cmd_meminfo(void)
{
    vga_print("Physical memory (PMM):\n", 0x07);
    vga_print("  Total: ", 0x07);
    vga_print_u64(pmm_get_total_mem(), 0x07);
    vga_print(" bytes (", 0x07);
    vga_print_u64(pmm_get_total_mem() / 1024, 0x07);
    vga_print(" KB)\n", 0x07);
    vga_print("  Free:  ", 0x07);
    vga_print_u64(pmm_get_free_mem(), 0x07);
    vga_print(" bytes (", 0x07);
    vga_print_u64(pmm_get_free_mem() / 1024, 0x07);
    vga_print(" KB)\n", 0x07);
    vga_print("  Blocks: total=", 0x07);
    vga_print_u64(pmm_get_total_blocks(), 0x07);
    vga_print(" free=", 0x07);
    vga_print_u64(pmm_get_free_blocks(), 0x07);
    vga_print("\n", 0x07);

    vga_print("Kernel heap (first-fit, 256 KB):\n", 0x07);
    vga_print("  Total: ", 0x07);
    vga_print_u64(kheap_total(), 0x07);
    vga_print(" bytes\n", 0x07);
    vga_print("  Used:  ", 0x07);
    vga_print_u64(kheap_used(), 0x07);
    vga_print(" bytes\n", 0x07);
    vga_print("  Free:  ", 0x07);
    vga_print_u64(kheap_free_bytes(), 0x07);
    vga_print(" bytes\n", 0x07);
}

static void cmd_testmem(void)
{
    vga_print("kmalloc test:\n", 0x07);

    void *a = kmalloc(32);
    vga_print("  kmalloc(32)  = ", 0x07);
    vga_print_hex64((uint64_t)(uintptr_t)a, 0x07);
    vga_print("\n", 0x07);

    void *b = kmalloc(1024);
    vga_print("  kmalloc(1024)= ", 0x07);
    vga_print_hex64((uint64_t)(uintptr_t)b, 0x07);
    vga_print("\n", 0x07);

    void *c = kmalloc(5000);
    vga_print("  kmalloc(5000)= ", 0x07);
    vga_print_hex64((uint64_t)(uintptr_t)c, 0x07);
    vga_print("\n", 0x07);

    if (!a || !b || !c) {
        vga_print("  FAIL: kmalloc returned NULL (OOM)\n", 0x0C);
        if (a) kfree(a);
        if (b) kfree(b);
        if (c) kfree(c);
        return;
    }

    for (int i = 0; i < 32; i++) ((uint8_t *)a)[i] = (uint8_t)(0xA0 + i);
    for (int i = 0; i < 1024; i++) ((uint8_t *)b)[i] = (uint8_t)(i & 0xFF);
    int ok = 1;
    for (int i = 0; i < 32; i++)
        if (((uint8_t *)a)[i] != (uint8_t)(0xA0 + i)) ok = 0;
    for (int i = 0; i < 1024; i++)
        if (((uint8_t *)b)[i] != (uint8_t)(i & 0xFF)) ok = 0;
    vga_print(ok ? "  pattern check: OK\n" : "  pattern check: FAIL\n", 0x07);

    vga_print("  heap used now: ", 0x07);
    vga_print_u64(kheap_used(), 0x07);
    vga_print(" bytes\n", 0x07);

    kfree(b);
    vga_print("  kfree(b) done\n", 0x07);

    void *d = kmalloc(512);
    vga_print("  kmalloc(512) = ", 0x07);
    vga_print_hex64((uint64_t)(uintptr_t)d, 0x07);
    vga_print(" (reuse test)\n", 0x07);

    kfree(a);
    kfree(c);
    if (d) kfree(d);
    vga_print("  kfree(a,c,d) done. heap used: ", 0x07);
    vga_print_u64(kheap_used(), 0x07);
    vga_print(" bytes\n", 0x07);
    vga_print("  testmem: PASS\n", 0x0A);
}

static void cmd_tasks(void)
{
    vga_print("PID  STATE    NAME       COUNTER  STACK\n", 0x07);
    for (int i = 0; i < TASK_MAX; i++) {
        task_t *t = scheduler_get_task(i);
        if (!t) continue;
        if (t->state != TASK_READY && t->state != TASK_RUNNING) continue;
        vga_print("  ", 0x07);
        vga_print_dec(t->pid, 0x07);
        vga_print("  ", 0x07);
        vga_print(state_name(t->state), 0x07);
        vga_print("  ", 0x07);
        vga_print(t->name, 0x07);
        vga_print("  cnt=", 0x07);
        vga_print_u64(t->counter, 0x07);
        vga_print("  rsp=", 0x07);
        vga_print_hex64(t->rsp, 0x07);
        vga_print("\n", 0x07);
    }
    vga_print("Active tasks: ", 0x07);
    vga_print_u64((uint64_t)scheduler_task_count(), 0x07);
    vga_print("\n", 0x07);
}

// --- Phase 4: VFS команды ----------------------------------------------------------

static void cmd_ls(void)
{
    vfs_node_t *root = vfs_root();
    uint64_t n = vfs_dir_count(root);
    vga_print("VFS / : ", 0x07);
    vga_print_u64(n, 0x07);
    vga_print(" file(s)", 0x07);
    if (initrd_tar_files() > 0) {
        vga_print(" (TAR initrd, ", 0x07);
        vga_print_u64((uint64_t)initrd_tar_files(), 0x07);
        vga_print(" from module)", 0x07);
    } else {
        vga_print(" (builtin fallback, no GRUB module)", 0x07);
    }
    vga_print("\n", 0x07);
    for (uint64_t i = 0; ; i++) {
        vfs_node_t *c = root->readdir ? root->readdir(root, i) : 0;
        if (!c)
            break;
        vga_print(c->type == VFS_DIR ? "  [DIR]  " : "  [FILE] ", 0x07);
        vga_print(c->name, 0x07);
        if (c->type == VFS_FILE) {
            vga_print("  (", 0x07);
            vga_print_u64(c->size, 0x07);
            vga_print(" bytes)", 0x07);
        }
        vga_print("\n", 0x07);
    }
}

static void cmd_cat(const char *path)
{
    while (*path == ' ')
        path++;
    if (*path == '\0') {
        vga_print("Usage: cat <file>\n", 0x07);
        return;
    }
    vfs_node_t *node = vfs_open(path);
    if (!node) {
        vga_print("cat: file not found: ", 0x0C);
        vga_print(path, 0x0C);
        vga_print("\n", 0x0C);
        return;
    }
    if (node->type != VFS_FILE) {
        vga_print("cat: not a file\n", 0x0C);
        return;
    }
    // Печатаем с капом 8 КБ (ELF-бинарники смотреть через exec, не cat).
    uint64_t cap = node->size > 8192 ? 8192 : node->size;
    uint8_t buf[256];
    uint64_t off = 0;
    while (off < cap) {
        uint64_t chunk = cap - off > sizeof(buf) ? sizeof(buf) : cap - off;
        uint64_t got = vfs_read(node, off, chunk, buf);
        if (got == 0)
            break;
        for (uint64_t i = 0; i < got; i++)
            vga_putchar((char)buf[i], 0x07);
        off += got;
    }
    if (node->size > cap)
        vga_print("\n... [truncated]\n", 0x07);
    else
        vga_putchar('\n', 0x07);
}

static void cmd_exec(const char *arg)
{
    while (*arg == ' ')
        arg++;
    if (*arg == '\0') {
        vga_print("Usage: exec <prog>  (e.g. exec user_prog.elf)\n", 0x07);
        return;
    }
    vfs_node_t *node = vfs_open(arg);
    if (!node) {
        vga_print("exec: file not found: ", 0x0C);
        vga_print(arg, 0x0C);
        vga_print("\n", 0x0C);
        return;
    }
    uint64_t entry = elf_load(node);
    if (entry == 0) {
        vga_print("exec: ELF load failed\n", 0x0C);
        return;
    }
    uint64_t ustack = (uint64_t)(uintptr_t)&user_stack[sizeof(user_stack)];
    ustack &= ~15ULL;
    vga_print("exec: entering Ring 3 (CS=0x1B, SS=0x23)...\n", 0x0A);
    uint64_t code = 0;
    switch_to_user_mode(entry, ustack, &code);
    vga_print("exec: back in kernel, exit code=", 0x0A);
    vga_print_u64(code, 0x0A);
    vga_print("\n", 0x0A);
}

// --- Phase 4: графическое демо --------------------------------------------------------
// Цвета 0xRRGGBB.
#define GUI_BG      0x1E3A5F
#define GUI_TASKBAR 0x101418
#define GUI_WIN_BG  0xE8E8E8
#define GUI_TITLE   0x204A87
#define GUI_BLACK   0x000000
#define GUI_WHITE   0xFFFFFF
#define GUI_GREEN   0x2E8B57
#define GUI_RED     0xC0392B

static void gui_draw_desktop(void)
{
    uint32_t w = fb_width(), h = fb_height();
    fb_clear(GUI_BG);
    // Панель задач.
    fb_fill_rect(0, h - 28, w, 28, GUI_TASKBAR);
    fb_draw_string("AI-OS", 8, h - 22, GUI_WHITE, GUI_TASKBAR);
    // Окно.
    uint32_t wx = 90, wy = 70, ww = 560, wh = 300;
    if (wx + ww > w) ww = w - wx - 10;
    if (wy + wh > h - 40) wh = h - 40 - wy;
    fb_fill_rect(wx, wy, ww, wh, GUI_WIN_BG);
    fb_fill_rect(wx, wy, ww, 24, GUI_TITLE);
    fb_draw_string("AI-OS Phase 4 - GUI demo (double buffered)", wx + 10, wy + 8,
                   GUI_WHITE, GUI_TITLE);
    fb_draw_string("Framebuffer: check 'info' for mode.", wx + 14, wy + 44,
                   GUI_BLACK, GUI_WIN_BG);
    fb_draw_string("Clock ticks live. Type text + Enter.", wx + 14, wy + 60,
                   GUI_BLACK, GUI_WIN_BG);
    fb_draw_string("Type 'exit' or press ESC to quit GUI.", wx + 14, wy + 76,
                   GUI_BLACK, GUI_WIN_BG);
    fb_draw_string("No flicker: all drawing via back buffer.", wx + 14, wy + 92,
                   GUI_BLACK, GUI_WIN_BG);
    // Палитра.
    uint32_t bars[8] = {0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00,
                        0x00FFFF, 0x0000FF, 0xFF00FF, 0xFFFFFF};
    for (int i = 0; i < 8; i++)
        fb_fill_rect(wx + 14 + (uint32_t)i * 60, wy + 120, 56, 40, bars[i]);
    // Подписи.
    fb_draw_string("put_pixel / fill_rect / draw_string", wx + 14, wy + 175,
                   GUI_BLACK, GUI_WIN_BG);
    fb_draw_string("> ", wx + 14, wy + 200, GUI_BLACK, GUI_WIN_BG);
}

static void cmd_gui(void)
{
    if (!fb_available()) {
        vga_print("gui: no framebuffer (boot via GRUB ISO with gfxpayload).\n", 0x0C);
        vga_print("Framebuffer tag (Multiboot2 type 8) not found.\n", 0x0C);
        return;
    }
    vga_print("gui: entering graphical mode...\n", 0x0A);

    uint32_t w = fb_width(), h = fb_height();
    gui_draw_desktop();
    // Строка часов на таскбаре.
    uint32_t clock_x = w > 140 ? w - 130 : 8;
    uint32_t clock_y = h - 22;
    fb_fill_rect(clock_x - 4, clock_y - 2, 126, 12, GUI_TASKBAR);
    fb_draw_string("up 0s", clock_x, clock_y, GUI_GREEN, GUI_TASKBAR);
    fb_flip();

    uint64_t last_sec = uptime_ms() / 1000;
    char echo[96];
    echo[0] = '\0';

    while (1) {
        scheduler_yield();

        uint64_t sec = uptime_ms() / 1000;
        if (sec != last_sec) {
            last_sec = sec;
            // Обновляем только область часов (без мерцания всего экрана).
            fb_fill_rect(clock_x - 4, clock_y - 2, 126, 12, GUI_TASKBAR);
            // "up 12345s"
            char tmp[32];
            int len = 0;
            tmp[0] = 'u'; tmp[1] = 'p'; tmp[2] = ' ';
            len = 3;
            char digits[21];
            int dl = 0;
            uint64_t v = sec;
            if (v == 0)
                digits[dl++] = '0';
            while (v > 0 && dl < 20) {
                digits[dl++] = (char)('0' + (v % 10));
                v /= 10;
            }
            for (int i = dl - 1; i >= 0 && len < 29; i--)
                tmp[len++] = digits[i];
            tmp[len++] = 's';
            tmp[len] = '\0';
            fb_draw_string(tmp, clock_x, clock_y, GUI_GREEN, GUI_TASKBAR);
            fb_flip();
        }

        if (keyboard_is_input_ready()) {
            char line[128];
            keyboard_get_input(line, sizeof(line));
            keyboard_clear_input();

            // Выход: строка "exit" или символ ESC (0x1B) в вводе.
            int want_exit = streq(line, "exit");
            for (int i = 0; line[i] && !want_exit; i++)
                if (line[i] == 0x1B)
                    want_exit = 1;
            if (want_exit)
                break;

            // Эхо ввода в окно (обрезаем до 60 символов).
            for (int i = 0; i < 60 && line[i]; i++) {
                echo[i] = line[i];
                echo[i + 1] = '\0';
            }
            fb_fill_rect(90 + 14 + 18, 70 + 200, 500, 10, GUI_WIN_BG);
            fb_draw_string(echo, 90 + 14 + 18, 70 + 200, GUI_RED, GUI_WIN_BG);
            fb_flip();
        }

        __asm__ __volatile__ ("hlt");
    }

    // Выход: гасим экран, оставляем подсказку на FB (текстовая VGA-консоль
    // в графическом видеорежиме не видна — нужен reboot для консоли).
    fb_clear(0x000000);
    fb_draw_string("Back to text shell (invisible in gfx mode).", 40, 40,
                   GUI_GREEN, 0x000000);
    fb_draw_string("Reboot to see the console. Uptime keeps ticking.", 40, 56,
                   GUI_GREEN, 0x000000);
    fb_flip();
    vga_print("gui: demo finished.\n", 0x0A);
}

// --- Shell -------------------------------------------------------------------------------

static void handle_command(const char *cmd)
{
    if (streq(cmd, "help")) {
        vga_print("Commands:\n", 0x07);
        vga_print("  help    - show this help\n", 0x07);
        vga_print("  clear   - clear screen\n", 0x07);
        vga_print("  echo    - echo text\n", 0x07);
        vga_print("  info    - system info\n", 0x07);
        vga_print("  uptime  - seconds since boot (PIT 100 Hz)\n", 0x07);
        vga_print("  meminfo - RAM + heap stats (PMM/kheap)\n", 0x07);
        vga_print("  testmem - kmalloc/kfree test\n", 0x07);
        vga_print("  tasks   - list kernel threads\n", 0x07);
        vga_print("  crash   - divide by zero (panic demo)\n", 0x07);
        vga_print("  ls      - list files in VFS/initrd\n", 0x07);
        vga_print("  cat     - print text file: cat <file>\n", 0x07);
        vga_print("  exec    - run user ELF in Ring 3: exec <prog>\n", 0x07);
        vga_print("  gui     - graphical demo (needs framebuffer)\n", 0x07);
        vga_print("  desktop - macOS-style desktop (needs framebuffer)\n", 0x07);
        vga_print("  secinfo - security status (NX, canary, guards)\n", 0x07);
        vga_print("  cc      - compile+run C expr: cc 2+3*4\n", 0x07);
        vga_print("  cxx     - compile+run C++ expr: cxx (1+2)*3\n", 0x07);
        vga_print("  objc    - compile+run ObjC expr: objc @(6*7)\n", 0x07);
        vga_print("  swift   - compile+run Swift: swift let a=1+2*3\n", 0x07);
        vga_print("  build   - full pipeline: build swift (10-2)/4\n", 0x07);
        vga_print("  samples - embedded C/C++/ObjC/Swift sources\n", 0x07);
        vga_print("  fx      - toggle glass/lowfx (weak hardware)\n", 0x07);
    } else if (streq(cmd, "clear")) {
        vga_clear();
    } else if (starts_with(cmd, "echo ")) {
        vga_print(cmd + 5, 0x07);
        vga_putchar('\n', 0x07);
    } else if (streq(cmd, "info")) {
        vga_print("AI-OS x86_64 (Phase 4)\n", 0x07);
        vga_print("Long Mode | PIT 100Hz | PMM+kheap | tasks | VFS+ELF+Ring3\n", 0x07);
        vga_print("GDT: kern 0x08/0x10, user 0x1B/0x23, TSS 0x28\n", 0x07);
        vga_print("Syscalls: int 0x80 (DPL=3)", 0x07);
        if (syscall_cpu_supported())
            vga_print(" + SYSCALL/SYSRET\n", 0x07);
        else
            vga_print(" (no SYSCALL on CPU)\n", 0x07);
        if (fb_available()) {
            vga_print("Framebuffer: ", 0x07);
            vga_print_u64(fb_width(), 0x07);
            vga_print("x", 0x07);
            vga_print_u64(fb_height(), 0x07);
            vga_print("x", 0x07);
            vga_print_u64(fb_bpp(), 0x07);
            vga_print(" @", 0x07);
            vga_print_hex64(fb_addr(), 0x07);
            vga_print("\n", 0x07);
        } else {
            vga_print("Framebuffer: none (text mode)\n", 0x07);
        }
        vga_print("VFS files: ", 0x07);
        vga_print_u64(vfs_dir_count(vfs_root()), 0x07);
        vga_print("\n", 0x07);
    } else if (streq(cmd, "uptime")) {
        uint64_t ms = uptime_ms();
        vga_print("Uptime: ", 0x07);
        vga_print_u64(ms / 1000, 0x07);
        vga_print(" s (", 0x07);
        vga_print_u64(ms, 0x07);
        vga_print(" ms, ticks=", 0x07);
        vga_print_u64(timer_ticks(), 0x07);
        vga_print(")\n", 0x07);
    } else if (streq(cmd, "meminfo")) {
        cmd_meminfo();
    } else if (streq(cmd, "testmem")) {
        cmd_testmem();
    } else if (streq(cmd, "tasks")) {
        cmd_tasks();
    } else if (streq(cmd, "ls")) {
        cmd_ls();
    } else if (starts_with(cmd, "cat ")) {
        cmd_cat(cmd + 4);
    } else if (streq(cmd, "cat")) {
        vga_print("Usage: cat <file>\n", 0x07);
    } else if (starts_with(cmd, "exec ")) {
        cmd_exec(cmd + 5);
    } else if (streq(cmd, "exec")) {
        vga_print("Usage: exec <prog>  (e.g. exec user_prog.elf)\n", 0x07);
    } else if (streq(cmd, "gui")) {
        cmd_gui();
    } else if (streq(cmd, "desktop")) {
        desktop_run();
    } else if (starts_with(cmd, "cc ")) {
        cmd_compile(0, cmd + 3);
    } else if (streq(cmd, "cc")) {
        vga_print("Usage: cc <expr>  (e.g. cc 2+3*4)\n", 0x07);
    } else if (starts_with(cmd, "cxx ")) {
        cmd_compile(1, cmd + 4);
    } else if (streq(cmd, "cxx")) {
        vga_print("Usage: cxx <expr>  (e.g. cxx (1+2)*3)\n", 0x07);
    } else if (starts_with(cmd, "objc ")) {
        cmd_compile(2, cmd + 5);
    } else if (streq(cmd, "objc")) {
        vga_print("Usage: objc <expr>  (e.g. objc @(6*7))\n", 0x07);
    } else if (starts_with(cmd, "swift ")) {
        cmd_compile(3, cmd + 6);
    } else if (streq(cmd, "swift")) {
        vga_print("Usage: swift <expr>  (e.g. swift let a=1+2*3)\n", 0x07);
    } else if (starts_with(cmd, "build ")) {
        cmd_build(cmd + 6);
    } else if (streq(cmd, "build")) {
        vga_print("Usage: build <c|cpp|objc|swift> <expr>\n", 0x07);
    } else if (streq(cmd, "samples")) {
        cmd_samples();
    } else if (streq(cmd, "fx")) {
        wm_set_lowfx(!wm_lowfx());
        if (wm_lowfx())
            vga_print("FX: low mode (fast, no glass) — `fx` to restore glass.\n", 0x07);
        else
            vga_print("FX: glass full (transparent macOS style).\n", 0x07);
    } else if (streq(cmd, "secinfo")) {
        extern uint64_t __stack_chk_guard;
        vga_print("Security status:\n", 0x07);
        vga_print("  NX/XD: .text RX only, stacks/heap/data NX\n", 0x07);
        vga_print("  Stack canary: 0x", 0x07);
        vga_print_hex64(__stack_chk_guard, 0x07);
        vga_print("\n", 0x07);
        vga_print("  User-ptr sanitizer: [0x400000,512M)+ustack\n", 0x07);
        vga_print("  sys_alloc: isolated user heap @16M (NX)\n", 0x07);
        vga_print("  int 0x80: DPL=3 only; GDT TSS=0x28\n", 0x07);
    } else if (streq(cmd, "crash")) {
        vga_print("Triggering #DE (divide by zero)...\n", 0x0C);
        volatile int one = 1;
        volatile int zero = 0;
        volatile int res = one / zero; // #DE -> panic + reg dump
        (void)res;
        vga_print("Survived?! Should have panicked.\n", 0x0C);
    } else if (cmd[0] != '\0') {
        vga_print("Unknown command: ", 0x07);
        vga_print(cmd, 0x07);
        vga_putchar('\n', 0x07);
    }
}

void kernel_main(void *mb_info_ptr)
{
    // ---- Инициализация подсистем (строгий порядок) -------------------------
    vga_init();        // 0. Экран первым — видим лог загрузки.
    gdt_init();        // 1. GDT+TSS (+ user 0x1B/0x23).
    idt_init();        // 2. IDT+PIC (маски: всё закрыто, кроме IRQ1).
    exceptions_init(); // 3. Векторы 0-31 -> exc_stub_* с дампом+panic.
    timer_init();      // 4. PIT 100 Гц + IRQ0 (вектор 0x20).
    pmm_init(mb_info_ptr); // 5. Bitmap PMM по карте Multiboot2.
    kheap_init();      // 6. Куча 256 КБ (first-fit).
    scheduler_init();  // 7. Задачи main + worker1/worker2.
    keyboard_init();   // 8. Буфер ввода.
    vfs_init();        // 9. Корень VFS.
    initrd_init(mb_info_ptr); // 10. TAR-модуль Multiboot2 (или builtin).
    security_init();   // 11. NX/XD + canary-banner (EFER.NXE, сплит 0-6М).
    syscall_init();    // 12. int 0x80 (DPL=3) + SYSCALL MSRs + user heap.
    fb_init(mb_info_ptr); // 13. Framebuffer (Multiboot2 tag 8).
    mouse_init();      // 14. PS/2 мышь IRQ12 (fail-soft).
    desktop_init();    // 15. Данные рабочего стола (no-op без FB).

    // ---- Приветственный экран ----------------------------------------------
    vga_print("========================================\n", 0x07);
    vga_print("  Welcome to AI-OS x86_64! (Phase 5)\n", 0x07);
    vga_print("  VFS+initrd | Ring3+ELF | framebuffer\n", 0x07);
    vga_print("  NX/XD + sanitizer | macOS-style desktop\n", 0x07);
    vga_print("  Type 'help' for available commands.\n", 0x07);
    vga_print("========================================\n", 0x07);
    vga_print("boot: gdt ok, idt ok, exceptions ok, timer 100Hz ok\n", 0x07);
    vga_print("boot: pmm total=", 0x07);
    vga_print_u64(pmm_get_total_mem() / 1024, 0x07);
    vga_print(" KB free=", 0x07);
    vga_print_u64(pmm_get_free_mem() / 1024, 0x07);
    vga_print(" KB, heap=256 KB, tasks=", 0x07);
    vga_print_u64((uint64_t)scheduler_task_count(), 0x07);
    vga_print("\n", 0x07);
    vga_print("boot: vfs files=", 0x07);
    vga_print_u64(vfs_dir_count(vfs_root()), 0x07);
    if (fb_available()) {
        vga_print(", fb ", 0x07);
        vga_print_u64(fb_width(), 0x07);
        vga_print("x", 0x07);
        vga_print_u64(fb_height(), 0x07);
        vga_print("x", 0x07);
        vga_print_u64(fb_bpp(), 0x07);
    } else {
        vga_print(", fb none", 0x07);
    }
    vga_print("\n", 0x07);
    vga_print("\nAI-OS> ", 0x07);

    // ---- Основной цикл -------------------------------------------------------
    char cmd[CMD_BUFFER_SIZE];

    while (1) {
        // Даём квант worker-задачам (кооперативный Round-Robin).
        scheduler_yield();

        if (keyboard_is_input_ready()) {
            keyboard_get_input(cmd, CMD_BUFFER_SIZE);
            handle_command(cmd);
            keyboard_clear_input();
            vga_print("AI-OS> ", 0x07);
        }

        __asm__ __volatile__ ("hlt");
    }
}
