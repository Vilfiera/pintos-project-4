#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "filesys/off_t.h"
#include "lib/string.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Typical return values from main() and arguments to exit(). */
#define EXIT_SUCCESS 0          /* Successful execution. */
#define EXIT_FAILURE 1          /* Unsuccessful execution. */
#define USER_VADDR_BOUND (void*) 0x08048000

struct lock filesys_mutex;

static void syscall_handler (struct intr_frame *);

static void parse_args(void* esp, int* argBuf, int numToParse);
static void valid_ptr(void* user_ptr);
static void valid_buf(char* buf, unsigned size);
static void valid_string(void* string);
int inumber(int fd);
bool isdir (int fd);
bool readdir (int fd, char *name);
bool mkdir (const char *filename);
bool chdir (const char *filename);
struct file_record * fileRd_ptr(int fd);





static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}







void
syscall_init (void) 
{
  lock_init(&filesys_mutex);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int args[3];
  void* esp = f->esp;
  valid_ptr(esp);
  uint32_t current_syscall = *(uint32_t*)esp;
  
  switch(current_syscall)
  {
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
    parse_args(esp, &args[0], 1);
    exit ((int) args[0]);
		break;
	case SYS_EXEC:
		parse_args(esp, &args[0], 1);
    valid_ptr(args[0]);
    valid_string((void*) args[0]);
    f->eax = exec ((const char*) args[0]);
		break;
	case SYS_WAIT:
		parse_args(esp, &args[0], 1);
    f->eax = wait ((pid_t) args[0]);
		break;
  case SYS_CREATE:
		parse_args(esp, &args[0], 2);
    valid_ptr(args[0]);
    //valid_buf((char*)args[0], (unsigned)args[1]);
    valid_string((void*) args[0]);
    f->eax = create((const char*) args[0], (unsigned) args[1]);
    break;
	case SYS_REMOVE:
		parse_args(esp, &args[0], 1);
    valid_ptr(args[0]);
    valid_string((void*) args[0]);
    f->eax = remove ((const char*) args[0]);
		break;
	case SYS_OPEN:
		parse_args(esp, &args[0], 1);
    valid_ptr(args[0]);
    valid_string((void*) args[0]);
    f->eax = open ((const char*) args[0]);
		break;
	case SYS_FILESIZE:
		parse_args(esp, &args[0], 1);
    f->eax = filesize ((int) args[0]);
		break;
	case SYS_READ:
		parse_args(esp, &args[0], 3);
    valid_ptr(args[1]);
    valid_buf((char*) args[1], (unsigned)args[2]);
    f->eax = read ((int) args[0], (void*) args[1], (unsigned) args[2]);
		break;
	case SYS_WRITE:
		parse_args(esp, &args[0], 3);
    valid_ptr(args[1]);
    valid_buf((char*) args[1], (unsigned) args[2]);
	  f->eax = write((int) args[0], (const void*) args[1], (unsigned) args[2]);
		break;
	case SYS_SEEK:
		parse_args(esp, &args[0], 2);
    seek ((int) args[0], (unsigned) args[1]);
		break;
	case SYS_TELL:
		parse_args(esp, &args[0], 1);
    f->eax = tell ((int) args[0]);
		break;
	case SYS_CLOSE:
		parse_args(esp, &args[0], 1);
    close ((int) args[0]);
		break;
	case SYS_CHDIR:
		parse_args (esp, &args [0], 1);
		f->eax = chdir ((const char *) args[0]);
		break;
	case SYS_READDIR:
		parse_args (esp, &args[0], 2);
		f->eax = readdir ((int) args[0], (char *) args[1]);
		break;
	case SYS_ISDIR:
		parse_args (esp, &args[0], 1);
		f->eax = isdir ((int) args[0]);
		break;
	case SYS_INUMBER:
		parse_args(esp, &args[0], 1);
		f->eax = inumber ((int) args[0]);
		break;
	case SYS_MKDIR:
		parse_args (esp, &args[0], 1);
		f->eax = mkdir ((const char *)args[0]);
		break;
	default:	
    exit(-1);	
  }
  thread_yield();
}


bool chdir (const char *filename)
{
	bool result;
	lock_acquire (&filesys_mutex);
	result = changeDir (filename);
	lock_release (&filesys_mutex);
	return result;
}

bool mkdir (const char *filename)
{
	bool result;
	lock_acquire (&filesys_mutex);
	result = filesys_create (filename, 0, true);
	lock_release (&filesys_mutex);
	return result;
}

