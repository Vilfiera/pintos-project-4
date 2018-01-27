#include "devices/block.h"

//struct cache_entry 
void cache_init();
void cache_destroy();
void cache_read(block_sector_t sector, void *target);
void cache_read_partial(block_sector_t, void *, size_t, size_t);
void cache_write(block_sector_t sector, const void *source);
void cache_write_partial(block_sector_t, const void *, size_t, size_t);
void cache_periodic_write (void *aux); 
void flush_entire_cache();
void cache_access(void*);
void cache_read_ahead(block_sector_t);
void cache_read_ahead(block_sector_t sector);
