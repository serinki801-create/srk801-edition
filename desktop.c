// ============================================================================
// desktop.c — рабочий стол с приложениями в оформлении Darling AppKit
// ============================================================================
// Приложения:
//   Terminal — интерактивный (help/ls/cat/echo/clear/meminfo/version/exec/exit),
//              тёмный Pro-фон, зелёный prompt "abzal@ai-os ~ %", блок-курсор.
//   Finder (VFS) — светлый список: штриховка alternatingContentBackground,
//                  синее выделение строки (selectedContentBackground), маркеры
//                  папки/файла; клик по файлу — cat в Terminal.
//   About AI-OS (Sys Info) — системные данные.
// Menu bar (Darling): File/Edit/View/Go/Window/Help — dropdown-меню с actions.
// Управление: мышь (окна, светофоры, меню, Dock, tooltip) и клавиатура
// (Tab — фокус, 1-5 — Dock, Enter на Dock — запустить; если фокус на
// Terminal — печатные строки идут в терминал; ESC — закрыть меню / выход).
// ============================================================================
#include "desktop.h"
#include "wm.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "mouse.h"
#include "timer.h"
#include "scheduler.h"
#include "vga.h"
#include "vfs.h"
#include "pmm.h"
#include "kheap.h"
#include "elf.h"
#include "syscall.h"
#include <stdint.h>

#define TERM_BG       0x1E1E1E // Terminal Pro: тёмный фон
#define TERM_FG       0xF5F5F5
#define TERM_PROMPT   0x40C463 // зелёный prompt
#define TERM_ERR      0xFF6B6B
#define TERM_DIM      0x9E9E9E
#define FINDER_BG     0xFFFFFF
#define FOLDER_BLUE   0x3693F3 // маркер папки
#define FILE_GRAY     0x8E8E93 // маркер файла
#define ABOUT_BG      0xEDEDED
#define PROMPT_STR    "abzal@ai-os ~ % "

static int g_win_term = -1, g_win_sys = -1, g_win_files = -1;
static int g_ready = 0;
static int g_quit = 0;

// Forward decls.
static void term_render(void);
static void term_run(const char *cmd);
static void close_win_by_id(int id);
static void open_term(void);
static void open_about(void);
static void open_finder(void);
static void fill_files(void);
static int streq_line(const char *a, const char *b);
static void handle_desktop_keys(const char *line);

// --- Терминал: история строк (9 под prompt) + input ------------------------------
static char g_thist[WM_LINES - 1][WM_LINE_MAX];
static uint32_t g_thist_fg[WM_LINES - 1];
static int g_thist_n = 0;
static char g_term_input[48];

