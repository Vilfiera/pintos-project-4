#include "devices/block.h"

//struct cache_entry 
void cache_init();
void cache_destroy();
void cache_read(block_sector_t sector, void *target);
void cache_write(block_sector_t sector, const void *source);
void cache_periodic_write (void *aux); 
void flush_entire_cache();
