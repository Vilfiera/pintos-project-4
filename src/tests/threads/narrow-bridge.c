/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define LEFT 0
#define RIGHT 1
#define EMERGENCY_LEFT 2
#define EMERGENCY_RIGHT 3

struct semaphore mutex;
int activeL, activeR;
int waitingEL, waitingL, waitingER, waitingR;
struct semaphore ELSem;
struct semaphore LSem;
struct semaphore ERSem;
struct semaphore RSem;
unsigned short lfsr = 0xACE1u;
unsigned bit;

void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right);
void OneVehicleHelper(void*);
void OneVehicle(int, int);
void ArriveBridge(int, int);
void CrossBridge(int, int);
void ExitBridge(int, int);
int rand(void);

void test_narrow_bridge(void)
{
    narrow_bridge(0, 0, 0, 0);
    narrow_bridge(1, 0, 0, 0);
    narrow_bridge(0, 0, 0, 1);
    narrow_bridge(0, 4, 0, 0);
    narrow_bridge(0, 0, 4, 0);
    narrow_bridge(3, 3, 3, 3);
    narrow_bridge(4, 3, 4 ,3);
    narrow_bridge(7, 23, 17, 1);
    narrow_bridge(40, 30, 0, 0);
    narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    narrow_bridge(22, 22, 10, 10);
    narrow_bridge(0, 0, 11, 12);
    narrow_bridge(0, 10, 0, 10);
    narrow_bridge(0, 10, 10, 0);
  
 pass();
}


void narrow_bridge(UNUSED unsigned int num_vehicles_left, UNUSED unsigned int num_vehicles_right,
        UNUSED unsigned int num_emergency_left, UNUSED unsigned int num_emergency_right)
{
    sema_init(&mutex, 1);
    sema_init(&ELSem, 0);
    sema_init(&LSem, 0);
    sema_init(&ERSem, 0);
    sema_init(&RSem, 0);

    activeL = 0;
    activeR = 0;
    waitingEL = 0;
    waitingL = 0;
    waitingER = 0;
    waitingR = 0;

    sema_down(&mutex); 
    unsigned int i;
    for (i = 0; i < num_emergency_left; i++) {
      char threadName[16];
      snprintf(threadName, sizeof(threadName), "EL%d", i);
      int directionPriority = EMERGENCY_LEFT;
      thread_create(threadName, 1, &OneVehicleHelper, (void*) directionPriority);
    }
    for (i = 0; i < num_emergency_right; i++) {
      char threadName[16];
      snprintf(threadName, sizeof(threadName), "ER%d", i);
      int directionPriority = EMERGENCY_RIGHT;
      thread_create(threadName, 1, &OneVehicleHelper, (void*) directionPriority);
    }
    for (i = 0; i < num_vehicles_left; i++) {
      char threadName[16];
      snprintf(threadName, sizeof(threadName), "L%d", i);
      int directionPriority = LEFT;
      thread_create(threadName, 1, &OneVehicleHelper, (void*) directionPriority);
    }

    for (i = 0; i < num_vehicles_right; i++) {
      char threadName[16];
      snprintf(threadName, sizeof(threadName), "R%d", i);
      int directionPriority = RIGHT;
      thread_create(threadName, 1, &OneVehicleHelper, (void*) directionPriority);
    }
    sema_up(&mutex);
}

void OneVehicleHelper(void* arg) {
  int directionPriority = (int) arg;
  switch (directionPriority) {
    case LEFT:
      OneVehicle(0, 0);
      break;
    case RIGHT:
      OneVehicle(1, 0);
      break;
    case EMERGENCY_LEFT:
      OneVehicle(0, 1);
      break;
    case EMERGENCY_RIGHT:
      OneVehicle(1, 1);
      break;
    default:
      printf("Error: Invalid input.\n");
  }
}

void OneVehicle(int direc, int prio) {
  ArriveBridge(direc, prio);
  CrossBridge(direc, prio);
  ExitBridge(direc, prio);
}

