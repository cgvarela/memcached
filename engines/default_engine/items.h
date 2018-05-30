#include "memcached/types.h"
#include <string.h>
#include <stddef.h>
#include "default_engine_internal.h"

#ifndef ITEMS_H
#define ITEMS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * You should not try to aquire any of the item locks before calling these
 * functions.
 */
typedef struct _hash_item {
    struct _hash_item* next;
    struct _hash_item* prev;
    struct _hash_item* h_next; /* hash chain next */
    /**
     * The unique identifier for this item (it is guaranteed to be unique
     * per key, which means that a two different version of a document
     * cannot have the same CAS value (This is not true after a server
     * restart given that default_bucket is an in-memory bucket).
     */
    uint64_t cas;

    /** least recent access */
    rel_time_t time;

    /** When the item will expire (relative to process startup) */
    rel_time_t exptime;

    /**
     * When the current lock for the object expire. If locktime < "current
     * time" the item isn't locked anymore (timed out). If locktime >=
     * "current time" the object is locked.
     */
    rel_time_t locktime;

    /** The total size of the data (in bytes) */
    uint32_t nbytes;

    /** Flags associated with the item (in network byte order) */
    uint32_t flags;

    /**
     * The number of entities holding a reference to this item object (we
     * operate in a copy'n'write context so it is always safe for all of
     * our clients to share an existing object, but we need the refcount
     * so that we know when we can release the object.
     */
    uint16_t refcount;

    /** Intermal flags used by the engine.*/
    uint8_t iflag;

    /** which slab class we're in */
    uint8_t slabs_clsid;

    /** to identify the type of the data */
    uint8_t datatype;

    // There is 3 spare bytes due to alignment
} hash_item;

/*
    The structure of the key we hash with.

    This is a combination of the bucket index and the client's key.

    To respect the memcached protocol we support keys > 250, even
    though the current frontend doesn't.

    Keys upto 128 bytes long will be carried wholly on the stack,
    larger keys go on the heap.
*/
typedef struct _hash_key_sized {
    bucket_id_t bucket_index;
    uint8_t client_key[128];
} hash_key_sized;

typedef struct _hash_key_data {
    bucket_id_t bucket_index;
    uint8_t client_key[1];
} hash_key_data;

typedef struct _hash_key_header {
    uint16_t len; /* length of the hash key (bucket_index+client) */
    hash_key_data* full_key; /* points to hash_key::key_storage or a malloc blob*/
} hash_key_header;

typedef struct _hash_key {
    hash_key_header header;
    hash_key_sized key_storage;
} hash_key;

static inline uint8_t* hash_key_get_key(const hash_key* key) {
    return (uint8_t*)key->header.full_key;
}

static inline bucket_id_t hash_key_get_bucket_index(const hash_key* key) {
    return key->header.full_key->bucket_index;
}

static inline void hash_key_set_bucket_index(hash_key* key,
                                             bucket_id_t bucket_index) {
    key->header.full_key->bucket_index = bucket_index;
}

static inline uint16_t hash_key_get_key_len(const hash_key* key) {
    return key->header.len;
}

static inline void hash_key_set_len(hash_key* key, uint16_t len) {
    key->header.len = len;
}

static inline uint8_t* hash_key_get_client_key(const hash_key* key) {
    return key->header.full_key->client_key;
}

static inline uint16_t hash_key_get_client_key_len(const hash_key* key) {
    return hash_key_get_key_len(key) -
           sizeof(key->header.full_key->bucket_index);
}

static inline void hash_key_set_client_key(hash_key* key,
                                           const void* client_key,
                                           const ssize_t client_key_len) {
    memcpy(key->header.full_key->client_key, client_key, client_key_len);
}

/*
 * return the bytes needed to store the hash_key structure
 * in a single contiguous allocation.
 */
static inline size_t hash_key_get_alloc_size(const hash_key* key) {
    return offsetof(hash_key, key_storage) + hash_key_get_key_len(key);
}

typedef struct {
    unsigned int evicted;
    unsigned int evicted_nonzero;
    rel_time_t evicted_time;
    unsigned int outofmemory;
    unsigned int tailrepairs;
    unsigned int reclaimed;
} itemstats_t;

struct items {
   hash_item *heads[POWER_LARGEST];
   hash_item *tails[POWER_LARGEST];
   itemstats_t itemstats[POWER_LARGEST];
   unsigned int sizes[POWER_LARGEST];
   /*
    * serialise access to the items data
   */
   cb_mutex_t lock;
};


/**
 * Allocate and initialize a new item structure
 * @param engine handle to the storage engine
 * @param key the key for the new item
 * @param nkey the number of bytes in the key
 * @param flags the flags in the new item
 * @param exptime when the object should expire
 * @param nbytes the number of bytes in the body for the item
 * @return a pointer to an item on success NULL otherwise
 */