static void str_cp(char *dst, const char *src, int n)
{
    int i = 0;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void term_render(void)
{
    if (g_win_term < 0)
        return;
    wm_clear_lines(g_win_term);
    for (int i = 0; i < g_thist_n; i++) {
        wm_set_line(g_win_term, i, g_thist[i]);
        wm_set_line_fg(g_win_term, i, g_thist_fg[i]);
    }
    int p = g_thist_n;
    wm_set_line(g_win_term, p, g_term_input);
    wm_set_line_fg(g_win_term, p, TERM_FG);
    wm_set_prefix(g_win_term, p, PROMPT_STR, TERM_PROMPT);
    wm_set_cursor(g_win_term, 1);
}

static void term_push(const char *s, uint32_t fg)
{
    if (g_win_term < 0 || !s)
        return;
    if (g_thist_n >= WM_LINES - 1) {
        for (int i = 0; i < WM_LINES - 2; i++) {
            str_cp(g_thist[i], g_thist[i + 1], WM_LINE_MAX);
            g_thist_fg[i] = g_thist_fg[i + 1];
        }
        g_thist_n = WM_LINES - 2;
    }
    str_cp(g_thist[g_thist_n], s, WM_LINE_MAX);
    g_thist_fg[g_thist_n] = fg;
    g_thist_n++;
    term_render();
}

static void term_clear(void)
{
    g_thist_n = 0;
    g_term_input[0] = '\0';
    term_render();
}

// --- Команды терминали -------------------------------------------------------------
static void cmd_help(void)
{
    term_push("> Commands available:", TERM_DIM);
    term_push("  help        this list", TERM_FG);
    term_push("  ls          list / (VFS root)", TERM_FG);
    term_push("  cat <file>  print file content", TERM_FG);
    term_push("  echo <txt>  print text", TERM_FG);
    term_push("  clear       clear terminal", TERM_FG);
    term_push("  meminfo     RAM total/free", TERM_FG);
    term_push("  version     OS version", TERM_FG);
    term_push("  exec        run user_prog.elf (Ring 3)", TERM_FG);
    term_push("  exit        close terminal", TERM_FG);
}

static void cmd_ls(void)
{
    vfs_node_t *root = vfs_root();
    int n = 0;
    for (uint64_t i = 0; i < 24; i++) {
        vfs_node_t *c = root->readdir ? root->readdir(root, i) : 0;
        if (!c)
            break;
        char line[64];
        int p = 0;
        line[p++] = c->type == VFS_DIR ? 'd' : '-';
        for (int k = 0; c->name[k] && p < 48; k++)
            line[p++] = c->name[k];
        line[p++] = ' ';
        char dg[21];
        int dl = 0;
        uint64_t v = c->size;
        if (v == 0)
            dg[dl++] = '0';
        while (v > 0 && dl < 20) {
            dg[dl++] = (char)('0' + (v % 10));
            v /= 10;
        }
        for (int k = dl - 1; k >= 0 && p < 60; k--)
            line[p++] = dg[k];
        line[p] = '\0';
        term_push(line, c->type == VFS_DIR ? FOLDER_BLUE : TERM_FG);
        n++;
    }
    if (n == 0)
        term_push("  (empty)", TERM_DIM);
}

static void cmd_cat(const char *name)
{
    vfs_node_t *node = vfs_open(name);
    if (!node || node->type != VFS_FILE) {
        term_push(name, TERM_ERR);
        term_push("  cat: no such file", TERM_ERR);
        return;
    }
    char line[64];
    int p = 0;
    uint8_t buf[64];
    uint64_t got = node->data ? node->size : 0;
    if (got > 52)
        got = 52;
    if (node->data) {
        for (uint64_t i = 0; i < got; i++) {
            char c = (char)node->data[i];
            if (c == '\n' || c == '\r')
                continue;
            if (p < 58)
                line[p++] = c;
        }
    } else {
        vfs_read(node, 0, got, buf);
        for (uint64_t i = 0; i < got; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r')
                continue;
            if (p < 58)
                line[p++] = c;
        }
    }
    if (node->size > 52) {
        line[p++] = '.'; line[p++] = '.'; line[p++] = '.';
    }
    line[p] = '\0';
    term_push(line, TERM_FG);
}

static void run_exec_elf(void)
{
    char line[64];
    vfs_node_t *node = vfs_open("user_prog.elf");
    if (!node) {
        term_push("exec: user_prog.elf missing!", TERM_ERR);
        return;
    }
    uint64_t entry = elf_load(node);
    if (entry == 0) {
        term_push("exec: ELF load failed.", TERM_ERR);
        return;
    }
    static uint8_t dstack[16384] __attribute__((aligned(16)));
    uint64_t top = (uint64_t)(uintptr_t)&dstack[sizeof(dstack)];
    top &= ~15ULL;
    term_push("> running user_prog.elf (Ring 3)...", TERM_DIM);
    wm_render();
    uint64_t code = 0;
    switch_to_user_mode(entry, top, &code);
    int len = 0;
    if (code == 42) {
        str_cp(line, "exec: exit(42) OK.", sizeof(line));
    } else {
        str_cp(line, "exec: done.", sizeof(line));
    }
    (void)len;
    term_push(line, TERM_FG);
}

static void term_run(const char *cmd)
{
    // "cmd args..."
    int len = 0;
    while (cmd[len]) len++;
    int blank = 1;
    for (int i = 0; cmd[i]; i++)
        if (cmd[i] != ' ')
            blank = 0;
    if (blank) {
        // Пустой Enter: просто новый prompt.
        g_term_input[0] = '\0';
        term_render();
        return;
    }
    term_push(cmd, TERM_FG);
    if (len >= 4 && cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p')
        cmd_help();
    else if (len == 2 && cmd[0] == 'l' && cmd[1] == 's')
        cmd_ls();
    else if (len > 4 && cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ')
        cmd_cat(cmd + 4);
    else if (len > 5 && cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' &&
             cmd[3] == 'o' && cmd[4] == ' ')
        term_push(cmd + 5, TERM_FG);
    else if (len == 5 && cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'e' &&
             cmd[3] == 'a' && cmd[4] == 'r')
        term_clear();
    else if (len == 7 && cmd[0] == 'm' && cmd[1] == 'e' && cmd[2] == 'm' &&
             cmd[3] == 'i' && cmd[4] == 'n' && cmd[5] == 'f' && cmd[6] == 'o') {
        char line[64];
        uint64_t tot = pmm_get_total_mem() / 1048576ULL;
        uint64_t fre = pmm_get_free_mem() / 1048576ULL;
        // Цифры вручную (freestanding, без snprintf):
        int p = 0;
        line[p++] = ' '; line[p++] = 'R'; line[p++] = 'A'; line[p++] = 'M';
        line[p++] = ' '; line[p++] = 'T'; line[p++] = 'o'; line[p++] = 't';
        line[p++] = 'a'; line[p++] = 'l'; line[p++] = ':'; line[p++] = ' ';
        uint64_t v = tot;
        char dg[21]; int dl = 0;
        if (v == 0) dg[dl++] = '0';
        while (v > 0 && dl < 20) { dg[dl++] = (char)('0' + (v % 10)); v /= 10; }
        for (int k = dl - 1; k >= 0 && p < 56; k--) line[p++] = dg[k];
        line[p++] = ' '; line[p++] = 'M'; line[p++] = 'B'; line[p++] = ' ';
        line[p++] = '-'; line[p++] = ' '; line[p++] = 'f'; line[p++] = 'r';
        line[p++] = 'e'; line[p++] = 'e'; line[p++] = ':'; line[p++] = ' ';
        v = fre; dl = 0;
        if (v == 0) dg[dl++] = '0';
        while (v > 0 && dl < 20) { dg[dl++] = (char)('0' + (v % 10)); v /= 10; }
        for (int k = dl - 1; k >= 0 && p < 62; k--) line[p++] = dg[k];
        line[p++] = ' '; line[p++] = 'M'; line[p++] = 'B';
        line[p] = '\0';
        term_push(line, TERM_FG);
    }
    else if (len == 7 && cmd[0] == 'v' && cmd[1] == 'e' && cmd[2] == 'r' &&
             cmd[3] == 's' && cmd[4] == 'i' && cmd[5] == 'o' && cmd[6] == 'n')
        term_push("  AI-OS Phase 6 — Darling AppKit visuals", TERM_FG);
    else if (len == 4 && cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'e' && cmd[3] == 'c')
        run_exec_elf();
    else if (len == 4 && cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'i' && cmd[3] == 't') {
        close_win_by_id(g_win_term);
        g_term_input[0] = '\0';
        g_thist_n = 0;
        return;
    }
    else {
        char line[64];
        int p = 0;
        for (int k = 0; cmd[k] && p < 40; k++)
            line[p++] = cmd[k];
        const char *suf = " -- not found (try: help)";
        for (int k = 0; suf[k] && p < 62; k++)
            line[p++] = suf[k];
        line[p] = '\0';
        term_push(line, TERM_ERR);
    }
    g_term_input[0] = '\0';
    term_render();
}

// --- Окна приложений ---------------------------------------------------------------
static void open_term(void)
{
    if (g_win_term >= 0) {
        wm_restore(g_win_term);
        return;
    }
    uint32_t sw = fb_width();
    int ww = 480;
    if (ww + 200 > (int)sw)
        ww = (int)sw - 210;
    g_win_term = wm_open("Terminal - AI-OS", 60, 40, ww, 230, TERM_BG);
    wm_set_fg(g_win_term, TERM_FG);
    g_thist_n = 0;
    g_term_input[0] = '\0';
    term_push("AI-OS Terminal — Darling visuals", TERM_DIM);
    term_push("type 'help' for commands.", TERM_DIM);
    term_render();
    wm_set_running(1, 1);
}

static void open_about(void)
{
    if (g_win_sys >= 0) {
        wm_restore(g_win_sys);
        wm_focus(g_win_sys);
        return;
    }
    g_win_sys = wm_open("About AI-OS", 120, 90, 420, 210, ABOUT_BG);
    wm_set_line(g_win_sys, 0, "AI-OS");
    wm_set_line_fg(g_win_sys, 0, 0x0063E1);
    wm_set_line(g_win_sys, 1, "Version 6.0 (Phase 6)");
    wm_set_line_fg(g_win_sys, 1, 0x8E8E93);
    wm_set_line(g_win_sys, 2, "");
    wm_set_line(g_win_sys, 3, "CPU: x86_64  |  Ring 0 + Ring 3");
    wm_set_line(g_win_sys, 4, "Graphics: framebuffer (VBE/GOP)");
    wm_set_line(g_win_sys, 5, "GUI: Darling AppKit visuals");
    wm_set_line(g_win_sys, 6, "Shell: Terminal (type 'help')");
    wm_set_running(2, 1);
}

static void open_finder(void)
{
    if (g_win_files >= 0) {
        wm_restore(g_win_files);
        fill_files();
        return;
    }
    uint32_t sw = fb_width();
    if (sw < 700)
        return; // узкий экран — без Finder
    g_win_files = wm_open("Finder - VFS", 640, 60, 340, 220, FINDER_BG);
    wm_set_fg(g_win_files, 0x1A1A1A);
    wm_set_list_mode(g_win_files, 1);
    fill_files();
    wm_set_running(0, 1);
}

// Список VFS-корня в Finder (строка 0 — заголовок, дальше — записи).
static void fill_files(void)
{
    if (g_win_files < 0)
        return;
    wm_clear_lines(g_win_files);
    wm_set_line(g_win_files, 0, "/ (VFS root)");
    wm_set_line_fg(g_win_files, 0, 0x8E8E93);
    vfs_node_t *root = vfs_root();
    int l = 1;
    for (uint64_t i = 0; l < WM_LINES; i++) {
        vfs_node_t *c = root->readdir ? root->readdir(root, i) : 0;
        if (!c)
            break;
        static char line[64];
        int p = 0;
        for (int k = 0; c->name[k] && p < 40; k++)
            line[p++] = c->name[k];
        line[p++] = ' ';
        char digits[21];
        int dl = 0;
        uint64_t v = c->size;
        if (v == 0)
            digits[dl++] = '0';
        while (v > 0 && dl < 20) {
            digits[dl++] = (char)('0' + (v % 10));
            v /= 10;
        }
        line[p++] = '(';
        for (int k = dl - 1; k >= 0 && p < 60; k--)
            line[p++] = digits[k];
        if (p < 62) {
            line[p++] = 'B';
            line[p++] = ')';
        }
        line[p] = '\0';
        wm_set_line(g_win_files, l, line);
        wm_set_line_fg(g_win_files, l,
                       c->type == VFS_DIR ? FOLDER_BLUE : FILE_GRAY);
        l++;
    }
}

static void open_default_windows(void)
{
    open_term();
    open_about();
    open_finder();
}

// Закрыть окно по id (идёт в индекс г_*).
static void close_win_by_id(int id)
{
    if (id < 0)
        return;
    if (id == g_win_term) {
        wm_close(id);
        g_win_term = -1;
        wm_set_running(1, 0);
    } else if (id == g_win_sys) {
        wm_close(id);
        g_win_sys = -1;
        wm_set_running(2, 0);
    } else if (id == g_win_files) {
        wm_close(id);
        g_win_files = -1;
        wm_set_running(0, 0);
    }
}

// --- Клик по строке Finder -> cat в Terminal ---------------------------------------
static int finder_row_cb(int win_id, int row)
{
    if (win_id != g_win_files || row < 1)
        return 0;
    vfs_node_t *root = vfs_root();
    vfs_node_t *c = root->readdir ? root->readdir(root, row - 1) : 0;
    if (!c)
        return 0;
    if (c->type == VFS_FILE) {
        if (g_win_term < 0)
            open_term();
        char line[64];
        int p = 0;
        line[p++] = 'c'; line[p++] = 'a'; line[p++] = 't'; line[p++] = ' ';
        for (int k = 0; c->name[k] && p < 60; k++)
            line[p++] = c->name[k];
        line[p] = '\0';
        term_run(line);
    }
    return 0;
}

// --- Действия меню -------------------------------------------------------------------
static void menu_action(int a)
{
    switch (a) {
    case ACT_NEW_WINDOW:
        open_term();
        break;
    case ACT_CLOSE:
        close_win_by_id(wm_focused());
        break;
    case ACT_MINIMIZE:
        if (wm_focused() >= 0)
            wm_minimize(wm_focused());
        break;
    case ACT_ZOOM:
        if (wm_focused() >= 0)
            wm_maximize(wm_focused());
        break;
    case ACT_CLEAR:
        if (g_win_term >= 0)
            term_clear();
        break;
    case ACT_ABOUT:
        open_about();
        break;
    case ACT_HELP:
        if (g_win_term >= 0) {
            wm_restore(g_win_term);
            cmd_help();
        }
        break;
    case ACT_TOGGLE_FX:
        wm_set_lowfx(wm_lowfx() ? 0 : 1);
        break;
    case ACT_QUIT:
        g_quit = 1;
        break;
    default:
        break;
    }
}

// --- Действие Dock-иконки -------------------------------------------------------------
static void dock_activate(int idx)
{
    switch (idx) {
    case 0: // Finder
        open_finder();
        break;
    case 1: // Term
        open_term();
        break;
    case 2: // Sys
        open_about();
        break;
    case 3: // Exec
        run_exec_elf();
        break;
    case 4: // Trash: закрыть все окна
        close_win_by_id(g_win_files);
        close_win_by_id(g_win_sys);
        close_win_by_id(g_win_term);
        g_thist_n = 0;
        g_term_input[0] = '\0';
        break;
    }
}

void desktop_init(void)
{
    g_ready = 0;
    if (!fb_available())
        return;
    wm_init(fb_width(), fb_height());
    wm_set_action_cb(menu_action);
    wm_set_list_cb(finder_row_cb);
    g_ready = 1;
}

int desktop_available(void)
{
    return g_ready && fb_available();
}

void desktop_run(void)
{
    if (!desktop_available()) {
        vga_print("desktop: no framebuffer (boot via GRUB ISO).\n", 0x0C);
        return;
    }
    vga_print("desktop: entering Darling-styled desktop (ESC quits)...\n", 0x0A);

    open_default_windows();
    wm_dock_select(1);

    uint64_t last_sec = uptime_ms() / 1000;
    {
        char tmp[32] = "up 0s";
        wm_set_clock(tmp);
    }
    wm_render();

    int running = 1;
    while (running) {
        scheduler_yield();

        if (wm_tick())
            wm_render();

        // Мышь -> WM каждый кадр при наличии.
        if (mouse_present()) {
            uint32_t sw = fb_width(), sh = fb_height();
            int mx = mouse_x(), my = mouse_y();
            if (mx >= (int)sw) mx = (int)sw - 1;
            if (my >= (int)sh) my = (int)sh - 1;
            static int lx = -1, ly = -1;
            static uint8_t lb = 0;
            if (mx != lx || my != ly || mouse_buttons() != lb) {
                lx = mx; ly = my; lb = mouse_buttons();
                wm_mouse(mx, my, lb);
                wm_render();
            }
        }

        // Часы раз в секунду.
        uint64_t sec = uptime_ms() / 1000;
        if (sec != last_sec) {
            last_sec = sec;
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
            wm_set_clock(tmp);
            wm_render();
        }

        if (keyboard_is_input_ready()) {
            char line[128];
            keyboard_get_input(line, sizeof(line));
            keyboard_clear_input();

            int is_esc = 0;
            for (int i = 0; line[i]; i++)
                if (line[i] == 0x1B)
                    is_esc = 1;

            if (is_esc) {
                if (wm_menu_open() >= 0) {
                    wm_menu_close();
                    wm_render();
                } else {
                    running = 0;
                    break;
                }
            }
            else if (g_quit) {
                running = 0;
                break;
            }
            else if (g_win_term >= 0 && wm_focused() == g_win_term &&
                     (line[0] == '\0' || (line[0] >= 0x20 && line[0] <= 0x7E))) {
                // Фокус на Terminal: строка (в т.ч. пустой Enter) — в терминал.
                if (line[0] >= 0x20 && line[0] <= 0x7E)
                    term_run(line);
                else
                    term_render();
                wm_render();
            }
            else if (streq_line(line, "exit")) {
                running = 0;
                break;
            }
            else if (line[0] == '\0') {
                // Пустой Enter на выбранной иконке = запуск.
                dock_activate(wm_dock_selected());
                wm_render();
            }
            else if (line[0] >= '1' && line[0] <= '5' && line[1] == '\0') {
                wm_dock_select(line[0] - '1');
                wm_render();
            }
            else if (line[0] == '\t' || (line[0] == ' ' && line[1] == '\0')) {
                wm_focus_next();
                wm_render();
            }
            else {
                handle_desktop_keys(line);
                wm_render();
            }
        }

        if (g_quit) {
            running = 0;
            break;
        }

        __asm__ __volatile__ ("hlt");
    }

    fb_clear(0x000000);
    fb_draw_string("Back to text shell (invisible in gfx mode).", 40, 40,
                   0x30D158, 0x000000);
    fb_draw_string("Reboot to see the console.", 40, 56, 0x30D158, 0x000000);
    fb_flip();
    vga_print("desktop: session finished.\n", 0x0A);
}

// Строгое сравнение строк (мини-хелпер).
static int streq_line(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void handle_desktop_keys(const char *line)
{
    // WASD — двигать фокусное окно, QE — тоже (диагонали через combos нет,
    // держим просто): w/s/a/d = вверх/вниз/влево/вправо на 16px.
    for (int i = 0; line[i]; i++) {
        char c = line[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        switch (c) {
            case 'w': wm_move_focused(0, -16); break;
            case 's': wm_move_focused(0, 16); break;
            case 'a': wm_move_focused(-16, 0); break;
            case 'd': wm_move_focused(16, 0); break;
            case 'q': wm_move_focused(-16, -16); break;
            case 'e': wm_move_focused(16, -16); break;
            case 'z': wm_move_focused(-16, 16); break;
            case 'c': wm_move_focused(16, 16); break;
            default: break;
        }
    }
}
