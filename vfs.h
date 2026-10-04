// ============================================================================
// vfs.h — Virtual File System: абстракция узлов (файл/каталог)
// ============================================================================
// Каждый узел несёт указатели на операции read/write/open/close/readdir.
// Поверх VFS монтируется Initrd (TAR) и встроенные файлы-заглушки.
// Пути: "/hello.txt" или "hello.txt" (от корня), макс. 128 символов.
// ============================================================================
#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#define VFS_NAME_MAX 128

typedef enum {
    VFS_FILE = 0,
    VFS_DIR  = 1,
} vfs_type_t;

typedef struct vfs_node vfs_node_t;
struct vfs_node {
    char name[VFS_NAME_MAX];
    vfs_type_t type;
    uint64_t size;          // размер файла в байтах (для каталога 0)
    uint8_t *data;          // содержимое файла (initrd/builtin, NULL для dir)
    // Операции узла:
    uint64_t (*read)(vfs_node_t *node, uint64_t off, uint64_t n, uint8_t *buf);
    uint64_t (*write)(vfs_node_t *node, uint64_t off, uint64_t n, const uint8_t *buf);
    int (*open)(vfs_node_t *node);
    int (*close)(vfs_node_t *node);
    vfs_node_t *(*readdir)(vfs_node_t *dir, uint64_t index);
    // Связки дерева:
    vfs_node_t *next;       // следующий в том же каталоге
    vfs_node_t *child;      // первый потомок (для каталога)
};

// Инициализация VFS (создаёт пустой корень "/"). После — initrd_init().
void vfs_init(void);

// Корень дерева.
vfs_node_t *vfs_root(void);

// Добавить узел в каталог (копирует метаданные, память под vfs_node_t
// выделяет вызывающий через kmalloc).
void vfs_add_child(vfs_node_t *dir, vfs_node_t *node);

// Открыть узел по пути ("/a/b" или "a/b"). Возвращает NULL если нет.
vfs_node_t *vfs_open(const char *path);

// Прочитать из узла (дефолтная реализация для файлов в памяти).
uint64_t vfs_read(vfs_node_t *node, uint64_t off, uint64_t n, uint8_t *buf);

// Количество записей в каталоге.
uint64_t vfs_dir_count(vfs_node_t *dir);

// Создать файл в памяти (для builtin-заглушек и initrd).
// data может указывать на статическую память (не копируется).
vfs_node_t *vfs_create_memfile(vfs_node_t *dir, const char *name,
                               uint8_t *data, uint64_t size);

#endif