hash_item *item_alloc(struct default_engine *engine,
                      const void *key, const size_t nkey, int flags,
                      rel_time_t exptime, int nbytes, const void *cookie,
                      uint8_t datatype);

/**
 * Get an item from the cache
 *
 * @param engine handle to the storage engine
 * @param cookie connection cookie
 * @param key the key for the item to get
 * @param nkey the number of bytes in the key
 * @param state Only return documents in this state
 * @return pointer to the item if it exists or NULL otherwise
 */
hash_item* item_get(struct default_engine* engine,
                    const void* cookie,
                    const void* key,
                    const size_t nkey,
                    const DocStateFilter state);

/**
 * Get an item from the cache and acquire the lock.
 *
 * @param engine handle to the storage engine
 * @param cookie connection cookie
 * @param where to return the item (if found)
 * @param key the key for the item to get
 * @param nkey the number of bytes in the key
 * @param locktime when the item expire
 * @return ENGINE_SUCCESS for success
 */
ENGINE_ERROR_CODE item_get_locked(struct default_engine* engine,
                                  const void* cookie,
                                  hash_item** it,
                                  const void* key,
                                  const size_t nkey,
                                  rel_time_t locktime);

/**
 * Get and touch an item
 *
 * @param engine handle to the storage engine
 * @param cookie connection cookie
 * @param where to return the item (if found)
 * @param key the key for the item to get
 * @param nkey the number of bytes in the key
 * @param exptime The new expiry time
 * @return ENGINE_SUCCESS for success
 */
ENGINE_ERROR_CODE item_get_and_touch(struct default_engine* engine,
                                     const void* cookie,
                                     hash_item** it,
                                     const void* key,
                                     const size_t nkey,
                                     rel_time_t exptime);


/**
 * Unlock an item in the cache
 *
 * @param engine handle to the storage engine
 * @param cookie connection cookie
 * @param key the key for the item to unlock
 * @param nkey the number of bytes in the key
 * @param cas value for the locked value
 * @return ENGINE_SUCCESS for success
 */
ENGINE_ERROR_CODE item_unlock(struct default_engine* engine,
                              const void* cookie,
                              const void* key,
                              const size_t nkey,
                              uint64_t cas);

/**
 * Reset the item statistics
 * @param engine handle to the storage engine
 */
void item_stats_reset(struct default_engine *engine);

/**
 * Get item statitistics
 * @param engine handle to the storage engine
 * @param add_stat callback provided by the core used to
 *                 push statistics into the response
 * @param cookie cookie provided by the core to identify the client
 */
void item_stats(struct default_engine *engine,
                ADD_STAT add_stat,
                const void *cookie);

/**
 * Get detaild item statitistics
 * @param engine handle to the storage engine
 * @param add_stat callback provided by the core used to
 *                 push statistics into the response
 * @param cookie cookie provided by the core to identify the client
 */
void item_stats_sizes(struct default_engine *engine,
                      ADD_STAT add_stat, const void *cookie);

/**
 * Flush expired items from the cache
 * @param engine handle to the storage engine
 */
void  item_flush_expired(struct default_engine *engine);

/**
 * Release our reference to the current item
 * @param engine handle to the storage engine
 * @param it the item to release
 */
void item_release(struct default_engine *engine, hash_item *it);

/**
 * Unlink the item from the hash table (make it inaccessible)
 * @param engine handle to the storage engine
 * @param it the item to unlink
 */
void item_unlink(struct default_engine *engine, hash_item *it);

/**
 * Unlink the item from the hash table (make it inaccessible),
 * but only if the CAS value in the item is the same as the
 * one in the hash table (two different connections may operate
 * on the same objects, so the cas value for the value in the
 * hashtable may be different than the items value. We need
 * to have exclusive access to the hashtable to do the actual
 * unlink)
 *
 * @param engine handle to the storage engine
 * @param it the item to unlink
 */
ENGINE_ERROR_CODE safe_item_unlink(struct default_engine *engine,
                                   hash_item *it);

/**
 * Store an item in the cache
 * @param engine handle to the storage engine
 * @param item the item to store
 * @param cas the cas value (OUT)
 * @param operation what kind of store operation is this (ADD/SET etc)
 * @param document_state the state of the document to store
 * @return ENGINE_SUCCESS on success
 *
 * @todo should we refactor this into hash_item ** and remove the cas
 *       there so that we can get it from the item instead?
 */
ENGINE_ERROR_CODE store_item(struct default_engine *engine,
                             hash_item *item,
                             uint64_t *cas,
                             ENGINE_STORE_OPERATION operation,
                             const void *cookie,
                             const DocumentState document_state);

/**
 * Run a single scrub loop for the engine.
 * @param engine handle to the storage engine
 */
void item_scrubber_main(struct default_engine *engine);

/**
 * Start the item scrubber for the engine
 * @param engine handle to the storage engine
 * @return true if the scrubber has been invoked
 */
bool item_start_scrub(struct default_engine *engine);

#ifdef __cplusplus
}
#endif


#endif
