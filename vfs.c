// ============================================================================
// vfs.c — Virtual File System: корень, поиск по пути, чтение файлов в памяти
// ============================================================================
#include "vfs.h"
#include "kheap.h"
#include <stdint.h>

static vfs_node_t g_root;

static void str_copy(char *dst, const char *src, int n)
{
    int i = 0;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

// --- Дефолтные операции для файлов в памяти ---------------------------------
static uint64_t memfile_read(vfs_node_t *node, uint64_t off, uint64_t n, uint8_t *buf)
{
    if (!node || !buf)
        return 0;
    if (off >= node->size)
        return 0;
    if (off + n > node->size)
        n = node->size - off;
    for (uint64_t i = 0; i < n; i++)
        buf[i] = node->data[off + i];
    return n;
}

static uint64_t memfile_write(vfs_node_t *node, uint64_t off, uint64_t n, const uint8_t *buf)
{
    (void)node; (void)off; (void)n; (void)buf;
    return 0; // initrd/builtin — только чтение
}

static int memfile_open(vfs_node_t *node)  { (void)node; return 0; }
static int memfile_close(vfs_node_t *node) { (void)node; return 0; }

static vfs_node_t *dir_readdir(vfs_node_t *dir, uint64_t index)
{
    if (!dir || dir->type != VFS_DIR)
        return 0;
    vfs_node_t *c = dir->child;
    while (c && index > 0) { c = c->next; index--; }
    return c;
}

// --- Публичный API ------------------------------------------------------------
void vfs_init(void)
{
    str_copy(g_root.name, "/", VFS_NAME_MAX);
    g_root.type = VFS_DIR;
    g_root.size = 0;
    g_root.data = 0;
    g_root.read = 0;
    g_root.write = 0;
    g_root.open = memfile_open;
    g_root.close = memfile_close;
    g_root.readdir = dir_readdir;
    g_root.next = 0;
    g_root.child = 0;
}

vfs_node_t *vfs_root(void)
{
    return &g_root;
}

void vfs_add_child(vfs_node_t *dir, vfs_node_t *node)
{
    if (!dir || !node || dir->type != VFS_DIR)
        return;
    node->next = 0;
    if (!dir->child) {
        dir->child = node;
        return;
    }
    vfs_node_t *c = dir->child;
    while (c->next)
        c = c->next;
    c->next = node;
}

vfs_node_t *vfs_open(const char *path)
{
    if (!path || !*path)
        return 0;
    // Пропускаем ведущие '/'.
    while (*path == '/')
        path++;
    if (*path == '\0')
        return &g_root; // "/" — сам корень

    // Берём первый компонент (вложенных каталогов у initrd нет, но парсим).
    char comp[VFS_NAME_MAX];
    int i = 0;
    while (path[i] && path[i] != '/' && i + 1 < VFS_NAME_MAX) {
        comp[i] = path[i];
        i++;
    }
    comp[i] = '\0';

    vfs_node_t *c = g_root.child;
    while (c) {
        if (str_eq(c->name, comp)) {
            // Есть продолжение пути — спускаемся только в каталог.
            const char *rest = path + i;
            while (*rest == '/')
                rest++;
            if (*rest == '\0')
                return c;
            if (c->type == VFS_DIR) {
                // Рекурсивно ищем внутри (одноуровневая рекурсия через цикл).
                vfs_node_t *d = c->child;
                while (d) {
                    if (str_eq(d->name, rest))
                        return d;
                    d = d->next;
                }
                return 0;
            }
            return 0;
        }
        c = c->next;
    }
    return 0;
}

uint64_t vfs_read(vfs_node_t *node, uint64_t off, uint64_t n, uint8_t *buf)
{
    if (!node || node->type != VFS_FILE || !node->read)
        return 0;
    return node->read(node, off, n, buf);
}

uint64_t vfs_dir_count(vfs_node_t *dir)
{
    if (!dir || dir->type != VFS_DIR)
        return 0;
    uint64_t n = 0;
    vfs_node_t *c = dir->child;
    while (c) { n++; c = c->next; }
    return n;
}

vfs_node_t *vfs_create_memfile(vfs_node_t *dir, const char *name,
                               uint8_t *data, uint64_t size)
{
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node)
        return 0;
    str_copy(node->name, name, VFS_NAME_MAX);
    node->type = VFS_FILE;
    node->size = size;
    node->data = data;
    node->read = memfile_read;
    node->write = memfile_write;
    node->open = memfile_open;
    node->close = memfile_close;
    node->readdir = 0;
    node->next = 0;
    node->child = 0;
    vfs_add_child(dir, node);
    return node;
}
