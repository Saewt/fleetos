#include <string.h>
#include <stdio.h>
#include "filesystem.h"
#include "logger.h"

static FileSystem fs;

void fs_init(void) {
    memset(fs.data, 0, sizeof(fs.data));
    memset(fs.fat, -2, sizeof(fs.fat));
    memset(fs.files, 0, sizeof(fs.files));
    fs.file_count = 0;
    fs_create("/missions/");
    fs_create("/logs/");
    fs_create("/config/");
}

static int find_free_block(void) {
    for (int i = 0; i < BLOCK_COUNT; i++) {
        if (fs.fat[i] == -2) return i;
    }
    return -1;
}

static int find_file(const char *path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.files[i].used && strcmp(fs.files[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

int fs_create(const char *path) {
    if (fs.file_count >= MAX_FILES) return -1;
    if (find_file(path) >= 0) return 0;

    int idx = 0;
    for (idx = 0; idx < MAX_FILES; idx++) {
        if (!fs.files[idx].used) break;
    }
    if (idx >= MAX_FILES) return -1;

    strncpy(fs.files[idx].path, path, 63);
    fs.files[idx].path[63] = '\0';
    fs.files[idx].first_block = -1;
    fs.files[idx].size = 0;
    fs.files[idx].used = 1;
    fs.file_count++;
    return 0;
}

int fs_write(const char *path, const char *data, int size) {
    if (!data || size <= 0) return -1;

    int idx = find_file(path);
    if (idx < 0) {
        if (fs_create(path) < 0) return -1;
        idx = find_file(path);
    }
    if (idx < 0) return -1;

    int total_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (total_blocks > BLOCK_COUNT) return -1;

    int prev_block = -1;
    int first_block = find_free_block();
    if (first_block < 0) return -1;

    int written = 0;
    int current_block = first_block;

    for (int b = 0; b < total_blocks && written < size; b++) {
        int chunk = size - written;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;

        memcpy(fs.data[current_block], data + written, chunk);
        written += chunk;

        if (prev_block >= 0) {
            fs.fat[prev_block] = current_block;
        }

        prev_block = current_block;
        fs.fat[current_block] = -1;

        if (b < total_blocks - 1) {
            int next = find_free_block();
            if (next < 0) break;
            current_block = next;
        }
    }

    if (fs.files[idx].first_block < 0) {
        fs.files[idx].first_block = first_block;
    } else {
        int blk = fs.files[idx].first_block;
        while (fs.fat[blk] != -1) blk = fs.fat[blk];
        fs.fat[blk] = first_block;
    }
    fs.files[idx].size += written;
    return written;
}

int fs_read(const char *path, char *buf, int size) {
    if (!buf || size <= 0) return -1;

    int idx = find_file(path);
    if (idx < 0) return -1;

    int blk = fs.files[idx].first_block;
    int read = 0;

    while (blk != -1 && read < size) {
        int chunk = size - read;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        if (chunk > fs.files[idx].size - read) chunk = fs.files[idx].size - read;

        memcpy(buf + read, fs.data[blk], chunk);
        read += chunk;
        blk = fs.fat[blk];
    }
    return read;
}

int fs_delete(const char *path) {
    int idx = find_file(path);
    if (idx < 0) return -1;

    int blk = fs.files[idx].first_block;
    while (blk != -1) {
        int next = fs.fat[blk];
        fs.fat[blk] = -2;
        memset(fs.data[blk], 0, BLOCK_SIZE);
        blk = next;
    }

    fs.files[idx].used = 0;
    fs.files[idx].path[0] = '\0';
    fs.file_count--;
    return 0;
}

int fs_get_file_count(void) {
    return fs.file_count;
}

void fs_to_json(char *buf, size_t bufsize) {
    int pos = 0;
    pos += snprintf(buf + pos, bufsize - pos, "\"file_count\":%d,\"files\":[", fs.file_count);
    int added = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs.files[i].used) continue;
        if (added > 0) pos += snprintf(buf + pos, bufsize - pos, ",");
        pos += snprintf(buf + pos, bufsize - pos,
            "{\"path\":\"%s\",\"size\":%d}", fs.files[i].path, fs.files[i].size);
        added++;
    }
    pos += snprintf(buf + pos, bufsize - pos, "]");
}
