#define _GNU_SOURCE
#include "lazycopy.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

//making max_pages to keeptrack of how many pages we have, and pages which will be an array of all pages
int max_pages = 0;
void** pages = NULL;
//making num_pages to keep track of how many pages we currently have copied
int num_pages = 0;
//seg finder finds the index of pages where the seg fault occured
int segfinder(siginfo_t* info) {
  //finding address of seg fault
  intptr_t p = (intptr_t)info->si_addr;
  int i = 0;
  while (i < num_pages) { //looping through pages to see if the seg fault occured on each page
    if (((intptr_t)pages[i] <= p) && ((intptr_t)pages[i] + CHUNKSIZE > p)) {
      return i;
    }
    i++;
  }
  return -1;
}

void seg_fault_remap(int signal, siginfo_t* info, void* ctx) {
  //getting index of seg fault page in address
  int i = segfinder(info);
  if (i == -1){ //checking segfault was because we wrote to read only pages
    printf("Life ain't all sunshine and segmentation faults.");
    exit(1);
  }
  //temporarily copying to temp
  void* temp = malloc(CHUNKSIZE);
  if (memcpy(temp, pages[i], CHUNKSIZE) < 0) perror("memcpy Failed");
  //allocating physical memory for page to write to
  pages[i] = mmap(pages[i], CHUNKSIZE, PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_SHARED | MAP_FIXED, -1, 0);

  // Check for an error
  if (pages[i] == MAP_FAILED) {
    perror("mmap failed");
    exit(2);
  }
  //copying data from temp to new page
  if (memcpy(pages[i], temp, CHUNKSIZE) < 0) perror("memcpy Failed");
  free(temp);
}

/**
 * Setting up seg fault handler
 */
void chunk_startup() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(struct sigaction));
  sa.sa_sigaction = seg_fault_remap;
  sa.sa_flags = SA_SIGINFO;

  if (sigaction(SIGSEGV, &sa, NULL) != 0) {
    perror("seg fault failed");
    exit(2);
  }
}

/**
 * This function should return a new chunk of memory for use.
 *
 * \returns a pointer to the beginning of a 64KB chunk of memory that can be read, written, and
 * copied
 */
void* chunk_alloc() {
  // Call mmap to request a new chunk of memory. See comments below for description of arguments.
  void* result = mmap(NULL, CHUNKSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  // Arguments:
  //   NULL: this is the address we'd like to map at. By passing null, we're asking the OS to
  //   decide. CHUNKSIZE: This is the size of the new mapping in bytes. PROT_READ | PROT_WRITE: This
  //   makes the new reading readable and writable MAP_ANONYMOUS | MAP_SHARED: This mapes a new
  //   mapping to cleared memory instead of a file,
  //                               which is another use for mmap. MAP_SHARED makes it possible for
  //                               us to create shared mappings to the same memory.
  //   -1: We're not connecting this memory to a file, so we pass -1 here.
  //   0: This doesn't matter. It would be the offset into a file, but we aren't using one.

  // Check for an error
  if (result == MAP_FAILED) {
    perror("mmap failed in chunk_alloc");
    exit(2);
  }

  // Everything is okay. Return the pointer.
  return result;
}

/**
 * Create a copy of a chunk by copying values eagerly.
 *
 * \param chunk This parameter points to the beginning of a chunk returned from chunk_alloc()
 * \returns a pointer to the beginning of a new chunk that holds a copy of the values from
 *   the original chunk.
 */
void* chunk_copy_eager(void* chunk) {
  // First, we'll allocate a new chunk to copy to
  void* new_chunk = chunk_alloc();

  // Now copy the data
  if (memcpy(new_chunk, chunk, CHUNKSIZE) < 0) perror("memcpy Failed");

  // Return the new chunk
  return new_chunk;
}

/**
 * Create a copy of a chunk by copying values lazily.
 *
 * \param chunk This parameter points to the beginning of a chunk returned from chunk_alloc()
 * \returns a pointer to the beginning of a new chunk that holds a copy of the values from
 *   the original chunk.
 */
void* chunk_copy_lazy(void* chunk) {
  // Just to make sure your code works, this implementation currently calls the eager copy version
  // return chunk_copy_eager(chunk);

  // Your implementation should do the following:
  // 1. Use mremap to create a duplicate mapping of the chunk passed in
  // 2. Mark both mappings as read-only
  // 3. Keep some record of both lazy copies so you can make them writable later.
  //    At a minimum, you'll need to know where the chunk begins and ends.
  // printf("1\n");
  // printf("%d, %d", num_pages, max_pages);
  // checking if we have room in our page array for the new pages
  if (num_pages >= max_pages - 2) {
    max_pages += 64;
    // increasing size of page array if necessary by addding 64 new spots in array
    pages = realloc(pages, sizeof(void*) * max_pages);
    int j = 0;
    //zeroing out new pages
    for (j = 0; j < 64; j++) {
      pages[max_pages - j - 1] = 0;
    }
  }
  //getting virtual address for copy
  void* new_chunk = chunk_alloc();
  //pointing new chunk to old memory (laziness)
  void* result = mremap(chunk, 0, CHUNKSIZE, MREMAP_FIXED | MREMAP_MAYMOVE, new_chunk);
  //checking mremap works
  if (result == MAP_FAILED) {
    perror("mmap failed");
    exit(2);
  }
  // making the old chunk read only
  int result1 = mprotect(chunk, CHUNKSIZE, PROT_READ);
  //checking it worked
  if (result1 == -1) {
    perror("mprotect1 failed");
    exit(2);
  }
  //making the new chunk read only
  int result2 = mprotect(new_chunk, CHUNKSIZE, PROT_READ);
  //error checking
  if (result2 == -1) {
    perror("mprotect2 failed");
    exit(2);
  }
  //finding spot to put the new and old address in our array
  int i = num_pages;
  //placing old chunk and incrementing i
  pages[i++] = chunk;
  //placing new chunk
  pages[i] = new_chunk;
  if ((intptr_t)new_chunk < 0) {
    perror("errno");
    exit(1);
  }
  num_pages += 2;

  return new_chunk;
  // Later, if either copy is written to you will need to:
  // 1. Save the contents of the chunk elsewhere (a local array works well)
  // 2. Use mmap to make a writable mapping at the location of the chunk that was written
  // 3. Restore the contents of the chunk to the new writable mapping
}
