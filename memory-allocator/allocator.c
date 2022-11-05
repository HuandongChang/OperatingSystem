#define _GNU_SOURCE

#include <assert.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

// The minimum size returned by malloc
#define MIN_MALLOC_SIZE 16

// The magic number
#define MAGIC_NUM 0x0000000FF1CE

// Round a value x up to the next multiple of y
#define ROUND_UP(x, y) ((x) % (y) == 0 ? (x) : (x) + ((y) - (x) % (y)))

// The size of a single page of memory, in bytes
#define PAGE_SIZE 0x1000

intptr_t freelistArray[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// A utility logging function that definitely does not call malloc or free
void log_message(char* message);

// Round the size to 2^n, and then return the index of the freelistArray
size_t round_power_two(size_t x) {
  int leading_zeros = __builtin_clz(x);

  int trailing_zeros = __builtin_ctz(x);

  int index = 32 - leading_zeros - 1;
  if (leading_zeros + trailing_zeros + 1 != 32) index++;
  if (index < 5)
    return (size_t)0;
  else
    return (size_t)index - 4;
}

/**
 * Allocate space on the heap.
 * \param size  The minimium number of bytes that must be allocated
 * \returns     A pointer to the beginning of the allocated space.
 *              This function may return NULL when an error occurs.
 */
void* xxmalloc(size_t size) {
  // find which array we need to use
  size_t index = round_power_two(size);
  // pointer to be returned
  void* p;

  // size larger than 2048 needs its own page
  if (index > 7) {
    size = ROUND_UP(size, PAGE_SIZE);
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  }
  // if there is no space in the page of a given size, we need to request a new page.
  else if (freelistArray[index] == 0) {
    p = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // find the size of each block
    int request_size = 16;
    for (int i = 0; i < index; i++) request_size *= 2;

    // find how many blocks in the page
    int blocks = PAGE_SIZE / request_size;

    // Header: store the size at the start of a page and the magic number after 8 bytes
    *(size_t*)p = request_size;
    *(int*)((intptr_t)p + 8) = MAGIC_NUM;

    // The first block is used for header, second to be returned, and the third goes to the free
    // list.
    freelistArray[index] = (intptr_t)p + request_size * 2;

    // Store the address of the next block in the current block
    for (int i = 1; i <= blocks - 2; i++) {
      *((intptr_t*)((intptr_t)p + i * request_size)) =
          (intptr_t)((intptr_t)p + (i + 1) * request_size);
    }

    // set the start of the last block is 0, indicating 0
    *((intptr_t*)((intptr_t)p + (blocks - 1) * request_size)) = 0;
    // pointer to be returned is the first block
    p = (void*)((intptr_t)p + request_size);

    // If the required_size is 2048, then there is no block left in the free list
    if (index == 7) freelistArray[index] = 0;

  } else {
    // return the first block in the free list and update the head.
    intptr_t p_val = freelistArray[index];
    freelistArray[index] = *(intptr_t*)freelistArray[index];
    p = (void*)p_val;
  }

  // Check for errors
  if (p == MAP_FAILED) {
    log_message("mmap failed! Giving up.\n");
    exit(2);
  }

  return p;
}

/**
 * Free space occupied by a heap object.
 * \param ptr   A pointer somewhere inside the object that is being freed
 */
void xxfree(void* ptr) {
  // Retrieve the usable size
  size_t usable_size = malloc_usable_size(ptr);
  // usable_size == 0 when ptr is NULL or usable_size larger than 2048
  if (usable_size == 0) return;

  // Given the usable size, calculate the index of the free list in the free list array
  int free_index = -4;
  size_t usable_size_copy = usable_size;
  while (usable_size_copy != 1) {
    free_index++;
    usable_size_copy /= 2;
  }

  // Round up the ptr to a multiple of the usable_size
  intptr_t free_pointer = ROUND_UP((intptr_t)ptr, usable_size);
  // If the ptr itself is not a multiple of the usable_size, calculate the start of this block
  if (free_pointer != (intptr_t)ptr) free_pointer -= usable_size;
  // Update the free list
  intptr_t temp_address = freelistArray[free_index];
  *(intptr_t*)(free_pointer) = temp_address;
  freelistArray[free_index] = free_pointer;
}

/**
 * Get the available size of an allocated object. This function should return the amount of space
 * that was actually allocated by malloc, not the amount that was requested.
 * \param ptr   A pointer somewhere inside the allocated object
 * \returns     The number of bytes available for use in this object
 */
size_t xxmalloc_usable_size(void* ptr) {
  // If ptr is NULL always return zero
  if (ptr == NULL) {
    return 0;
  }

  // Round up the ptr to a multiple of the page_size
  intptr_t pointer_start = ROUND_UP((intptr_t)ptr, PAGE_SIZE);
  // If the ptr itself is not a multiple of the page_size, calculate the start of this page
  if (pointer_start != (intptr_t)ptr) pointer_start -= PAGE_SIZE;

  // Check whether the magic number matches
  if (*(int*)(pointer_start + 8) == MAGIC_NUM) {
    // retrive the usable size at the start of the page
    size_t usable_size = *(size_t*)pointer_start;
    return usable_size;
  } else {
    return 0;
  }
}

/**
 * Print a message directly to standard error without invoking malloc or free.
 * \param message   A null-terminated string that contains the message to be printed
 */
void log_message(char* message) {
  // Get the message length
  size_t len = 0;
  while (message[len] != '\0') {
    len++;
  }

  // Write the message
  if (write(STDERR_FILENO, message, len) != len) {
    // Write failed. Try to write an error message, then exit
    char fail_msg[] = "logging failed\n";
    write(STDERR_FILENO, fail_msg, sizeof(fail_msg));
    exit(2);
  }
}
