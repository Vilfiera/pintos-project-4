#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "devices/block.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include <stdio.h>
#define BUFFER_CACHE 64

static struct cache_entry* cache_lookup (block_sector_t sector);
static struct cache_entry* cache_evict();
struct cache_entry {
	bool occupied;
	block_sector_t disk_sector;
	uint8_t buffer[BLOCK_SECTOR_SIZE];
	bool dirty;
	bool lru;
};

static struct cache_entry cache[BUFFER_CACHE];

static struct lock mutex;

void start_write_back() {
  thread_create("periodically_flush_cache", 0, cache_periodic_write, NULL);
}

//initialization
void cache_init(){
	lock_init(&mutex);
	int i;
	for (i = 0; i < BUFFER_CACHE; ++i){
		cache[i].occupied = false;
	}
  start_write_back();
}


//flush the given entry back to required disk_sector
static void cache_flush(struct cache_entry *entry){
	ASSERT(lock_held_by_current_thread(&mutex));
	ASSERT(entry != NULL && entry->occupied == true);
	if (entry->dirty){
		block_write(fs_device, entry->disk_sector, entry->buffer);
		entry->dirty = false;
	}
}

void cache_destroy() {
  lock_acquire(&mutex);

  size_t i = 0;
  for (i = 0; i < BUFFER_CACHE; i++) {
    if (!cache[i].occupied) {
      continue;
    }
    cache_flush(&(cache[i]));
  }
  lock_release(&mutex);
}



//lookup the given entry in cache buffer and return
// the required buffer else return NULL
static struct cache_entry* cache_lookup (block_sector_t sector){
	int i;
	for (i = 0; i < BUFFER_CACHE; ++i){
		if (cache[i].occupied == false){
			continue;
		}
		if (cache[i].disk_sector == sector){
			//cache hit
			return &(cache[i]);
		}
	}
  //cache miss
  return NULL;
}
//return free slot else evict a slot by implementing clock algo
static struct cache_entry* cache_evict(){
	ASSERT(lock_held_by_current_thread(&mutex));
	
	//implement clock algo
	static int clock = 0;
	while(true){
		if (cache[clock].occupied == false){
			//empty slot
			return &(cache[clock]);
		}
		if (cache[clock].lru){
			//second chance to evict
			cache[clock].lru = false;
		}
		//if not lru then return this slot
		else break;
		clock++;
		clock = clock % BUFFER_CACHE;
	}
	struct cache_entry *slot = &cache[clock];
	if(slot->dirty){
		//flush to disk
		cache_flush(slot);
	}
	slot->occupied = false;
	return slot;
}

//read into empty cache buffer's slot or evict and write to that slot
void cache_read(block_sector_t sector, void *target){
  cache_read_partial(sector, target, 0, BLOCK_SECTOR_SIZE);
}

// Reads desired sector into given target, through the cache.
// Allows user to specify the offset in the sector to start
// reading from, as well as the length of the read.
void cache_read_partial(block_sector_t sector, void *target, 
                        size_t ofs, size_t length) {
  lock_acquire(&mutex);
  ASSERT(length <= BLOCK_SECTOR_SIZE);
  ASSERT(ofs < BLOCK_SECTOR_SIZE);
  struct cache_entry *slot = cache_lookup(sector); //check entry
	//if not found
	if (slot == NULL){
		slot = cache_evict(); //evict slot
		ASSERT(slot != NULL && slot->occupied == false);
		slot->occupied = true;
		slot->disk_sector = sector;
		slot->dirty = false;
		block_read(fs_device, sector, slot->buffer);
	}
	//copy data from cache slot to memory
	slot->lru = true;
	memcpy(target, slot->buffer + ofs, length);
	lock_release(&mutex);

}

void cache_access(void* sectorPtr){
  lock_acquire(&mutex);
  block_sector_t sector = *(block_sector_t*)sectorPtr;
	struct cache_entry *slot = cache_lookup(sector); //check entry
	//if not found
	if (slot == NULL){
		slot = cache_evict(); //evict slot
		ASSERT(slot != NULL && slot->occupied == false);
		slot->occupied = true;
		slot->disk_sector = sector;
		slot->dirty = false;
		block_read(fs_device, sector, slot->buffer);
	}
	slot->lru = true;
  lock_release(&mutex);
}

void cache_read_ahead(block_sector_t sector) {
  block_sector_t *sectorPtr = malloc(sizeof(block_sector_t));
  *sectorPtr = sector;
  thread_create("cache_read_ahead", 0, cache_access, sectorPtr);
}

//write data from memory to cache and then to the disk
void cache_write(block_sector_t sector, const void *source){
  cache_write_partial(sector, source, 0, BLOCK_SECTOR_SIZE);
}

// Reads desired sector into given target, through the cache.
// Allows user to specify the offset in the sector to start
// writing from, as well as the length of the write.
void cache_write_partial(block_sector_t sector, const void *source,
                          size_t ofs, size_t length) {
	lock_acquire(&mutex);
  ASSERT(length <= BLOCK_SECTOR_SIZE);
  ASSERT(ofs < BLOCK_SECTOR_SIZE);
	struct cache_entry *slot = cache_lookup(sector);
	if (slot == NULL){
		slot = cache_evict ();
		ASSERT(slot != NULL && slot->occupied == false);
		slot->occupied = true;
		slot->disk_sector = sector;
		slot->dirty = false;
		block_read(fs_device, sector, slot->buffer);
	}
	slot->lru = true;
	slot->dirty = true;
	memcpy(slot->buffer + ofs, source, length);
	lock_release(&mutex);
}

void cache_periodic_write(void *aux UNUSED) {
  while (true) {
   timer_sleep(10*TIMER_FREQ);
   flush_entire_cache();
  }
}

void flush_entire_cache() {
  lock_acquire(&mutex);
  size_t i;
  for (i = 0; i < BUFFER_CACHE; i++) {
    if (!cache[i].occupied) {
      continue;
    } else if (cache[i].dirty) {
      cache_flush(&(cache[i]));
    }
  }
  lock_release(&mutex);
}
