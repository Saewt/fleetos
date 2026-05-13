#include <string.h>
#include "filesystem.h"

static FileSystem fs;

void fs_init(void) {
    memset(fs.data, 0, sizeof(fs.data));
    memset(fs.fat, -2, sizeof(fs.fat));
    memset(fs.files, 0, sizeof(fs.files));
    fs.file_count = 0;
    /* TODO: Create pre-defined directories: /missions/, /logs/, /config/ */
}

int fs_create(const char *path) {
    (void)path;
    /* TODO: Create new file entry */
    return -1;
}

int fs_write(const char *path, const char *data, int size) {
    (void)path;
    (void)data;
    (void)size;
    /* TODO: Write data to file blocks */
    return -1;
}

int fs_read(const char *path, char *buf, int size) {
    (void)path;
    (void)buf;
    (void)size;
    /* TODO: Read data from file blocks */
    return -1;
}

int fs_delete(const char *path) {
    (void)path;
    /* TODO: Delete file and free blocks */
    return -1;
}

void fs_list(void) {
    /* TODO: List all files in filesystem */
}
