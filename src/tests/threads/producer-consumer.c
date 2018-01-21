/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define BUF_SIZE 8

char buf[BUF_SIZE]; // Buffer
size_t n = 0; // 0 <= n <= BUF_SIZE: # of characters in buffer.
size_t head = 0;  // buf index of next char to write (mod BUF_SIZE to loop)
size_t tail = 0;  // buf index of next char to read (mod BUF_SIZE to loop)
struct lock lock; // Monitor lock
struct condition not_empty; // Signaled when the buffer is not empty
struct condition not_full;  // Signaled when the buffer is not full.


void producer_consumer(unsigned int num_producer, unsigned int num_consumer);
void put(void*);
void get(void*);

void test_producer_consumer(void)
{

    producer_consumer(0, 0);
    producer_consumer(1, 0);
    producer_consumer(0, 1);
    producer_consumer(1, 1);
    producer_consumer(3, 1);
    producer_consumer(1, 3);
    producer_consumer(4, 4);
    producer_consumer(7, 2);
    producer_consumer(2, 7);
    producer_consumer(6, 6);
    producer_consumer(1000, 1000); 
    pass();
}


void producer_consumer(unsigned int num_producer, unsigned int num_consumer)
{
   // msg("NOT IMPLEMENTED");
   // /* FIXME implement */
  lock_init(&lock);
  lock_acquire(&lock);
  cond_init(&not_empty);
  cond_init(&not_full);
 
  unsigned int i; 
  for (i = 0; i < num_producer; i++) {
    char threadName[16];
    snprintf(threadName, sizeof(threadName), "producer%d", i);
    thread_create(threadName, 1, &put, NULL);
  }
  for (i = 0; i < num_consumer; i++) {
    char threadName[16];
    snprintf(threadName, sizeof(threadName), "consumer%d", i);
    thread_create(threadName, 1, &get, NULL);
  }
  cond_signal(&not_full, &lock);
}

void put(void* arg)  {
  char* input = "Hello world";
  int i = 0;
  for (i = 0; i < 11; i++) {
    lock_acquire(&lock);
    while (n == BUF_SIZE) // Can't add to buf as long as it's full
      cond_wait(&not_full, &lock);
    // Loop for every word in "Hello world"
    buf[head++ % BUF_SIZE] = input[i];
    n++;
    cond_signal(&not_empty, &lock); // buf can't be empty anymore
    lock_release(&lock);
  }

}

void get(void* arg) {
	char ch;
  while (true) {
    lock_acquire(&lock);
    while (n == 0) // Can't read buf as long as it's empty
      cond_wait(&not_empty, &lock);
    ch = buf[tail++ % BUF_SIZE]; // Get ch from buf
    // Print char here
    n--;
    cond_signal(&not_full, &lock);  // buf can't be full anymore
    printf("%c", ch);
    lock_release(&lock);
  }
}

