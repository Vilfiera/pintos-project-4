		     +-------------------------+
		     |            OS           |
		     | PROJECT 4: FILE SYSTEMS |
		     |     DESIGN DOCUMENT     |
		     +-------------------------+

---- GROUP ----

>> Fill in the names and email addresses of your group members.

Kevin Vong <s8kevong@stud.uni-saarland.de>
Kaleem Ullah <s8kaulla@stud.uni-saarland.de>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

		     INDEXED AND EXTENSIBLE FILES
		     ============================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In inode.c:
  In struct inode_disk:
    block_sector_t direct_blocks[INODE_NUM_DIRECT_BLOCKS];
 
    An array of 123 direct block pointers. Note that 5*4 bytes are used
    by other struct members.

    block_sector_t single_indirect_block;
    
    A pointer to an indirect block, which contains 128 direct block pointers.

    block_sector_t double_indirect_block;             

    A pointer to a double indirect block, which contains 128 indirect blocks.

  struct indirect_block {
      block_sector_t direct_blocks[DIRECT_BLOCKS_PER_SECTOR];
  };

  Structure of an indirect block, which contains 128 pointers to other blocks.
  Note that 128 * 4 = 512 byte size.

  In struct inode:
    struct lock inode_lock;

    A lock used to prevent multiple processes from extending a file.

    off_t readable_length;
    
    Prevents readers from reading from an extended file before the writer
    actually writes to it.

>> A2: What is the maximum size of a file supported by your inode
>> structure?  Show your work.

Each inode_disk has 123 direct blocks, 1 indirect block, and 1 double
indirect block. Each block is 512 bytes, and each indirect block contains
128 indirect blocks.

So we have 123*512 + 1*128*512 + 1*128*128*512 = 8517120 bytes.
8517120 bytes is about 8.12 MB.

---- SYNCHRONIZATION ----

>> A3: Explain how your code avoids a race if two processes attempt to
>> extend a file at the same time.

In struct inode, we added an inode_lock member. When a proces wants to
extend a file -- seen in inode_write_at -- they have to acquire this lock.
When the lock is acquired, the file is extended to the desired size.
Since we only let processes extend files when they acquire the lock, and
files are only extended to the desired size, this prevents a file from
being overextended -- like when two processes try to increment the length 
at the same time.

>> A4: Suppose processes A and B both have file F open, both
>> positioned at end-of-file.  If A reads and B writes F at the same
>> time, A may read all, part, or none of what B writes.  However, A
>> may not read data other than what B writes, e.g. if B writes
>> nonzero data, A is not allowed to see all zeros.  Explain how your
>> code avoids this race.

In struct inode, we added a readable_length member. When a file is
extended, it's length is extended before data is written to it. Instead
of allowing readers to check the actual file length, they check this
readable_length -- which is updated after the writer writes to the
extended part of a file. This prevents the reader from reading all zeroes
when a file is first extended.

>> A5: Explain how your synchronization design provides "fairness".
>> File access is "fair" if readers cannot indefinitely block writers
>> or vice versa.  That is, many processes reading from a file cannot
>> prevent forever another process from writing the file, and many
>> processes writing to a file cannot prevent another process forever
>> from reading the file.

In our design, readers cannot block writers. The only locking mechanism
in place is the one used to prevent multiple file extensions. Since
there is no locking between readers and writers, there are no issues
with fairness.

However, it's worth noting that this allows for undefined behavior
with readers and writers accessing the same file. As stated in the
project specification, all, part, or none of a write can be read
by a reader when a writer is accessing the same file. This makes
usage a little unpredictable, but it's consistent with how the
specifications said the filesys should perform.

---- RATIONALE ----

>> A6: Is your inode structure a multilevel index?  If so, why did you
>> choose this particular combination of direct, indirect, and doubly
>> indirect blocks?  If not, why did you choose an alternative inode
>> structure, and what advantages and disadvantages does your
>> structure have, compared to a multilevel index?

Yes, it's a multilevel index with 123 direct, 1 direct, and 1 double indirect
block. The inode_disk had to be 512 bytes long, and there were 3 fields of 4
bytes each already. Since we needed at least one direct and one double indirect,
that made 5 fields of 4 bytes accounted for. We used the rest of the 512 bytes
for direct blocks (492 bytes / 4 byte block pointers = 123 direct blocks).

			    SUBDIRECTORIES
			    ==============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In inode.c:
  In struct inode_disk:
    bool isDir;

    Used to track whether a file is a directory or not.

In thread.h:
  In struct thread:
  	struct dir *cur_dir;

    Keeps track of a thread's current working directory.
  
---- ALGORITHMS ----

>> B2: Describe your code for traversing a user-specified path.  How
>> do traversals of absolute and relative paths differ?

To traverse a specified path, we first check if it's an absolute path.
If so, we call dir_open_root to open the root directory. Otherwise, we
use the thread's current working directory.

From here, we tokenize the specified path, separating tokens with the "/"
delimiter. For each token we get, we traverse the current directory -- so
we follow the entire path using each token as a stepping stone towards
the user's specified file.

