#include "tree.h"
#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// external AP
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);


// ─── MODE CONSTANTS ─────────────────────
#define MODE_FILE  0100644
#define MODE_EXEC  0100755
#define MODE_DIR   0040000

// ─────────────────────────────────────────
// BUILD TREE LEVEL
// ─────────────────────────────────────────
static ObjectID build_tree_level(Index *idx, const char *prefix) {

    Tree tree;
    tree.count = 0;

    size_t prefix_len = prefix ? strlen(prefix) : 0;

    for (int i = 0; i < idx->count; i++) {

        char *path = idx->entries[i].path;

        if (prefix_len > 0 &&
            strncmp(path, prefix, prefix_len) != 0)
            continue;

        const char *rest = path + prefix_len;

        // ─── DIRECTORY CASE ───
        if (strchr(rest, '/')) {

            char dir[256] = {0};
            sscanf(rest, "%255[^/]", dir);

            int exists = 0;
            for (int j = 0; j < tree.count; j++) {
                if (strcmp(tree.entries[j].name, dir) == 0) {
                    exists = 1;
                    break;
                }
            }

            if (!exists) {

                char new_prefix[512];
                snprintf(new_prefix, sizeof(new_prefix),
                         "%s%s/", prefix ? prefix : "", dir);

                ObjectID subid = build_tree_level(idx, new_prefix);

                TreeEntry e;
                e.mode = MODE_DIR;
                strncpy(e.name, dir, sizeof(e.name) - 1);
                e.name[sizeof(e.name) - 1] = '\0';
                e.hash = subid;

                tree.entries[tree.count++] = e;
            }

        }
        // ─── FILE CASE ───
        else {

            TreeEntry e;
            e.mode = idx->entries[i].mode;
            strncpy(e.name, rest, sizeof(e.name) - 1);
            e.name[sizeof(e.name) - 1] = '\0';
            e.hash = idx->entries[i].hash;

            tree.entries[tree.count++] = e;
        }
    }

    // ─── SERIALIZE TREE ───
    void *buf = NULL;
    size_t len = 0;

    if (tree_serialize(&tree, &buf, &len) != 0 || !buf) {
        return (ObjectID){0};  // safe fail
    }

    ObjectID id;
    if (object_write(OBJ_TREE, buf, len, &id) != 0) {
        free(buf);
        return (ObjectID){0};
    }

    free(buf);
    return id;
}

// ─────────────────────────────────────────
// PUBLIC API
// ─────────────────────────────────────────

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {

    if (!tree || !data_out || !len_out)
        return -1;

    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) return -1;

    buf[0] = '\0';

    for (int i = 0; i < tree->count; i++) {

        char line[256];

        snprintf(line, sizeof(line),
                 "%o %s\n",
                 tree->entries[i].mode,
                 tree->entries[i].name);

        size_t need = strlen(buf) + strlen(line) + 1;

        if (need > cap) {
            while (cap < need) cap *= 2;
            buf = realloc(buf, cap);
        }

        strcat(buf, line);
    }

    *data_out = buf;
    *len_out = strlen(buf);

    return 0;
}
int tree_from_index(ObjectID *id_out) {

    Index idx;
    index_load(&idx);

    ObjectID root = build_tree_level(&idx, "");

    *id_out = root;
    return 0;
}
