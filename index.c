// index.c — Staging area implementation

#include "index.h"
#include "pes.h"
#include "object.h"   // IMPORTANT FIX: needed for object_write
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// ─── STATUS (UNCHANGED LOGIC) ───────────────────────────────────────────────

int index_status(const Index *index) {
    printf("Staged changes:\n");

    if (index->count == 0)
        printf("  (nothing to show)\n");

    for (int i = 0; i < index->count; i++) {
        printf("  staged: %s\n", index->entries[i].path);
    }

    printf("\nUnstaged changes:\n");

    int found = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0 ||
            st.st_mtime != (time_t)index->entries[i].mtime_sec ||
            st.st_size != (off_t)index->entries[i].size) {

            printf("  modified/deleted: %s\n", index->entries[i].path);
            found++;
        }
    }

    if (!found)
        printf("  (nothing to show)\n");

    printf("\nUntracked files:\n");

    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL) {

            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0)
                continue;

            if (strcmp(ent->d_name, ".pes") == 0)
                continue;

            int tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    tracked = 1;
                    break;
                }
            }

            if (!tracked)
                printf("  %s\n", ent->d_name);
        }

        closedir(dir);
    }

    return 0;
}

// ─── LOAD INDEX ─────────────────────────────────────────────────────────────

int index_load(Index *index) {

    FILE *f = fopen(INDEX_FILE, "r");
    index->count = 0;

    if (!f)
        return 0;

    char line[1024];

    while (fgets(line, sizeof(line), f)) {

        if (index->count >= MAX_INDEX_ENTRIES)
            break;

        IndexEntry *e = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];

        if (sscanf(line, "%o %64s %lu %u %s",
                   &e->mode,
                   hash_hex,
                   &e->mtime_sec,
                   &e->size,
                   e->path) != 5) {
            continue;
        }

        hex_to_hash(hash_hex, &e->hash);
        index->count++;
    }

    fclose(f);
    return 0;
}

// ─── SAVE INDEX (FIXED, NO DUPLICATES) ──────────────────────────────────────

int index_save(const Index *index) {

    FILE *f = fopen(INDEX_FILE, "w");
    if (!f) return -1;

    for (int i = 0; i < index->count; i++) {

        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&index->entries[i].hash, hash_hex);

        fprintf(f, "%o %s %lu %u %s\n",
                index->entries[i].mode,
                hash_hex,
                index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return 0;
}

// ─── ADD FILE ───────────────────────────────────────────────────────────────

int index_add(Index *index, const char *path) {

    if (index_load(index) != 0)
        return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    struct stat st;
    if (stat(path, &st) != 0) {
        fclose(f);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, size, f) != size) {
        free(buf);
        fclose(f);
        return -1;
    }

    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, buf, size, &id) != 0) {
        free(buf);
        return -1;
    }

    free(buf);

    IndexEntry *e = index_find(index, path);

    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES)
            return -1;

        e = &index->entries[index->count++];
    }

    e->mode = get_file_mode(path);
    e->hash = id;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;

    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';

    return index_save(index);
}