Absolute paths, which begin with a "/", begin from the root directory when
traversing. Relative paths begin from the thread's current working
directory when traversing. Other than where they begin, the traversals
are the same.

---- SYNCHRONIZATION ----

>> B4: How do you prevent races on directory entries?  For example,
>> only one of two simultaneous attempts to remove a single file
>> should succeed, as should only one of two simultaneous attempts to
>> create a file with the same name, and so on.

When a directory entry is deleted, it writes to the inode of the entry's
directory. Having more than one simultaneous attempt to write to the inode
is no problem, as they will just write the same data. The inode for the
directory entry is then marked as "removed," which will free the inode's
resources after it is closed by the last opener.

We prevented races by allowing inconsequential actions to be performed
simultaneously, like writing to a directory's inode to delete a dir entry,
or by only allowing the last process to actually access critical sections,
such as deallocating resources.

>> B5: Does your implementation allow a directory to be removed if it
>> is open by a process or if it is in use as a process's current
>> working directory?  If so, what happens to that process's future
>> file system operations?  If not, how do you prevent it?

Yes, a directory can be removed even if it is open by a process. If
this happens, the inode is marked as "removed" -- so all operations will
fail for that process. This is one of the two options in the spec, and
we figured that it would be simpler to allow deletion rather than
keep track of how many threads had a directory open.
 
---- RATIONALE ----

>> B6: Explain why you chose to represent the current directory of a
>> process the way you did.

We used a struct dir pointer in struct thread, because it seemed logical
for each process to be able to perform operations within its current
directory easily. This made it really easy for us to traverse a specified
path, as we could either use the root or the current directory to begin
our traversal through the file system. 

			     BUFFER CACHE
			     ============

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In cache.c:
  struct cache_entry {
    bool occupied;
    block_sector_t disk_sector;
    uint8_t buffer[BLOCK_SECTOR_SIZE];
    bool dirty;
    bool lru;
  };

  This is one entry in our cache; contains info like disk sector,
  a buffer, and occupied/dirty/used bits.

  static struct cache_entry cache[BUFFER_CACHE];

  This is an array of 64 cache entries kept in memory.

  static struct lock mutex;

  A lock used to ensure mutual exclusive access to the cache.

---- ALGORITHMS ----

>> C2: Describe how your cache replacement algorithm chooses a cache
>> block to evict.

We used the clock eviction algorithm, which scans through the cache
to find an empty slot. If none are empty, then it will begin to "unmark"
the LRU bit of each entry. If an entry is not used again by the time our
clock pointer gets to it again, it is flushed out from the cache and its slot
is freed.
 
>> C3: Describe your implementation of write-behind.

We write directly to the cache and mark the entry as dirty. Whenever an
entry is evicted, we check if it's dirty; if so, we write the block back to
its disk sector.

Note that we also periodically write the cache back to disk. We do this by
spawning a new thread, which calls the cache_periodic_write function, in
our cache_init. This ensures that we don't always wait for a slot to be evicted
before writing it back to disk.

>> C4: Describe your implementation of read-ahead.

In inode_read_at, whenever we read a block, we check if there is another block
in our current file. If so, we spawn a thread that calls the cache_read_ahead
function, which reads that sector into the cache. Since we spawn a thread and
continue with the user requested workload, this provides an aynchronous read-ahead
to our cache implementation.

---- SYNCHRONIZATION ----

>> C5: When one process is actively reading or writing data in a
>> buffer cache block, how are other processes prevented from evicting
>> that block?

When a process is currently using a block in the cache, it possesses
the cache lock. This prevents any other thread from evicting a block
from the cache. In between reads and writes, though, this block can be
evicted by another -- but this is highly unlikely, due to the second
chance we give accessed blocks before evicting them.

>> C6: During the eviction of a block from the cache, how are other
>> processes prevented from attempting to access the block?

Every access to the cache requires the acquisition of the cache lock.
If a block is being evicted, then a process possesses the lock -- as
eviction is an operation on the cache. Since this process has the lock,
all other processes must wait for the lock to be released before accessing
the desired block. After the lock is released, the other processes
are free to initiate a cache miss and read in the block from the disk again.

---- RATIONALE ----

>> C7: Describe a file workload likely to benefit from buffer caching,
>> and workloads likely to benefit from read-ahead and write-behind.

A workload likely to benefit from caching is one that accesses the same 
file repeatedly. Since the file data would just be kept in the cache --
assuming that it's small enough -- this prevents us from constantly having
to read from and write to disk.

Read-ahead is useful for workloads that sequentially read a file, as we will
already have the next block in the cache by the time the process gets to it.
Write-behind is useful for workloads that repeatedly write small amounts of
data to a file. If we store these changes in the cache, and only write them
to disk when the slot is evicted or the periodic write back kicks in, this
saves us from issuing a lot of really small write requests.

			   SURVEY QUESTIONS
			   ================

Answering these questions is optional, but it will help us improve the
course in future quarters.  Feel free to tell us anything you
want--these questions are just to spur your thoughts.  You may also
choose to respond anonymously in the course evaluations at the end of
the quarter.

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

>> Do you have any suggestions for the TAs to more effectively assist
>> students in future quarters?

>> Any other comments?
