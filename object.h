// object.h — Content-addressable object store

#ifndef OBJECT_H
#define OBJECT_H

#include <stddef.h>
#include <stdint.h>

#define HASH_SIZE 32
#define HASH_HEX_SIZE (HASH_SIZE * 2)

// Represents a SHA-256 hash
typedef struct {
    uint8_t hash[HASH_SIZE];
} ObjectID;

// Object types stored in repository
typedef enum {
    OBJ_BLOB,
    OBJ_TREE,
    OBJ_COMMIT
} ObjectType;

// Convert binary hash → hex string (64 chars + null)
void hash_to_hex(const ObjectID *id, char *hex_out);

// Convert hex string → binary hash
int hex_to_hash(const char *hex, ObjectID *id_out);

// Write object to store (.pes/objects)
int object_write(ObjectType type,
                 const void *data,
                 size_t len,
                 ObjectID *out);

// Read object from store
int object_read(const ObjectID *id,
                ObjectType *type_out,
                void **data_out,
                size_t *len_out);

#endif
