#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "common.h"

typedef struct {
    char path[64];
    int first_block;
    int size;
    int used;
} FileEntry;

typedef struct {
    char data[BLOCK_COUNT][BLOCK_SIZE];
    int fat[BLOCK_COUNT];
    FileEntry files[MAX_FILES];
    int file_count;
} FileSystem;

void fs_init(void);
int fs_create(const char *path);
int fs_write(const char *path, const char *data, int size);
int fs_read(const char *path, char *buf, int size);
int fs_delete(const char *path);
void fs_list(void);

#endif
