#include "tree.h"
#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// 🔥 FIX: add this (CRITICAL)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE  0100644
#define MODE_EXEC  0100755
#define MODE_DIR   0040000

// ─── PROVIDED FUNCTIONS (UNCHANGED) ─────────────────────────────────────────
// (keep your existing get_file_mode, tree_parse, tree_serialize exactly as-is)

// ─────────────────────────────────────────────────────────────────────────────
// BUILD TREE RECURSIVELY
// ─────────────────────────────────────────────────────────────────────────────

static ObjectID build_tree_level(Index *idx, const char *prefix) {

    Tree tree;
    tree.count = 0;

    for (int i = 0; i < idx->count; i++) {

        char *path = idx->entries[i].path;

        if (prefix && strlen(prefix) > 0) {
            if (strncmp(path, prefix, strlen(prefix)) != 0)
                continue;
        }

        char *rest = path + (prefix ? strlen(prefix) : 0);

        if (strchr(rest, '/')) {

            char dir[256];
            sscanf(rest, "%[^/]", dir);

            int exists = 0;
            for (int j = 0; j < tree.count; j++) {
                if (strcmp(tree.entries[j].name, dir) == 0) {
                    exists = 1;
                    break;
                }
            }

            if (!exists) {

                char new_prefix[512];

                if (prefix)
                    snprintf(new_prefix, sizeof(new_prefix), "%s%s/", prefix, dir);
                else
                    snprintf(new_prefix, sizeof(new_prefix), "%s/", dir);

                ObjectID subid = build_tree_level(idx, new_prefix);

                TreeEntry e;
                e.mode = MODE_DIR;
                strcpy(e.name, dir);
                e.hash = subid;

                tree.entries[tree.count++] = e;
            }

        } else {

            TreeEntry e;
            e.mode = idx->entries[i].mode;
            strcpy(e.name, rest);
            e.hash = idx->entries[i].hash;

            tree.entries[tree.count++] = e;
        }
    }

    // ─── FIX: always create a tree object (NOT raw zero return)
    void *buf;
    size_t len;

    if (tree.count == 0) {
        tree_serialize(&tree, &buf, &len);

        ObjectID id;
        object_write(OBJ_TREE, buf, len, &id);

        free(buf);
        return id;
    }

    tree_serialize(&tree, &buf, &len);

    ObjectID id;
    object_write(OBJ_TREE, buf, len, &id);

    free(buf);

    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API
// ─────────────────────────────────────────────────────────────────────────────

int tree_from_index(ObjectID *id_out) {

    Index idx;
    index_load(&idx);

    ObjectID root = build_tree_level(&idx, "");

    *id_out = root;
    return 0;
}
