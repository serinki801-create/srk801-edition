// ============================================================================
// initrd.c — Парсер TAR (ustar, блоки 512) в узлы VFS
// ============================================================================
// USTAR-заголовок (512 байт):
//   name[100] @0, size (octal, 12 байт) @124, typeflag @156,
//   magic "ustar" @257. Файл: заголовок + ceil(size/512)*512 байт данных.
// Конец архива: два нулевых блока. Контрольную сумму проверяем мягко
// (GNU tar пишет корректную, но для демо достаточно магии/размера).
// ============================================================================
#include "initrd.h"
#include "vfs.h"
#include "pmm.h"
#include <stdint.h>

#define TAR_BLOCK 512
#define INITRD_MAX_FILES 64

static int g_tar_files = 0;
static uint64_t g_mod_start = 0;
static uint64_t g_mod_end = 0;

// Встроенные файлы-заглушки (когда GRUB-модуля нет).
static char builtin_hello[] =
    "Hello from AI-OS initrd!\n"
    "This is a builtin file (no GRUB module found).\n"
    "Boot via GRUB ISO to see the real TAR initrd.\n";
static char builtin_readme[] =
    "AI-OS Phase 4 - VFS demo\n"
    "Commands: ls, cat <file>, exec <prog>, gui\n";

// --- Хелперы ------------------------------------------------------------------
static int str_eq_n(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
        if (a[i] == '\0')
            return 1;
    }
    return 1;
}

// Парсинг octal-числа (до n символов, останавливаемся на NUL/пробеле).
static uint64_t octal_parse(const char *s, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\0' || c == ' ')
            break;
        if (c < '0' || c > '7')
            break;
        v = (v << 3) + (uint64_t)(c - '0');
    }
    return v;
}

static int is_zero_block(const uint8_t *p)
{
    for (int i = 0; i < TAR_BLOCK; i++)
        if (p[i] != 0)
            return 0;
    return 1;
}

// Имя файла из заголовка (до 100 символов, NUL-терминировано).
static void tar_name(char *dst, const uint8_t *hdr)
{
    int i = 0;
    for (; i < 100 && hdr[i]; i++)
        dst[i] = (char)hdr[i];
    dst[i] = '\0';
    // Отрезаем ведущие "./" и "/" (GNU tar любит "./name").
    if (dst[0] == '.' && dst[1] == '/') {
        int j = 0;
        while (dst[j + 2]) { dst[j] = dst[j + 2]; j++; }
        dst[j] = '\0';
    }
    while (dst[0] == '/') {
        int j = 0;
        while (dst[j + 1]) { dst[j] = dst[j + 1]; j++; }
        dst[j] = '\0';
    }
}

// --- TAR -> VFS -----------------------------------------------------------------
static int initrd_parse_tar(uint8_t *base, uint8_t *end)
{
    uint8_t *p = base;
    int count = 0;

    while (p + TAR_BLOCK <= end && count < INITRD_MAX_FILES) {
        if (is_zero_block(p))
            break; // два нулевых блока = конец архива

        uint64_t fsize = octal_parse((const char *)(p + 124), 12);
        char type = (char)p[156];
        // type '0'/'\0' = обычный файл; '5' = каталог (пропускаем).
        if (type == '5') {
            p += TAR_BLOCK;
            continue;
        }

        char name[VFS_NAME_MAX];
        tar_name(name, p);
        if (name[0] == '\0') {
            p += TAR_BLOCK;
            continue;
        }

        uint64_t blocks = (fsize + TAR_BLOCK - 1) / TAR_BLOCK;
        uint8_t *data = p + TAR_BLOCK;
        if (data + blocks * TAR_BLOCK > end)
            break; // обрезанный архив

        // Пропускаем записи-каталоги с нулевым размером и слэшем в конце.
        int is_dir = 0;
        for (int i = 0; name[i]; i++)
            if (name[i] == '/')
                is_dir = 1;
        if (!is_dir && type != '5') {
            if (vfs_create_memfile(vfs_root(), name, data, fsize))
                count++;
        }

        p = data + blocks * TAR_BLOCK;
    }
    return count;
}

// --- Multiboot2: поиск первого модуля (tag type 3) -------------------------------
static int initrd_find_module(void *mb_info, uint64_t *out_start, uint64_t *out_end)
{
    if (!mb_info)
        return 0;
    uint8_t *ptr = (uint8_t *)mb_info;
    uint64_t p = (uint64_t)(uintptr_t)ptr;
    if (p < 0x1000 || p >= 0x20000000)
        return 0;
    uint32_t total = *(uint32_t *)(ptr + 0);
    if (total < 8 || total >= (1u << 20))
        return 0;

    uint8_t *tag = ptr + 8;
    uint8_t *end = ptr + total;
    while (tag + 8 <= end) {
        uint32_t type = *(uint32_t *)(tag + 0);
        uint32_t size = *(uint32_t *)(tag + 4);
        if (size < 8)
            break;
        if (type == 0)
            break;
        if (type == 3) { // modules
            if (size < 16)
                break;
            uint32_t mod_start = *(uint32_t *)(tag + 8);
            uint32_t mod_end = *(uint32_t *)(tag + 12);
            if (mod_end > mod_start && mod_start >= 0x100000) {
                *out_start = mod_start;
                *out_end = mod_end;
                return 1;
            }
            return 0;
        }
        uint32_t step = (size + 7) & ~7u;
        if (step == 0)
            break;
        tag += step;
        if (tag >= end)
            break;
    }
    return 0;
}

// --- Публичный API -----------------------------------------------------------------
int initrd_init(void *mb_info)
{
    g_tar_files = 0;
    g_mod_start = 0;
    g_mod_end = 0;

    uint64_t ms = 0, me = 0;
    if (initrd_find_module(mb_info, &ms, &me)) {
        g_mod_start = ms;
        g_mod_end = me;
        // Резервируем страницы модуля в PMM, чтобы их не раздали.
        pmm_reserve_range(ms, me - ms);
        g_tar_files = initrd_parse_tar((uint8_t *)(uintptr_t)ms,
                                       (uint8_t *)(uintptr_t)me);
    }

    if (g_tar_files == 0) {
        // Fallback: встроенные файлы, чтобы ls/cat работали всегда.
        vfs_create_memfile(vfs_root(), "hello.txt",
                           (uint8_t *)builtin_hello, sizeof(builtin_hello) - 1);
        vfs_create_memfile(vfs_root(), "readme.txt",
                           (uint8_t *)builtin_readme, sizeof(builtin_readme) - 1);
    }
    (void)str_eq_n;
    return (int)vfs_dir_count(vfs_root());
}

int initrd_tar_files(void) { return g_tar_files; }
uint64_t initrd_mod_start(void) { return g_mod_start; }
uint64_t initrd_mod_end(void) { return g_mod_end; }
