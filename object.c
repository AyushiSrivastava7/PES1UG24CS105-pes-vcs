// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters.

#include "object.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

// ─── PATH HELPERS ───────────────────────────────────────────────────────────

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ────────────────────────────────────────────────────────────────────────────
// WRITE OBJECT
// ────────────────────────────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    if (!data || !id_out) return -1;

    char header[256];

    const char *type_str =
        (type == OBJ_BLOB) ? "blob" :
        (type == OBJ_TREE) ? "tree" : "commit";

    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    if (header_len <= 0 || header_len >= (int)sizeof(header)) return -1;

    size_t total_size = header_len + 1 + len;

    char *buffer = malloc(total_size);
    if (!buffer) return -1;

    memcpy(buffer, header, header_len);
    buffer[header_len] = '\0';
    memcpy(buffer + header_len + 1, data, len);

    // compute hash of full object
    compute_hash(buffer, total_size, id_out);

    // deduplication
    if (object_exists(id_out)) {
        free(buffer);
        return 0;
    }

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%.2s", OBJECTS_DIR, hex);

    mkdir(OBJECTS_DIR, 0755);
    mkdir(dir, 0755);

    char final_path[512];
    object_path(id_out, final_path, sizeof(final_path));

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        free(buffer);
        return -1;
    }

    if (write(fd, buffer, total_size) != (ssize_t)total_size) {
        close(fd);
        unlink(tmp_path);
        free(buffer);
        return -1;
    }

    fsync(fd);
    close(fd);

    if (rename(tmp_path, final_path) != 0) {
        free(buffer);
        unlink(tmp_path);
        return -1;
    }

    int dirfd = open(dir, O_RDONLY);
    if (dirfd >= 0) {
        fsync(dirfd);
        close(dirfd);
    }

    free(buffer);
    return 0;
}

// ────────────────────────────────────────────────────────────────────────────
// READ OBJECT
// ────────────────────────────────────────────────────────────────────────────

int object_read(const ObjectID *id, ObjectType *type_out,
                void **data_out, size_t *len_out)
{
    if (!id || !type_out || !data_out || !len_out)
        return -1;

    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size_l = ftell(f);
    rewind(f);

    if (size_l <= 0) {
        fclose(f);
        return -1;
    }

    size_t size = (size_t)size_l;

    char *buffer = malloc(size);
    if (!buffer) {
        fclose(f);
        return -1;
    }

    if (fread(buffer, 1, size, f) != size) {
        fclose(f);
        free(buffer);
        return -1;
    }

    fclose(f);

    // verify integrity
    ObjectID computed;
    compute_hash(buffer, size, &computed);

    if (memcmp(&computed, id, sizeof(ObjectID)) != 0) {
        free(buffer);
        return -1;
    }

    char *sep = memchr(buffer, '\0', size);
    if (!sep) {
        free(buffer);
        return -1;
    }

    size_t header_len = sep - buffer;

    char header[256];
    memcpy(header, buffer, header_len);
    header[header_len] = '\0';

    char type_str[16];
    size_t declared_size;

    if (sscanf(header, "%15s %zu", type_str, &declared_size) != 2) {
        free(buffer);
        return -1;
    }

    if (strcmp(type_str, "blob") == 0)
        *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0)
        *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0)
        *type_out = OBJ_COMMIT;
    else {
        free(buffer);
        return -1;
    }

    size_t data_len = size - header_len - 1;

    if (data_len != declared_size) {
        free(buffer);
        return -1;
    }

    void *out = malloc(data_len);
    if (!out) {
        free(buffer);
        return -1;
    }

    memcpy(out, sep + 1, data_len);

    *data_out = out;
    *len_out = data_len;

    free(buffer);
    return 0;
}