void ArriveBridge(int direc, int prio) {
  sema_down(&mutex);
  if (direc == 0) { // Cars heading left
    if (activeR == 0) { // No cars heading right
      if (activeL < 3) { 
        if (prio == 1) { // Emergency vehicles can enter
          activeL++;
          sema_up(&ELSem);
        } else if ((waitingEL + waitingER) == 0){ // Prioritizes emergencies. 
          activeL++;
          sema_up(&LSem);
        } else {
          if (prio == 1) {
            waitingEL++;
          } else {
            waitingL++;
          }
        }
      } else {
        if (prio == 1) {
          waitingEL++;
        } else {
          waitingL++;
        }
      }
    } else {
      if (prio == 1) {
        waitingEL++;
      } else {
        waitingL++;
      }
    } 
  } else if (direc == 1) { // Cars heading right
    if (activeL == 0) { // No cars heading left
      if (activeR < 3) {
        if (prio == 1) { // Emergency vehicles can enter
          activeR++;
          sema_up(&ERSem);
        } else if ((waitingEL + waitingER) == 0) { // Prioritizes emergencies.
          activeR++;
          sema_up(&RSem);
        } else {
          if (prio == 1) {
            waitingER++;
          } else {
            waitingR++;
          }
        }
      } else {
        if (prio == 1) {
          waitingER++;
        } else {
          waitingR++;
        }
      }
    } else {
      if (prio == 1) {
        waitingER++;
      } else {
        waitingR++;
      }
    }
  }
  sema_up(&mutex);
  if (prio == 1) {
    if (direc == 0) {
      sema_down(&ELSem);
    } else {
      sema_down(&ERSem);
    }
  } else {
    if (direc == 0) {
      sema_down(&LSem);
    } else {
      sema_down(&RSem);
    }
  }  
}

void CrossBridge(int direc, int prio) {
  printf("Vehicle with direction %d, priority %d, entering bridge.\n", direc, prio);
  block_current_thread(rand());
  printf("Vehicle with direction %d, priority %d, exiting bridge\n", direc, prio);  
}
void ExitBridge(int direc, UNUSED int prio) {
  sema_down(&mutex);
  if (direc == 0) {
    activeL--;
    // If we are the last vehicle going left, we can allow vehicles to go right.
    if (activeL == 0 && (waitingER + waitingR) > 0) {
      int count = 0; // Ensures only 3 vehicles get to enter the bridge.
      while (waitingER > 0 && count < 3) { // Prioritizes emergency vehicles.
        sema_up(&ERSem);
        activeR++;
        waitingER--;
        count++;
      }
      while (waitingR > 0 && count < 3) {
        sema_up(&RSem);
        activeR++;
        waitingR--;
        count++;
      }
    } else if (waitingEL > 0) {
      sema_up(&ELSem);
      activeL++;
      waitingEL--;
    } else if (waitingL > 0 && waitingER == 0) {
      sema_up(&LSem);
      activeL++;
      waitingL--;
    } 
  } else if (direc == 1) {
    activeR--;
    // If we are the last vehicle going right, we can allow vehicles to go left.
    if (activeR == 0 && (waitingEL + waitingL) > 0) {
      int count = 0; // Ensures only 3 vehicles get to enter the bridge.
      while (waitingEL > 0 && count < 3) { // Prioritizes emergency vehicles.
        sema_up(&ELSem);
        activeL++;
        waitingEL--;
        count++;
      }
      while (waitingL > 0 && count < 3) {
        sema_up(&LSem);
        activeL++;
        waitingL--;
        count++;
      }
    } else if (waitingER > 0) {
      sema_up(&ERSem);
      activeR++;
      waitingER--;
    } else if (waitingR > 0 && waitingEL == 0) {
      sema_up(&RSem);
      activeR++;
      waitingR--;
    } 
  }
  sema_up(&mutex); 
}

// Returns random integer, used in CrossBridge().
// Taken from: https://stackoverflow.com/questions/7602919/
// how-do-i-generate-random-numbers-without-rand-function
int rand() {
  bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) & (lfsr >> 5)) & 1;
  return lfsr = (lfsr >> 1) | (bit << 15);
}