bool readdir (int fd, char *name)
{
	struct file_record *file_r;
	struct file *file;
	bool result;

	lock_acquire (&filesys_mutex);
	file_r = fileRd_ptr (fd);
	if (file_r == NULL)
	{
		lock_release (&filesys_mutex);
		return false;
	}
	file = file_r -> cfile;
	struct inode *inode = file_get_inode (file);
	if (inode == NULL)
	{
		lock_release (&filesys_mutex);
		return false;
	}
	if (! inode_is_dir(inode))
	{
		lock_release (&filesys_mutex);
		return false;
	}
	result = dir_readdir(file_r->dir, name);
	lock_release(&filesys_mutex);
	return result;
}

bool isdir (int fd)
{
	lock_acquire (&filesys_mutex);
	struct file *file = file_ptr (fd);
	bool result = inode_is_dir (file_get_inode(file));
	lock_release(&filesys_mutex);
	return result;
}

int inumber(int fd)
{
	lock_acquire (&filesys_mutex);
	struct file *file = file_ptr(fd);
	int result = inode_get_inumber (file_get_inode(file));
	lock_release(&filesys_mutex);
	return result;
}

void halt (void) 
{
 shutdown_power_off ();
}
void exit (int status) 
{
  struct thread *t = thread_current();
  if (isAlive(t->parent)) {
    struct list *listOfParent = &(t->parent->childlist);
    struct list_elem *e = list_begin(listOfParent);
    while (e != list_end(listOfParent)) {
      struct child_record *cr = list_entry(e, struct child_record, elem);
      if (cr->id == t->tid) {
        cr->retVal = status;
        cr->child = NULL; 
        break;
      }
      e = list_next(e);
    }
  }
  char *save_ptr;
  char *file_name = strtok_r(t->name, " ", &save_ptr); 
  printf("%s: exit(%d)\n", t->name, status);
  if ( t->parent_wait)
   sema_up(&(t->parent->child_sema));
  file_close (t->exefile);
  struct list *templist = &(t -> childlist);
  while (!list_empty (templist)) {
      struct list_elem *cr_elem = list_pop_front (templist);
      struct child_record *tempCR = list_entry(cr_elem, struct child_record, elem);
      free(tempCR);
  }

  templist = &(t->fd_entries);
  while(!list_empty(templist)) {
    struct list_elem *fd_elem = list_pop_front(templist);
    struct file_record *tempFR = list_entry(fd_elem, struct file_record, elem);
    file_close(tempFR->cfile);
    free(tempFR);
  }
  thread_exit ();
}
pid_t exec (const char *cmd_line) 
{
  int result = process_execute(cmd_line);
  sema_down(&(thread_current()->child_load_sema));
  if (thread_current()->child_status == -1) {
    return -1;
  }
  return result;
}
int wait (pid_t pid) 
{
 return process_wait (pid);
}
bool create (const char *file, unsigned initial_size) 
{
  lock_acquire(&filesys_mutex);
  bool result = filesys_create(file, initial_size, false);
  lock_release(&filesys_mutex);
  return result;
}
bool remove (const char *file) 
{

  lock_acquire(&filesys_mutex);
  bool result = filesys_remove(file);
  lock_release(&filesys_mutex);
  return result;
}
int open (const char *file) 
{
  
  lock_acquire(&filesys_mutex);
  struct file_record *cfileRecord;
  cfileRecord = malloc ( sizeof (struct file_record));
  if (cfileRecord == NULL) {
    lock_release(&filesys_mutex);
    return -1;
  }
  struct file *currentfile = filesys_open (file);
  if ( currentfile == NULL) {
    free(cfileRecord);
    lock_release(&filesys_mutex);
    return -1;
  }
  struct thread *t = thread_current();
  cfileRecord -> cfile = currentfile;
  //handle directory
  struct inode *inode = file_get_inode (cfileRecord -> cfile);
  if (inode != NULL && inode_is_dir (inode))
  {
	  cfileRecord->dir = dir_open (inode_reopen(inode));
  }
  else cfileRecord->dir = NULL;
  cfileRecord -> fd = t -> total_fd;
  t -> total_fd = t -> total_fd + 1;
  list_push_back(&(t-> fd_entries), &(cfileRecord ->elem));
  int result = cfileRecord -> fd;
  lock_release(&filesys_mutex);
  return result;
}
int filesize (int fd) 
{
  lock_acquire(&filesys_mutex);
  struct file *tempfile;
  tempfile = file_ptr(fd);
  if (tempfile != NULL) {
    int result = file_length(tempfile);
    lock_release(&filesys_mutex);
  	return result;
  }
  lock_release(&filesys_mutex);
  return -1;
}
int read (int fd, void *buffer, unsigned length) 
{
 lock_acquire(&filesys_mutex);
  if (fd == 1) {
    lock_release(&filesys_mutex);
    return -1;
  }
 if (fd != 0)
 {
   struct file *tempfile = NULL;
   tempfile = file_ptr(fd);
   if (tempfile == NULL) {
      lock_release(&filesys_mutex);
 	    return -1;
   }
    int result = file_read (tempfile, buffer, length);
    lock_release(&filesys_mutex);
    return result;
 }
 else{
    int bytesRead = 0;
    while (bytesRead < length) {
 	    input_getc();
      bytesRead++;
    }
    lock_release(&filesys_mutex);
    return bytesRead; 
	}
}
int write (int fd, const void *buffer, unsigned length)
{
lock_acquire(&filesys_mutex);
  if (fd == 0) {
    lock_release(&filesys_mutex);
    return -1;
  }
if (fd != 1)
 {

  struct file *tempfile;
  tempfile = file_ptr(fd);
  if (inode_is_dir (file_get_inode(tempfile)))
  {
	lock_release (&filesys_mutex);
	return -1;
  }
  if (tempfile == NULL) {
    lock_release(&filesys_mutex);
    return -1;
  }
 	int result = file_write (tempfile, buffer, length);
  lock_release(&filesys_mutex);
  return result;
 }
 else {
  putbuf(buffer, length);
  lock_release(&filesys_mutex);
  return length;
 }

}
void seek (int fd, unsigned position) 
{
  lock_acquire(&filesys_mutex);
  struct file *tempfile;
  tempfile = file_ptr(fd);
 	if (tempfile != NULL) {
    file_seek (tempfile, position);
  }
  lock_release(&filesys_mutex);
}
unsigned tell (int fd) 
{
  lock_acquire(&filesys_mutex);
  struct file *tempfile;
  tempfile = file_ptr(fd);
  if (tempfile == NULL) {
    lock_release(&filesys_mutex);
    return -1;
  }
  unsigned result = file_tell(tempfile);
  lock_release(&filesys_mutex);
  return result;
}
void close (int fd) 
{
  lock_acquire(&filesys_mutex);
   struct thread *t = thread_current();
   struct list *templist = &(t -> fd_entries);
   struct file_record *tempfileRd;
   struct list_elem *e;
   for (e = list_begin (templist); e != list_end (templist);
           e = list_next (e))
   {
	  tempfileRd = list_entry(e,struct file_record, elem);
	  if (tempfileRd->fd == fd) {
      file_close(tempfileRd -> cfile);
      list_remove(e);
      free(tempfileRd);
      break;
		}
   }
  lock_release(&filesys_mutex);
}

