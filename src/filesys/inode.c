#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/synch.h"
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define INODE_NUM_DIRECT_BLOCKS 123
#define DIRECT_BLOCKS_PER_SECTOR 128


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    
    block_sector_t direct_blocks[INODE_NUM_DIRECT_BLOCKS];               /* Direct block pointers */
    block_sector_t single_indirect_block;
    block_sector_t double_indirect_block;             
    bool isDir;
  };

struct indirect_block {
    // 128 four-byte pointers; constitutes a 512 Byte space
    block_sector_t direct_blocks[DIRECT_BLOCKS_PER_SECTOR];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */

    struct lock inode_lock;             // Prevents multiple threads extending a file.
    off_t readable_length;              // Prevents readers from reading unwritten zeroes.
  };

static block_sector_t index_to_sector(const struct inode*, off_t);
static bool inode_alloc(struct inode_disk*, size_t);
static bool inode_alloc_indirect(block_sector_t*, size_t, int);
static bool inode_free(struct inode*);
static bool inode_free_indirect(block_sector_t, size_t, int);
static uint32_t min(uint32_t x, uint32_t y);


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return index_to_sector(inode, pos / BLOCK_SECTOR_SIZE);
  else
    return -1;
}

static block_sector_t index_to_sector (const struct inode *inode,
                                        off_t sector_index) {
  struct inode_disk *inodeDisk = &inode->data;
  off_t start_index = 0;
  off_t max_index = 0;
  block_sector_t result;

  // Get a direct block
  max_index += INODE_NUM_DIRECT_BLOCKS;
  if (sector_index < max_index) {
    return inodeDisk->direct_blocks[sector_index];
  }
  start_index = max_index;

  // Go through our single indirect block
  max_index += DIRECT_BLOCKS_PER_SECTOR;
  if (sector_index < max_index) {
    struct indirect_block *indirectBlock = malloc(sizeof(struct indirect_block));
    cache_read(inodeDisk->single_indirect_block, indirectBlock);

    result = indirectBlock->direct_blocks[sector_index - start_index];
    free(indirectBlock);

    return result;
  }
  start_index = max_index;

  // Go through our double indirect block
  max_index += DIRECT_BLOCKS_PER_SECTOR * DIRECT_BLOCKS_PER_SECTOR;
  if (sector_index < max_index) {
    off_t index_within_block = sector_index - start_index;
    // Index within the double indirect block, to get to a single indirect block. 
    off_t double_indirect_index = index_within_block / DIRECT_BLOCKS_PER_SECTOR;
    // Index within the single indirect block, to get to a direct block.
    off_t single_indirect_index = index_within_block % DIRECT_BLOCKS_PER_SECTOR;

    struct indirect_block *indirectBlock = malloc(sizeof(struct indirect_block));
    // Read in double indirect block
    cache_read(inodeDisk->double_indirect_block, indirectBlock);
    // Read in single indirect block, using double indirect block
    cache_read(indirectBlock->direct_blocks[double_indirect_index], indirectBlock);

    // Now we can get our block index.
    result = indirectBlock->direct_blocks[single_indirect_index];

    free(indirectBlock);
    return result;
  }
  // sector index out of bounds!
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isDir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->isDir = isDir;
      if (inode_alloc(disk_inode, disk_inode->length)) {
        cache_write(sector, disk_inode);
        success = true; 
      }
      free (disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
   return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_lock);
  cache_read (inode->sector, &inode->data);
  inode->readable_length = inode_length(inode);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_free(inode);
          /*free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length));*/ 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;


  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->readable_length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (sector_idx, buffer + bytes_read);
        }
      else 
        {
          // Read partial sector into caller's buffer.
          cache_read_partial(sector_idx, buffer + bytes_read,
                              sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;

      sector_idx = byte_to_sector(inode, offset);
      if (sector_idx != -1) {
        cache_read_ahead(sector_idx);
      }
    }

    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  if (inode->deny_write_cnt)
    return 0;
  if (byte_to_sector(inode, offset + size - 1) == -1) {
    lock_acquire(&inode->inode_lock);
    if (!inode_alloc(&inode->data, offset+size)) {
      // Error: could not extend file
      lock_release(&inode->inode_lock);
      return 0;
    }
    inode->data.length = offset + size;
    lock_release(&inode->inode_lock);
    cache_write(inode->sector, &inode->data);
    inode->readable_length = inode->data.length;
  }
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
        }
      else 
        {
          // Write partial sector to disk.
          cache_write_partial(sector_idx, buffer + bytes_written,
                                sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

static bool inode_alloc(struct inode_disk* disk_inode, size_t size) {
  if (size < 0) {
    return false;
  }
  static char zeros[BLOCK_SECTOR_SIZE];

  size_t sectors_to_alloc = bytes_to_sectors(size);

  size_t index;
  size_t max_index = min(sectors_to_alloc, INODE_NUM_DIRECT_BLOCKS); 
  for (index = 0; index < max_index; index++) {
    if (!disk_inode->direct_blocks[index]) {
      if (!free_map_allocate(1, &disk_inode->direct_blocks[index])) {
        return false;
      }
      cache_write(disk_inode->direct_blocks[index], zeros);
    }
    sectors_to_alloc--;
  }

  if (!sectors_to_alloc) {
    return true;
  }

  max_index = min(sectors_to_alloc, DIRECT_BLOCKS_PER_SECTOR);
  if (!inode_alloc_indirect(&disk_inode->single_indirect_block, max_index, 1)) {
    return false;
  }
  sectors_to_alloc -= max_index;

  if (!sectors_to_alloc) {
    return true;
  }

  size_t double_indirect_sectors = DIRECT_BLOCKS_PER_SECTOR * DIRECT_BLOCKS_PER_SECTOR;
  max_index = min(sectors_to_alloc, double_indirect_sectors);
  if (!inode_alloc_indirect(&disk_inode->double_indirect_block,
                                max_index, 2)) {
    return false;
  }
  sectors_to_alloc -= max_index;
  
  if (!sectors_to_alloc) {
    return true;
  }

  return false;
}

static bool inode_alloc_indirect(block_sector_t *block, size_t sectors_to_alloc,
                                      int level) {
  static char zeros[BLOCK_SECTOR_SIZE];

  // Allocate direct blocks
  if (level == 0) {
    if (!*block) {
      if (!free_map_allocate(1, block)) {
        return false;
      }
      cache_write(*block, zeros);
    }
    return true;
  }

  // Begin allocating indirect block recursively
  struct indirect_block indirectBlock;
  if (!*block) {
    if (!free_map_allocate(1, block)) {
      return false;
    }
    cache_write(*block, zeros);
  }
  cache_read(*block, &indirectBlock);

  size_t index;
  size_t max_index;
  if (level == 1) {
    max_index = sectors_to_alloc;
  } else {
    max_index = DIV_ROUND_UP(sectors_to_alloc, DIRECT_BLOCKS_PER_SECTOR);
  }
                      
  for (index = 0; index < max_index; index++) {
    size_t chunk_to_alloc;
    if (level == 1) {
      chunk_to_alloc = min(sectors_to_alloc, 1);
    } else {
      chunk_to_alloc = min(sectors_to_alloc, DIRECT_BLOCKS_PER_SECTOR);
    }
    if (!inode_alloc_indirect(&indirectBlock.direct_blocks[index],
                                  chunk_to_alloc, level - 1)) {
      return false;
    }
    sectors_to_alloc -= chunk_to_alloc;
  }
  cache_write(*block, &indirectBlock);
  return true;
}

static bool inode_free(struct inode *inode) {
  // Nothing to deallocate
  if (!inode->data.length) {
    return false;
  }

  size_t sectors_to_free = bytes_to_sectors(inode->data.length);
  size_t max_index = min(sectors_to_free, INODE_NUM_DIRECT_BLOCKS);
  size_t index;
  for (index = 0; index < max_index; index++) {
    free_map_release(inode->data.direct_blocks[index], 1);
    sectors_to_free--;
  }

  max_index = min(sectors_to_free, DIRECT_BLOCKS_PER_SECTOR);
  if (max_index > 0) {
    inode_free_indirect(inode->data.single_indirect_block, max_index, 1);
    sectors_to_free -= max_index;
  }

  size_t double_indirect_sectors = DIRECT_BLOCKS_PER_SECTOR * DIRECT_BLOCKS_PER_SECTOR;
  max_index = min(sectors_to_free, double_indirect_sectors);
  if (max_index > 0) {
    inode_free_indirect(inode->data.double_indirect_block, max_index, 2);
    sectors_to_free -= max_index;
  }
  return true;
}

static bool inode_free_indirect(block_sector_t block, size_t sectors_to_free,
                                  int level) {
  if (level == 0) {
    free_map_release(block, 1);
    return true;
  }
  struct indirect_block indirectBlock;
  cache_read(block, &indirectBlock);

  size_t index;
  size_t max_index;
  if (level == 1) {
    max_index = sectors_to_free;
  } else {
    max_index = DIV_ROUND_UP(sectors_to_free, DIRECT_BLOCKS_PER_SECTOR);
  }
                      
  for (index = 0; index < max_index; index++) {
    size_t chunk_to_free;
    if (level == 1) {
      chunk_to_free = min(sectors_to_free, 1);
    } else {
      chunk_to_free = min(sectors_to_free, DIRECT_BLOCKS_PER_SECTOR);
    }
    if (!inode_free_indirect(indirectBlock.direct_blocks[index],
                                  chunk_to_free, level - 1)) {
      return false;
    }
    sectors_to_free -= chunk_to_free;
  }
  free_map_release(block, 1);
  return true;
}

static uint32_t min(uint32_t x, uint32_t y) {
  return x < y ? x : y;
}

bool inode_is_removed(struct inode *inode) {
  return inode->removed;
}

bool inode_is_dir(struct inode *inode) {
  return inode->data.isDir;
}
