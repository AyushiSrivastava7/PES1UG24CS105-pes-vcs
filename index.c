#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// external API
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ───────────────────────────────
// FIND ENTRY
// ───────────────────────────────
IndexEntry *index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            return &index->entries[i];
        }
    }
    return NULL;
}

// ───────────────────────────────
// LOAD INDEX
// ───────────────────────────────
int index_load(Index *index) {
    FILE *f = fopen(INDEX_FILE, "r");
    index->count = 0;

    if (!f) return 0;

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

// ───────────────────────────────
// SAVE INDEX
// ───────────────────────────────
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

    fclose(f);
    return 0;
}

// ───────────────────────────────
// INDEX STATUS (FIXED MISSING SYMBOL)
// ───────────────────────────────
int index_status(void) {
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    return (size > 0);
}

// ───────────────────────────────
// ADD FILE
// ───────────────────────────────
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

    size_t size = (size_t)st.st_size;

    if (size == 0) size = 1;

    void *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    fclose(f);

    if (read_bytes != size && st.st_size != 0) {
        free(buf);
        return -1;
    }

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

    // store metadata
    e->mode = st.st_mode & 0777;
    e->hash = id;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;

    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';

    return index_save(index);
}
