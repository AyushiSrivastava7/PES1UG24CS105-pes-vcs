// object.h — Content-addressable object store (FIXED)

#ifndef OBJECT_H
#define OBJECT_H

#include <stddef.h>
#include <stdint.h>
#include "pes.h"   // IMPORTANT: reuse shared types from pes.h

// ONLY keep hash utilities here
void hash_to_hex(const ObjectID *id, char *hex_out);
int hex_to_hash(const char *hex, ObjectID *id_out);

int object_write(ObjectType type,
                 const void *data,
                 size_t len,
                 ObjectID *out);

int object_read(const ObjectID *id,
                ObjectType *type_out,
                void **data_out,
                size_t *len_out);

#endif
