/*
 * Summary: Specification of the storage engine interface.
 *
 * Copy: See Copyright for the status of this software.
 *
 * Author: Trond Norbye <trond.norbye@sun.com>
 */
#ifndef MEMCACHED_DEFAULT_ENGINE_INTERNAL_H
#define MEMCACHED_DEFAULT_ENGINE_INTERNAL_H

#include "config.h"

#include <stdbool.h>

#include <memcached/engine.h>
#include <memcached/util.h>
#include <memcached/visibility.h>
#include <platform/platform.h>

/* Slab sizing definitions. */
#define POWER_SMALLEST 1
#define POWER_LARGEST  200
#define CHUNK_ALIGN_BYTES 8
#define DONT_PREALLOC_SLABS
#define MAX_NUMBER_OF_SLAB_CLASSES (POWER_LARGEST + 1)

/** How long an object can reasonably be assumed to be locked before
    harvesting it on a low memory condition. */
#define TAIL_REPAIR_TIME (3 * 3600)


/* Forward decl */
struct default_engine;

#include "trace.h"
#include "items.h"
#include "assoc.h"
#include "slabs.h"

   /* Flags */
#define ITEM_LINKED (1)

/* temp */
#define ITEM_SLABBED (2)

/** The item is deleted (may only be accessed if explicitly asked for) */
#define ITEM_ZOMBIE (4)

struct config {
   size_t verbose;
   rel_time_t oldest_live;
   bool evict_to_free;
   size_t maxbytes;
   bool preallocate;
   float factor;
   size_t chunk_size;
   size_t item_size_max;
   bool ignore_vbucket;
   bool vb0;
   char *uuid;
   bool keep_deleted;
};

/**
 * Statistic information collected by the default engine
 */
struct engine_stats {
   cb_mutex_t lock;
   uint64_t evictions;
   uint64_t reclaimed;
   uint64_t curr_bytes;
   uint64_t curr_items;
   uint64_t total_items;
};

struct engine_scrubber {
   cb_mutex_t lock;
   uint64_t visited;
   uint64_t cleaned;
   time_t started;
   time_t stopped;
   bool running;
   bool force_delete;
};

struct vbucket_info {
    int state : 2;
};

#define NUM_VBUCKETS 65536

/**
 * Definition of the private instance data used by the default engine.
 *
 * This is currently "work in progress" so it is not as clean as it should be.
 */
struct default_engine {
   ENGINE_HANDLE_V1 engine;
   SERVER_HANDLE_V1 server;
   GET_SERVER_API get_server_api;

   /**
    * Is the engine initalized or not
    */
   bool initialized;

   struct assoc* assoc;
   struct slabs slabs;
   struct items items;

   struct config config;
   struct engine_stats stats;
   struct engine_scrubber scrubber;

   union {
       engine_info engine;
       char buffer[sizeof(engine_info) +
                   (sizeof(feature_info) * LAST_REGISTERED_ENGINE_FEATURE)];
   } info;

   char vbucket_infos[NUM_VBUCKETS];

   /* a unique bucket index, note this is not cluster wide and dies with the process */
   bucket_id_t bucket_id;
};

char* item_get_data(const hash_item* item);
hash_key* item_get_key(const hash_item* item);
void item_set_cas(ENGINE_HANDLE *handle, const void *cookie,
                  item* item, uint64_t val);
#ifdef __cplusplus
extern "C" {
#endif

MEMCACHED_PUBLIC_API
void destroy_engine(void);

void default_engine_constructor(struct default_engine* engine, bucket_id_t id);
void destroy_engine_instance(struct default_engine* engine);

#ifdef __cplusplus
}
#endif


#endif
