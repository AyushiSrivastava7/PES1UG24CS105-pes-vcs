#include "index.h"
#include "pes.h"
#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// external API
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// IMPORTANT: must be declared somewhere (tree.h or index.h)
uint32_t get_file_mode(const char *path);

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
// SAVE INDEX (ATOMIC)
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

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return 0;
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

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *buf = malloc(size ? size : 1);
    if (!buf) {
        fclose(f);
        return -1;
    }

    fread(buf, 1, size, f);
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