struct file * file_ptr(int fd)
{
   struct thread *t = thread_current();
   struct list *templist = &(t -> fd_entries);
   struct file_record *tempfileRd;
   struct list_elem *e;
   if (list_empty(templist))
      return NULL;
   for (e = list_begin (templist); e != list_end (templist);
           e = list_next (e))
   {
	  tempfileRd = list_entry(e,struct file_record, elem);
	  if ( tempfileRd->fd == fd)
	      {
		return tempfileRd -> cfile;
 	      }
   }
  thread_exit();
  return NULL;

}

struct file_record * fileRd_ptr(int fd)
{
 struct thread *t = thread_current();
 struct list *templist = &(t -> fd_entries);
 struct file_record *tempfileRd;
 struct list_elem *e;
 if (list_empty(templist))
	 return NULL;
 for (e = list_begin (templist); e != list_end (templist);
		 e = list_next (e))
 {
	 tempfileRd = list_entry (e, struct file_record, elem);
	 if (tempfileRd->fd == fd)
	 {
		 return tempfileRd;
	 }
 }
 return NULL;
}

static void parse_args(void* esp, int* argBuf, int numToParse) {
  int i;
  for (i = 0; i < numToParse; i++) {
    valid_ptr(esp + ((i + 1) * 4));
    argBuf[i] = *(int*) (esp + ((i + 1) * 4));
  }
} 

static void valid_ptr(void* user_ptr) {
  if (!(is_user_vaddr(user_ptr) && user_ptr > USER_VADDR_BOUND
        && (pagedir_get_page(thread_current()->pagedir, user_ptr) != NULL))) {
    exit(-1);
  }
}

static void valid_buf(char* buf, unsigned size) {
  int i;
  valid_ptr(buf);
  valid_ptr(buf + size - 1);
 /* for (i = 0; i < size; i++) {
    valid_ptr(&buf[i]);
  }*/
}

static void valid_string(void* string) {
  valid_ptr(string);
  while (*(uint8_t*)string != '\0') {
    string++;
    valid_ptr(string);
  }
}


