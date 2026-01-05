#ifndef _SPL_LOOKASIDELIST_H
#define	_SPL_LOOKASIDELIST_H

extern void* osif_malloc(uint64_t);
extern void osif_free(void*, uint64_t);

#define	ZFS_LookAsideList_DRV_TAG '!SFZ'

#define	LOOKASIDELIST_CACHE_NAMELEN 31

#define ALLOC_MAGIC 0xABCD1234

typedef struct lookasidelist_cache {
    uint64_t cache_active_allocations;
    uint64_t total_alloc;
    uint64_t total_free;
    size_t cache_chunksize; // cache object size
    kstat_t *cache_kstat;
    char    cache_name[LOOKASIDELIST_CACHE_NAMELEN + 1];
    LOOKASIDE_LIST_EX lookasideField;
} lookasidelist_cache_t;

typedef enum {
    ALLOC_FROM_LOOKASIDE = 1
} alloc_source_t;

typedef struct alloc_hdr {
    uint32_t magic;
    alloc_source_t source;
} alloc_hdr_t;


lookasidelist_cache_t *lookasidelist_cache_create(char *name, size_t size);
void lookasidelist_cache_destroy(lookasidelist_cache_t *pLookasidelist_cache);
void* lookasidelist_cache_alloc(lookasidelist_cache_t *pLookasidelist_cache);
void lookasidelist_cache_free(lookasidelist_cache_t *pLookasidelist_cache,
    void *buf);
void lookaside_kmem_free(void);

#endif
